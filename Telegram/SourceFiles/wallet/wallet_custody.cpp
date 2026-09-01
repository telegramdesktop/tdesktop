/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_custody.h"

#include "storage/serialize_common.h"
#include "storage/storage_account.h"

namespace Wallet {
namespace {

const auto kCustodyStorageKey = u"custody/records"_q;
constexpr auto kCustodyFormatVersion = quint32(2);
constexpr auto kActiveFlag = quint32(1U << 0);

[[nodiscard]] std::optional<CustodyRecord> ReadRecord(
		Serialize::ByteArrayReader &stream) {
	auto result = CustodyRecord();
	auto network = qint32();
	auto flags = quint32();
	stream
		>> result.recordId
		>> result.address
		>> result.publicKey
		>> network
		>> result.secretRef
		>> flags;
	if (!stream.ok()
		|| result.recordId.isEmpty()
		|| result.secretRef.isEmpty()
		|| result.publicKey.size() != kCustodyPublicKeySize
		|| (network != 1 && network != 2)) {
		return std::nullopt;
	}
	result.network = network;
	result.active = ((flags & kActiveFlag) == kActiveFlag);
	return result;
}

void WriteRecord(
		Serialize::ByteArrayWriter &stream,
		const CustodyRecord &record) {
	stream
		<< record.recordId
		<< record.address
		<< record.publicKey
		<< qint32(record.network)
		<< record.secretRef
		<< quint32(record.active ? kActiveFlag : 0);
}

} // namespace

const CustodyRecord *CustodyStore::matching(
		const QByteArray &publicKey) const {
	const auto i = ranges::find(records, publicKey, &CustodyRecord::publicKey);
	return (i != end(records)) ? &*i : nullptr;
}

std::optional<CustodyStore> ReadCustodyStore(Storage::Account &local) {
	using State = Storage::WalletEngineValue::State;
	const auto value = local.readWalletEngineValue(kCustodyStorageKey);
	if (value.state == State::Absent) {
		return CustodyStore();
	} else if (value.state == State::Broken) {
		return std::nullopt;
	}
	auto stream = Serialize::ByteArrayReader(value.bytes);
	auto version = quint32();
	auto count = quint32();
	stream >> version >> count;
	if (!stream.ok() || (version > kCustodyFormatVersion)) {
		return std::nullopt;
	}
	auto result = CustodyStore();
	for (auto i = quint32(0); i != count; ++i) {
		auto record = ReadRecord(stream);
		if (!record) {
			return std::nullopt;
		}
		result.records.push_back(std::move(*record));
	}
	if (version >= 2) {
		stream >> result.lastSeenServerKey;
		if (!stream.ok()) {
			return std::nullopt;
		}
		if (!result.lastSeenServerKey.isEmpty()
			&& result.lastSeenServerKey.size() != kCustodyPublicKeySize) {
			result.lastSeenServerKey = QByteArray();
		}
	}
	return result;
}

bool WriteCustodyStore(Storage::Account &local, const CustodyStore &store) {
	auto stream = Serialize::ByteArrayWriter();
	stream << kCustodyFormatVersion << quint32(store.records.size());
	for (const auto &record : store.records) {
		WriteRecord(stream, record);
	}
	stream << store.lastSeenServerKey;
	return local.writeWalletEngineValue(
		kCustodyStorageKey,
		std::move(stream).result());
}

} // namespace Wallet
