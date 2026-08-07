/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_history.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#include <algorithm>

namespace Gram {
namespace {

[[nodiscard]] std::optional<Address> ParseRawAccount(const QJsonValue &value) {
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto parsed = ParseAddress(value.toString());
	if (!parsed) {
		return std::nullopt;
	}
	return parsed->address;
}

[[nodiscard]] std::optional<Address> ParseDecodedAddress(
		const QJsonValue &value) {
	if (!value.isObject()) {
		return std::nullopt;
	}
	const auto object = value.toObject();
	const auto workchain = object.value(u"workchain_id"_q);
	const auto address = object.value(u"address"_q);
	if (!workchain.isString() || !address.isString()) {
		return std::nullopt;
	}
	const auto parsed = ParseAddress(
		workchain.toString() + u":"_q + address.toString());
	if (!parsed) {
		return std::nullopt;
	}
	return parsed->address;
}

[[nodiscard]] std::optional<int64> PositiveValue(const QJsonValue &value) {
	const auto result = ApiDetails::ParseInt64String(value);
	return (result && *result > 0) ? result : std::nullopt;
}

[[nodiscard]] TransferItem::Status ComputeTxStatus(const QJsonObject &tx) {
	const auto description = tx.value(u"description"_q).toObject();
	return ApiDetails::ComputeSuccess(description)
		? TransferItem::Status::Success
		: TransferItem::Status::Failure;
}

[[nodiscard]] TransferItem::Status IncomingStatus(
		const QJsonObject &tx,
		TransferItem::Status fallback) {
	const auto description = tx.value(u"description"_q).toObject();
	const auto creditPh = description.value(u"credit_ph"_q).toObject();
	const auto credit = ApiDetails::ParseInt64String(
		creditPh.value(u"credit"_q));
	if (credit && *credit > 0) {
		return TransferItem::Status::Success;
	}
	return fallback;
}

[[nodiscard]] QJsonObject DecodedBody(const QJsonObject &message) {
	return message.value(u"message_content"_q)
		.toObject()
		.value(u"decoded"_q)
		.toObject();
}

[[nodiscard]] QString ExtractComment(const QJsonObject &message) {
	const auto object = DecodedBody(message);
	const auto comment = object.value(u"comment"_q);
	if (comment.isString() && !comment.toString().isEmpty()) {
		return comment.toString();
	}
	const auto text = object.value(u"text"_q);
	if (object.value(u"@type"_q).toString() == u"text_comment"_q
		&& text.isString()
		&& !text.toString().isEmpty()) {
		return text.toString();
	}
	return QString();
}

[[nodiscard]] QString DecodedType(const QJsonObject &message) {
	return DecodedBody(message).value(u"@type"_q).toString();
}

[[nodiscard]] bool TraceHasJettonOrNft(const QJsonObject &transactions) {
	const auto detect = [&](const QJsonObject &message) {
		const auto type = DecodedType(message);
		return type == u"jetton_transfer"_q
			|| type == u"jetton_internal_transfer"_q
			|| type == u"nft_transfer"_q
			|| type == u"nft_ownership_assigned"_q
			|| type == u"nft_owner_changed"_q;
	};
	for (const auto &value : transactions) {
		const auto tx = value.toObject();
		if (detect(tx.value(u"in_msg"_q).toObject())) {
			return true;
		}
		const auto outMsgs = tx.value(u"out_msgs"_q).toArray();
		for (const auto &outMsg : outMsgs) {
			if (detect(outMsg.toObject())) {
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] QByteArray TraceExternalHashNorm(
		const QJsonObject &trace,
		const QJsonObject &transactions,
		const QJsonArray &order) {
	auto extHash = QString();
	if (!order.isEmpty() && order.at(0).isString()) {
		const auto firstTx = transactions.value(order.at(0).toString())
			.toObject();
		const auto inMsg = firstTx.value(u"in_msg"_q).toObject();
		const auto hashNorm = inMsg.value(u"hash_norm"_q);
		const auto hash = inMsg.value(u"hash"_q);
		if (hashNorm.isString()) {
			extHash = hashNorm.toString();
		} else if (hash.isString()) {
			extHash = hash.toString();
		}
	}
	if (extHash.isEmpty()) {
		const auto external = trace.value(u"external_hash"_q);
		if (external.isString()) {
			extHash = external.toString();
		}
	}
	if (extHash.isEmpty()) {
		const auto inMsgHash = trace.value(u"trace"_q)
			.toObject()
			.value(u"in_msg_hash"_q);
		if (inMsgHash.isString()) {
			extHash = inMsgHash.toString();
		}
	}
	return extHash.isEmpty()
		? QByteArray()
		: ApiDetails::DecodeAnyBase64(extHash);
}

[[nodiscard]] bool TraceIsPending(const QJsonObject &trace) {
	if (trace.value(u"is_incomplete"_q).toBool()) {
		return true;
	}
	const auto info = trace.value(u"trace_info"_q).toObject();
	return (info.value(u"trace_state"_q).toString() == u"pending"_q)
		&& (info.value(u"pending_messages"_q).toInt() > 0);
}

[[nodiscard]] QString AddressBookDomain(
		const QJsonObject &addressBook,
		const Address &address) {
	if (address.hash.isEmpty()) {
		return QString();
	}
	const auto domain = addressBook
		.value(FormatRaw(address).toUpper())
		.toObject()
		.value(u"domain"_q);
	return domain.isString() ? domain.toString() : QString();
}

void ApplyCounterpartyNames(
		std::vector<TransferItem> &items,
		const QJsonObject &addressBook) {
	for (auto &item : items) {
		item.counterpartyName = AddressBookDomain(
			addressBook,
			item.counterparty);
	}
}

[[nodiscard]] QJsonObject IndexedNftTokenInfo(
		const QJsonObject &metadata,
		const Address &item) {
	const auto entry = metadata.value(FormatRaw(item).toUpper()).toObject();
	if (!entry.value(u"is_indexed"_q).toBool()) {
		return QJsonObject();
	}
	const auto list = entry.value(u"token_info"_q).toArray();
	for (const auto &value : list) {
		const auto info = value.toObject();
		if (info.value(u"type"_q).toString() == u"nft_items"_q) {
			return info;
		}
	}
	return QJsonObject();
}

[[nodiscard]] TransferItem MakeContractInteraction(
		const QJsonObject &sourceTx,
		const std::optional<QJsonObject> &opcodeOutMsg,
		TimeId date,
		quint64 lt,
		const QByteArray &traceId,
		const QByteArray &externalHashNorm,
		bool pending) {
	auto item = TransferItem();
	item.kind = TransferItem::Kind::ContractInteraction;
	item.incoming = false;
	if (opcodeOutMsg) {
		if (const auto destination = ParseRawAccount(
				opcodeOutMsg->value(u"destination"_q))) {
			item.counterparty = *destination;
		}
		if (const auto value = ApiDetails::ParseInt64String(
				opcodeOutMsg->value(u"value"_q))) {
			item.amountNano = *value;
		}
	} else {
		const auto inMsg = sourceTx.value(u"in_msg"_q).toObject();
		if (const auto source = ParseRawAccount(inMsg.value(u"source"_q))) {
			item.counterparty = *source;
		}
		if (const auto value = ApiDetails::ParseInt64String(
				inMsg.value(u"value"_q))) {
			item.amountNano = *value;
		}
	}
	if (const auto fees = ApiDetails::ParseInt64String(
			sourceTx.value(u"total_fees"_q))) {
		item.feeNano = *fees;
	}
	item.date = date;
	item.lt = lt;
	item.traceId = traceId;
	item.externalHashNorm = externalHashNorm;
	item.status = pending
		? TransferItem::Status::Pending
		: ComputeTxStatus(sourceTx);
	return item;
}

[[nodiscard]] std::optional<TransferItem> MakeCollectible(
		const QJsonObject &tx,
		const QJsonObject &metadata,
		TimeId date,
		quint64 lt,
		const QByteArray &traceId,
		const QByteArray &externalHashNorm,
		bool pending) {
	auto item = TransferItem();
	const auto applyLeg = [&](
			const QJsonObject &message,
			const QString &itemKey,
			const QString &ownerKey,
			bool incoming) {
		const auto collectible = ParseRawAccount(message.value(itemKey));
		if (!collectible) {
			return false;
		}
		item.incoming = incoming;
		item.collectible = *collectible;
		if (const auto owner = ParseDecodedAddress(
				DecodedBody(message).value(ownerKey))) {
			item.counterparty = *owner;
		}
		if (const auto value = ApiDetails::ParseInt64String(
				message.value(u"value"_q))) {
			item.amountNano = *value;
		}
		return true;
	};
	auto found = false;
	const auto outMsgs = tx.value(u"out_msgs"_q).toArray();
	for (const auto &outMsgValue : outMsgs) {
		const auto msg = outMsgValue.toObject();
		if (DecodedType(msg) == u"nft_transfer"_q
			&& applyLeg(msg, u"destination"_q, u"new_owner"_q, false)) {
			found = true;
			break;
		}
	}
	if (!found) {
		const auto inMsg = tx.value(u"in_msg"_q).toObject();
		found = (DecodedType(inMsg) == u"nft_ownership_assigned"_q)
			&& applyLeg(inMsg, u"source"_q, u"prev_owner"_q, true);
	}
	if (!found) {
		return std::nullopt;
	}
	item.kind = TransferItem::Kind::Collectible;
	const auto info = IndexedNftTokenInfo(metadata, item.collectible);
	item.collectibleName = info.value(u"name"_q).toString();
	item.collectibleImageUrl = info.value(u"image"_q).toString();
	if (const auto fees = ApiDetails::ParseInt64String(
			tx.value(u"total_fees"_q))) {
		item.feeNano = *fees;
	}
	item.date = date;
	item.lt = lt;
	item.traceId = traceId;
	item.externalHashNorm = externalHashNorm;
	const auto status = ComputeTxStatus(tx);
	item.status = pending
		? TransferItem::Status::Pending
		: item.incoming
		? IncomingStatus(tx, status)
		: status;
	return item;
}

[[nodiscard]] bool AppendTransferLegs(
		std::vector<TransferItem> &items,
		const QJsonObject &tx,
		int64 fees,
		TransferItem::Status outgoingStatus,
		TransferItem::Status incomingStatus,
		TimeId date,
		quint64 lt,
		const QByteArray &traceId,
		const QByteArray &externalHashNorm) {
	const auto outMsgs = tx.value(u"out_msgs"_q).toArray();
	for (const auto &outMsgValue : outMsgs) {
		const auto msg = outMsgValue.toObject();
		const auto value = msg.value(u"value"_q);
		if (!value.isString()
			&& !value.isNull()
			&& !value.isUndefined()) {
			return false;
		}
		const auto amount = PositiveValue(value);
		if (!amount) {
			continue;
		}
		const auto destination = ParseRawAccount(
			msg.value(u"destination"_q));
		if (!destination) {
			return false;
		}
		auto item = TransferItem();
		item.kind = TransferItem::Kind::Transfer;
		item.incoming = false;
		item.counterparty = *destination;
		item.amountNano = *amount;
		item.feeNano = fees;
		item.comment = ExtractComment(msg);
		item.date = date;
		item.lt = lt;
		item.traceId = traceId;
		item.externalHashNorm = externalHashNorm;
		item.status = outgoingStatus;
		items.push_back(std::move(item));
	}

	const auto inMsg = tx.value(u"in_msg"_q).toObject();
	const auto inValue = inMsg.value(u"value"_q);
	if (!inValue.isString()
		&& !inValue.isNull()
		&& !inValue.isUndefined()) {
		return false;
	}
	if (const auto amount = PositiveValue(inValue)) {
		const auto source = ParseRawAccount(inMsg.value(u"source"_q));
		if (!source) {
			return false;
		}
		auto item = TransferItem();
		item.kind = TransferItem::Kind::Transfer;
		item.incoming = true;
		item.counterparty = *source;
		item.amountNano = *amount;
		item.feeNano = fees;
		item.comment = ExtractComment(inMsg);
		item.date = date;
		item.lt = lt;
		item.traceId = traceId;
		item.externalHashNorm = externalHashNorm;
		item.status = incomingStatus;
		items.push_back(std::move(item));
	}
	return true;
}

[[nodiscard]] std::optional<std::vector<TransferItem>> ParseTraceItems(
		const QJsonObject &trace,
		const QJsonObject &metadata,
		const Address &self) {
	const auto traceIdValue = trace.value(u"trace_id"_q);
	if (!traceIdValue.isString()) {
		return std::nullopt;
	}
	const auto traceId = ApiDetails::DecodeAnyBase64(traceIdValue.toString());

	const auto startUtime = trace.value(u"start_utime"_q);
	if (!startUtime.isDouble()) {
		return std::nullopt;
	}
	const auto date = TimeId(startUtime.toInt());

	const auto lt = ApiDetails::ParseUint64String(trace.value(u"start_lt"_q));
	if (!lt) {
		return std::nullopt;
	}

	const auto transactionsValue = trace.value(u"transactions"_q);
	if (!transactionsValue.isObject()) {
		return std::nullopt;
	}
	const auto transactions = transactionsValue.toObject();

	const auto order = trace.value(u"transactions_order"_q).toArray();
	const auto extHashNorm = TraceExternalHashNorm(trace, transactions, order);
	const auto pending = TraceIsPending(trace);

	auto orderedKeys = std::vector<QString>();
	if (!order.isEmpty()) {
		for (const auto &entry : order) {
			if (entry.isString()) {
				orderedKeys.push_back(entry.toString());
			}
		}
	} else {
		for (auto i = transactions.begin(); i != transactions.end(); ++i) {
			orderedKeys.push_back(i.key());
		}
	}

	auto items = std::vector<TransferItem>();
	auto firstOwnTx = std::optional<QJsonObject>();
	auto contractTx = std::optional<QJsonObject>();
	auto contractOutMsg = std::optional<QJsonObject>();
	auto collectible = std::optional<TransferItem>();
	for (const auto &key : orderedKeys) {
		const auto tx = transactions.value(key).toObject();
		const auto account = ParseRawAccount(tx.value(u"account"_q));
		if (!account || !(*account == self)) {
			continue;
		}
		if (!firstOwnTx) {
			firstOwnTx = tx;
		}
		const auto fees = ApiDetails::ParseInt64String(
			tx.value(u"total_fees"_q));
		if (!fees) {
			return std::nullopt;
		}
		const auto status = pending
			? TransferItem::Status::Pending
			: ComputeTxStatus(tx);
		const auto incomingStatus = pending
			? TransferItem::Status::Pending
			: IncomingStatus(tx, ComputeTxStatus(tx));
		if (!AppendTransferLegs(
				items,
				tx,
				*fees,
				status,
				incomingStatus,
				date,
				*lt,
				traceId,
				extHashNorm)) {
			return std::nullopt;
		}
		if (!contractTx) {
			const auto outMsgs = tx.value(u"out_msgs"_q).toArray();
			for (const auto &outMsgValue : outMsgs) {
				const auto msg = outMsgValue.toObject();
				if (msg.value(u"opcode"_q).isString()) {
					contractTx = tx;
					contractOutMsg = msg;
					break;
				}
			}
		}
		if (!collectible) {
			collectible = MakeCollectible(
				tx,
				metadata,
				date,
				*lt,
				traceId,
				extHashNorm,
				pending);
		}
	}

	if (collectible) {
		return std::vector<TransferItem>{ *collectible };
	}
	if (TraceHasJettonOrNft(transactions)) {
		const auto sourceTx = contractTx ? contractTx : firstOwnTx;
		if (!sourceTx) {
			return std::vector<TransferItem>();
		}
		return std::vector<TransferItem>{
			MakeContractInteraction(
				*sourceTx,
				contractOutMsg,
				date,
				*lt,
				traceId,
				extHashNorm,
				pending),
		};
	}
	if (!items.empty()) {
		return items;
	}
	if (contractTx) {
		return std::vector<TransferItem>{
			MakeContractInteraction(
				*contractTx,
				contractOutMsg,
				date,
				*lt,
				traceId,
				extHashNorm,
				pending),
		};
	}
	return std::vector<TransferItem>();
}

[[nodiscard]] QString AccountLimitOffsetQuery(
		const QString &account,
		int limit,
		int offset) {
	const auto clampedLimit = std::clamp(limit, 0, 100);
	const auto clampedOffset = std::max(offset, 0);
	return u"account="_q
		+ ApiDetails::PercentEncoded(account)
		+ u"&limit="_q
		+ QString::number(clampedLimit)
		+ u"&offset="_q
		+ QString::number(clampedOffset);
}

} // namespace

HttpRequest TracesRequest(const QString &account, int limit, int offset) {
	auto result = HttpRequest();
	result.post = false;
	result.endpoint = u"/api/v3/traces"_q;
	result.query = AccountLimitOffsetQuery(account, limit, offset);
	return result;
}

std::optional<HistoryPage> ParseTraces(
		const QByteArray &json,
		const Address &self,
		int limit) {
	if (ParseApiError(json)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto root = document.object();
	const auto tracesValue = root.value(u"traces"_q);
	if (!tracesValue.isArray()) {
		return std::nullopt;
	}
	const auto traces = tracesValue.toArray();
	const auto addressBook = root.value(u"address_book"_q).toObject();
	const auto metadata = root.value(u"metadata"_q).toObject();

	auto page = HistoryPage();
	for (const auto &traceValue : traces) {
		if (!traceValue.isObject()) {
			return std::nullopt;
		}
		auto items = ParseTraceItems(traceValue.toObject(), metadata, self);
		if (!items) {
			return std::nullopt;
		}
		for (auto &item : *items) {
			page.list.push_back(std::move(item));
		}
	}
	ApplyCounterpartyNames(page.list, addressBook);
	page.hasNext = int(traces.size()) >= limit;
	return page;
}

HttpRequest TransactionsRequest(
		const QString &account,
		int limit,
		int offset) {
	auto result = HttpRequest();
	result.post = false;
	result.endpoint = u"/api/v3/transactions"_q;
	result.query = AccountLimitOffsetQuery(account, limit, offset);
	return result;
}

std::optional<HistoryPage> ParseTransactions(
		const QByteArray &json,
		const Address &self,
		int limit) {
	if (ParseApiError(json)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto root = document.object();
	const auto transactionsValue = root.value(u"transactions"_q);
	if (!transactionsValue.isArray()) {
		return std::nullopt;
	}
	const auto transactions = transactionsValue.toArray();
	const auto addressBook = root.value(u"address_book"_q).toObject();

	auto page = HistoryPage();
	for (const auto &txValue : transactions) {
		const auto tx = txValue.toObject();
		const auto account = ParseRawAccount(tx.value(u"account"_q));
		if (!account || !(*account == self)) {
			continue;
		}
		const auto fees = ApiDetails::ParseInt64String(
			tx.value(u"total_fees"_q));
		if (!fees) {
			return std::nullopt;
		}
		const auto now = tx.value(u"now"_q);
		if (!now.isDouble()) {
			return std::nullopt;
		}
		const auto date = TimeId(now.toInt());
		const auto lt = ApiDetails::ParseUint64String(tx.value(u"lt"_q));
		if (!lt) {
			return std::nullopt;
		}
		auto traceId = QByteArray();
		const auto traceIdValue = tx.value(u"trace_id"_q);
		if (traceIdValue.isString()) {
			traceId = ApiDetails::DecodeAnyBase64(traceIdValue.toString());
		}
		const auto inMsg = tx.value(u"in_msg"_q).toObject();
		auto externalHashNorm = QByteArray();
		const auto hashNorm = inMsg.value(u"hash_norm"_q);
		if (hashNorm.isString()) {
			externalHashNorm = ApiDetails::DecodeAnyBase64(hashNorm.toString());
		}
		const auto status = ComputeTxStatus(tx);
		if (!AppendTransferLegs(
				page.list,
				tx,
				*fees,
				status,
				IncomingStatus(tx, status),
				date,
				*lt,
				traceId,
				externalHashNorm)) {
			return std::nullopt;
		}
	}
	ApplyCounterpartyNames(page.list, addressBook);
	page.hasNext = int(transactions.size()) >= limit;
	return page;
}

HttpRequest TransactionsByMessageRequest(const QByteArray &msgHashNorm) {
	auto result = HttpRequest();
	result.post = false;
	result.endpoint = u"/api/v3/transactionsByMessage"_q;
	result.query = u"msg_hash="_q
		+ ApiDetails::PercentEncoded(
			QString::fromLatin1(msgHashNorm.toBase64()));
	return result;
}

std::optional<bool> ParseTransactionsByMessageFound(const QByteArray &json) {
	if (ParseApiError(json)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto transactions = document.object().value(u"transactions"_q);
	if (!transactions.isArray()) {
		return std::nullopt;
	}
	return !transactions.toArray().isEmpty();
}

} // namespace Gram
