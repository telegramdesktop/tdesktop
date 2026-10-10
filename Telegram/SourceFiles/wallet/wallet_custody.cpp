/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_custody.h"

#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "wallet/wallet_address.h"

namespace Wallet {
namespace {

const auto kCustodyStorageKey = u"custody/records"_q;
constexpr auto kCustodyFormatVersion = quint32(4);
constexpr auto kActiveFlag = quint32(1U << 0);
constexpr auto kRotatedSinceBackupFlag = quint32(1U << 1);
constexpr auto kAwaitingServerKeyFlag = quint32(1U << 2);
constexpr auto kPendingRotationFlag = quint32(1U << 0);

[[nodiscard]] std::optional<CustodyRecord> ReadRecord(
		Serialize::ByteArrayReader &stream,
		quint32 version) {
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
	if (version >= 4) {
		stream >> result.signingKey;
		if (!stream.ok()) {
			return std::nullopt;
		}
	}
	if (!result.signingKey.isEmpty()
		&& result.signingKey.size() != kCustodyPublicKeySize) {
		result.signingKey = QByteArray();
	}
	result.network = network;
	result.active = ((flags & kActiveFlag) == kActiveFlag);
	result.rotatedSinceBackup = ((flags & kRotatedSinceBackupFlag)
		== kRotatedSinceBackupFlag);
	result.awaitingServerKey = ((flags & kAwaitingServerKeyFlag)
		== kAwaitingServerKeyFlag);
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
		<< quint32((record.active ? kActiveFlag : 0)
			| (record.rotatedSinceBackup ? kRotatedSinceBackupFlag : 0)
			| (record.awaitingServerKey ? kAwaitingServerKeyFlag : 0))
		<< record.signingKey;
}

[[nodiscard]] std::optional<PendingRotation> ReadPendingRotation(
		Serialize::ByteArrayReader &stream,
		quint32 version) {
	auto result = PendingRotation();
	stream >> result.recordId >> result.secretRef >> result.operationId;
	if (!stream.ok()
		|| result.recordId.isEmpty()
		|| result.secretRef.isEmpty()
		|| result.operationId.isEmpty()) {
		return std::nullopt;
	}
	if (version >= 4) {
		stream >> result.newPublicKey;
		if (!stream.ok()) {
			return std::nullopt;
		}
	}
	if (!result.newPublicKey.isEmpty()
		&& result.newPublicKey.size() != kCustodyPublicKeySize) {
		result.newPublicKey = QByteArray();
	}
	return result;
}

void WritePendingRotation(
		Serialize::ByteArrayWriter &stream,
		const PendingRotation &pending) {
	stream
		<< pending.recordId
		<< pending.secretRef
		<< pending.operationId
		<< pending.newPublicKey;
}

} // namespace

bool CustodyRecord::signsWith(const QByteArray &servedKey) const {
	return signingKey.isEmpty()
		? (publicKey == servedKey)
		: (signingKey == servedKey || awaitingServerKey);
}

bool CustodyRecord::unresolved(const QByteArray &servedKey) const {
	return signingKey.isEmpty() && (publicKey != servedKey);
}

const CustodyRecord *CustodyStore::byAnchor(
		const QByteArray &publicKey) const {
	const auto i = ranges::find(records, publicKey, &CustodyRecord::publicKey);
	return (i != end(records)) ? &*i : nullptr;
}

const CustodyRecord *CustodyStore::forAddress(
		const QString &canonicalAddress) const {
	if (canonicalAddress.isEmpty()) {
		return nullptr;
	}
	const auto i = ranges::find_if(records, [&](const CustodyRecord &record) {
		return (CanonicalAddress(record.address) == canonicalAddress);
	});
	return (i != end(records)) ? &*i : nullptr;
}

const CustodyRecord *CustodyStore::current(
		const QString &canonicalAddress,
		const QByteArray &servedKey) const {
	if (canonicalAddress.isEmpty()) {
		return nullptr;
	}
	const auto i = ranges::find_if(records, [&](const CustodyRecord &record) {
		return (CanonicalAddress(record.address) == canonicalAddress)
			&& record.signsWith(servedKey);
	});
	return (i != end(records)) ? &*i : nullptr;
}

bool CustodyStore::anyAwaitingServerKey() const {
	return ranges::any_of(records, &CustodyRecord::awaitingServerKey);
}

void ForEachCustodySecretRef(
		CustodyStore &store,
		Fn<bool(const QString &secretRef)> drop) {
	auto &records = store.records;
	const auto detached = ranges::remove_if(records, [&](
			const CustodyRecord &record) {
		return !record.secretRef.isEmpty() && drop(record.secretRef);
	});
	records.erase(detached, end(records));
	auto &pending = store.pendingRotation;
	if (pending
		&& !pending->secretRef.isEmpty()
		&& drop(pending->secretRef)) {
		pending = std::nullopt;
	}
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
		auto record = ReadRecord(stream, version);
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
	if (version >= 3) {
		auto storeFlags = quint32();
		stream >> storeFlags;
		if (!stream.ok()) {
			return std::nullopt;
		}
		if ((storeFlags & kPendingRotationFlag) == kPendingRotationFlag) {
			auto pending = ReadPendingRotation(stream, version);
			if (!pending) {
				return std::nullopt;
			}
			result.pendingRotation = std::move(*pending);
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
	const auto &pending = store.pendingRotation;
	stream << quint32(pending ? kPendingRotationFlag : 0);
	if (pending) {
		WritePendingRotation(stream, *pending);
	}
	return local.writeWalletEngineValue(
		kCustodyStorageKey,
		std::move(stream).result());
}

} // namespace Wallet
