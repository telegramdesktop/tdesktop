/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_claims.h"

#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_custody.h"

namespace Wallet {
namespace {

const auto kStorageKey = u"presentation/ton-connect-claims"_q;
constexpr auto kFormatVersion = quint32(1);
constexpr auto kTraceIdMaxLength = 256;
constexpr auto kAnswerMaxBytes = 64 * 1024;
constexpr auto kOperationIdMaxBytes = 256;
constexpr auto kSignedBocMaxLength = 24 * 1024;

[[nodiscard]] bool ValidRecord(const TonConnectClaimRecord &record) {
	using Decision = TonConnectClaimDecision;
	if (!record.sessionId
		|| record.msgId <= 0
		|| !TonConnectRequestIdValid(record.requestId)
		|| record.traceId.size() > kTraceIdMaxLength
		|| record.expires <= 0
		|| record.created <= 0
		|| CanonicalAddress(record.address) != record.address
		|| record.publicKey.size() != kCustodyPublicKeySize
		|| record.answer.size() > kAnswerMaxBytes
		|| record.notSent.size() > kAnswerMaxBytes
		|| record.operationId.size() > kOperationIdMaxBytes
		|| record.signedBoc.size() > kSignedBocMaxLength
		|| (!record.signedBoc.isEmpty() && record.operationId.empty())) {
		return false;
	}
	switch (record.decision) {
	case Decision::Confirm:
		return !record.notSent.isEmpty();
	case Decision::Answer:
		return !record.answer.isEmpty()
			&& record.notSent.isEmpty()
			&& record.operationId.empty()
			&& record.signedBoc.isEmpty();
	}
	return false;
}

[[nodiscard]] bool SameKey(
		const TonConnectClaimRecord &a,
		const TonConnectClaimRecord &b) {
	return TonConnectClaimMatches(a, b.sessionId, b.msgId);
}

[[nodiscard]] bool ValidStore(const TonConnectClaimStore &store) {
	const auto &records = store.records;
	if (records.size() > kTonConnectClaimMaxRecords) {
		return false;
	}
	for (auto i = begin(records); i != end(records); ++i) {
		if (!ValidRecord(*i)
			|| std::any_of(begin(records), i, [&](const auto &other) {
				return SameKey(other, *i);
			})) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] std::optional<TonConnectClaimRecord> ReadRecord(
		Serialize::ByteArrayReader &stream) {
	auto result = TonConnectClaimRecord();
	auto sessionId = quint64();
	auto msgId = qint32();
	auto expires = qint32();
	auto created = qint32();
	auto decision = quint32();
	auto operationId = QByteArray();
	stream
		>> sessionId
		>> msgId
		>> result.requestId
		>> result.traceId
		>> expires
		>> created
		>> result.address
		>> result.publicKey
		>> decision
		>> result.answer
		>> result.notSent
		>> operationId
		>> result.signedBoc;
	if (!stream.ok()
		|| (decision != quint32(TonConnectClaimDecision::Confirm)
			&& decision != quint32(TonConnectClaimDecision::Answer))) {
		return std::nullopt;
	}
	result.sessionId = sessionId;
	result.msgId = msgId;
	result.expires = expires;
	result.created = created;
	result.decision = TonConnectClaimDecision(decision);
	result.operationId = operationId.toStdString();
	if (!ValidRecord(result)) {
		return std::nullopt;
	}
	return result;
}

void WriteRecord(
		Serialize::ByteArrayWriter &stream,
		const TonConnectClaimRecord &record) {
	stream
		<< quint64(record.sessionId)
		<< qint32(record.msgId)
		<< record.requestId
		<< record.traceId
		<< qint32(record.expires)
		<< qint32(record.created)
		<< record.address
		<< record.publicKey
		<< quint32(record.decision)
		<< record.answer
		<< record.notSent
		<< QByteArray::fromStdString(record.operationId)
		<< record.signedBoc;
}

} // namespace

bool TonConnectClaimMatches(
		const TonConnectClaimRecord &record,
		TonConnectSessionId sessionId,
		int32 msgId) {
	return (record.sessionId == sessionId) && (record.msgId == msgId);
}

std::optional<TonConnectClaimStore> ReadTonConnectClaims(
		Storage::Account &local) {
	using State = Storage::WalletEngineValue::State;
	const auto value = local.readWalletEngineValue(kStorageKey);
	if (value.state == State::Absent) {
		return TonConnectClaimStore();
	} else if (value.state != State::Read
		|| value.bytes.size() > kTonConnectClaimMaxBytes) {
		return std::nullopt;
	}
	auto stream = Serialize::ByteArrayReader(value.bytes);
	auto version = quint32();
	auto count = quint32();
	stream >> version >> count;
	if (!stream.ok()
		|| !version
		|| version > kFormatVersion
		|| count > kTonConnectClaimMaxRecords) {
		return std::nullopt;
	}
	auto result = TonConnectClaimStore();
	for (auto i = quint32(0); i != count; ++i) {
		auto record = ReadRecord(stream);
		if (!record || ranges::any_of(result.records, [&](const auto &other) {
				return SameKey(other, *record);
			})) {
			return std::nullopt;
		}
		result.records.push_back(std::move(*record));
	}
	return (stream.ok() && stream.atEnd())
		? std::make_optional(std::move(result))
		: std::nullopt;
}

bool WriteTonConnectClaims(
		Storage::Account &local,
		const TonConnectClaimStore &store) {
	if (!ValidStore(store)) {
		return false;
	}
	auto stream = Serialize::ByteArrayWriter();
	stream << kFormatVersion << quint32(store.records.size());
	for (const auto &record : store.records) {
		WriteRecord(stream, record);
	}
	auto bytes = std::move(stream).result();
	if (bytes.size() > kTonConnectClaimMaxBytes) {
		return false;
	}
	return local.writeWalletEngineValue(kStorageKey, std::move(bytes));
}

} // namespace Wallet
