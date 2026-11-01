/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session_deposit.h"

#include "apiwrap.h"
#include "gram/api/gram_api_nft.h"
#include "gram/gram_boc.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "wallet/wallet_address.h"

#include "wallet_engine.hpp"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Wallet {
namespace {

namespace engine = wallet_engine;

[[nodiscard]] bool SameAddress(const QString &a, const QString &b) {
	return !a.isEmpty() && !a.compare(b, Qt::CaseInsensitive);
}

[[nodiscard]] std::optional<QString> NullableAddress(
		const QJsonObject &object,
		const QString &key) {
	const auto value = object.value(key);
	if (value.isNull() || value.isUndefined()) {
		return QString();
	}
	const auto result = CanonicalAddress(value.toString());
	return result.isEmpty() ? std::nullopt : std::make_optional(result);
}

[[nodiscard]] SendError GiftDepositOwnerError(
		const QByteArray &json,
		const QString &item,
		const QString &own) {
	const auto items = QJsonDocument::fromJson(json).object().value(
		u"nft_items"_q);
	const auto list = items.toArray();
	if (!items.isArray() || list.size() > 1) {
		return SendError::Failed;
	} else if (list.isEmpty()) {
		return SendError::CollectibleUnavailable;
	}
	const auto entry = list.first().toObject();
	const auto address = NullableAddress(entry, u"address"_q);
	const auto owner = NullableAddress(entry, u"owner_address"_q);
	const auto sale = NullableAddress(entry, u"sale_contract_address"_q);
	const auto auction = NullableAddress(
		entry,
		u"auction_contract_address"_q);
	const auto init = entry.value(u"init"_q);
	const auto onSale = entry.value(u"on_sale"_q);
	if (!address
		|| !SameAddress(*address, item)
		|| !owner
		|| !sale
		|| !auction
		|| !init.isBool()
		|| !onSale.isBool()) {
		return SendError::Failed;
	}
	return (init.toBool()
		&& !onSale.toBool()
		&& sale->isEmpty()
		&& auction->isEmpty()
		&& SameAddress(*owner, own))
		? SendError::None
		: SendError::CollectibleUnavailable;
}

} // namespace

void RequestGiftDeposit(
		not_null<Main::Session*> session,
		const QString &collectible,
		const QString &slug,
		Fn<void(GiftDeposit)> done) {
	session->api().request(MTPpayments_DepositStarGift(
		MTP_flags(0),
		MTP_inputPeerSelf(),
		MTP_string(slug)
	)).done([=](const MTPpayments_StarGiftDepositPayload &result) {
		done(GiftDepositFromServer(result, collectible));
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: payments.depositStarGift failed: %1."
			).arg(error.type()));
		done({
			.error = GiftDepositError::Request,
			.type = error.type(),
			.silent = MTP::IgnoreError(error),
		});
	}).send();
}

GiftDeposit GiftDepositFromServer(
		const MTPpayments_StarGiftDepositPayload &answer,
		const QString &collectible) {
	const auto &data = answer.data();
	const auto nft = CanonicalAddress(qs(data.vnft_address()));
	if (!SameAddress(nft, CanonicalAddress(collectible))) {
		return { .error = GiftDepositError::AddressMismatch };
	}
	const auto amount = int64(data.vamount().v);
	const auto payload = QString::fromLatin1(data.vpayload().v.toBase64());
	const auto escrow = CanonicalAddress(Gram::NftTransferNewOwner(payload));
	if (amount <= 0 || escrow.isEmpty()) {
		return { .error = GiftDepositError::InvalidAnswer };
	}
	return {
		.args = SendArgs{
			.destination = escrow,
			.collectible = nft,
			.payload = payload,
			.amountNano = amount,
			.bounce = true,
		},
	};
}

FeeResult GiftDepositFee(
		const engine::SendEmulation &emulation,
		const QString &collectible) {
	const auto item = CanonicalAddress(collectible);
	const auto rootSucceeded = ranges::any_of(
		emulation.transactions,
		[](const engine::SendEmulationTransaction &transaction) {
			return transaction.is_root && transaction.succeeded;
		});
	const auto itemSucceeded = ranges::any_of(
		emulation.transactions,
		[&](const engine::SendEmulationTransaction &transaction) {
			const auto account = QString::fromStdString(transaction.account);
			return transaction.succeeded
				&& SameAddress(CanonicalAddress(account), item);
		});
	if (emulation.is_incomplete || !rootSucceeded || !itemSucceeded) {
		return { .error = SendError::CollectibleRejected };
	}
	auto ok = false;
	const auto fee = QString::fromStdString(
		emulation.trace_fees_nanograms).toLongLong(&ok);
	return (ok && fee >= 0)
		? FeeResult{ .feeNano = fee }
		: FeeResult{ .error = SendError::Failed };
}

SendError GiftDepositInputError(
		const SendArgs &args,
		const std::vector<CollectibleRecord> &items,
		bool listComplete) {
	if (args.payload.isEmpty()) {
		return SendError::None;
	}
	const auto item = CanonicalAddress(args.collectible);
	const auto escrow = CanonicalAddress(
		Gram::NftTransferNewOwner(args.payload));
	if (item.isEmpty()
		|| args.amountNano <= 0
		|| !SameAddress(escrow, CanonicalAddress(args.destination))
		|| args.userId
		|| !args.comment.text.isEmpty()
		|| !args.recipientPublicKey.isEmpty()
		|| !args.bounce) {
		return SendError::InvalidRequest;
	}
	const auto listed = [&](const CollectibleRecord &entry) {
		return SameAddress(entry.address, item);
	};
	return (listComplete && ranges::none_of(items, listed))
		? SendError::CollectibleUnavailable
		: SendError::None;
}

bool GiftDepositRejected(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::EmulationRejected &) {
		return true;
	} catch (const engine::wallet_client_error
			::EmulationMessageNotAccepted &) {
		return true;
	} catch (...) {
	}
	return false;
}

void CheckGiftDepositOwner(
		Api &api,
		const QString &wallet,
		const QString &collectible,
		std::shared_ptr<rpl::lifetime> registration,
		Fn<void(SendError)> refused,
		Fn<void()> owned) {
	struct Admission {
		std::shared_ptr<rpl::lifetime> registration;
		Fn<void(SendError)> refused;
		Fn<void()> owned;
	};
	const auto own = CanonicalAddress(wallet);
	const auto item = CanonicalAddress(collectible);
	const auto admission = std::make_shared<Admission>(Admission{
		.registration = std::move(registration),
		.refused = std::move(refused),
		.owned = std::move(owned),
	});
	const auto answer = [=](SendError error) {
		const auto taken = base::take(*admission);
		if (error == SendError::None) {
			taken.owned();
			return;
		}
		LOG(("Wallet Error: the gift deposit owner check answered %1."
			).arg(int(error)));
		if (taken.refused) {
			taken.refused(error);
		}
	};
	api.request(
		Gram::NftItemByAddressRequest(item),
		[=](const QByteArray &json) {
			answer(GiftDepositOwnerError(json, item, own));
		},
		[=](const Gram::ApiError &error) {
			answer((error.code == 406)
				? SendError::Silent
				: SendError::Failed);
		});
}

engine::SendIntent IntentFromArgs(
		const SendArgs &args,
		engine::SendMessageBody body) {
	auto message = engine::SendMessage{
		.destination = FormatFriendly(
			args.payload.isEmpty() ? args.destination : args.collectible,
			args.bounce).toStdString(),
		.amount = engine::SendAmount(engine::SendAmount::kExact{
			.nanograms = QString::number(args.amountNano).toStdString(),
		}),
		.body = std::move(body),
		.bounce = args.bounce,
		.state_init = std::nullopt,
	};
	return engine::SendIntent{
		.expiration = engine::SendExpiration(
			engine::SendExpiration::kEngineDefault{}),
		.messages = { std::move(message) },
	};
}

} // namespace Wallet
