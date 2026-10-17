/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_transfer_store.h"

#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_custody.h"

#include <QtCore/QIODevice>

namespace Wallet {
namespace {

const auto kStorageKey = u"presentation/submitted-transfers"_q;
constexpr auto kFormatVersion = quint32(3);
constexpr auto kIdentityMaxBytes = 256;
constexpr auto kAddressMaxBytes = 128;
constexpr auto kServerIdMaxBytes = 1024;
constexpr auto kDomainMaxBytes = 1024;
constexpr auto kServerCommentMaxBytes = 64 * 1024;
constexpr auto kHashSize = 32;
constexpr auto kHandoffFlag = quint32(1U << 0);
constexpr auto kReceiptFlag = quint32(1U << 1);
constexpr auto kConfirmedHashFlag = quint32(1U << 2);
constexpr auto kServedFlag = quint32(1U << 3);
constexpr auto kStoppedFlag = quint32(1U << 4);
constexpr auto kPairedFlag = quint32(1U << 5);
constexpr auto kBounceFlag = quint32(1U << 6);
constexpr auto kRecordFlags = kHandoffFlag | kReceiptFlag
	| kConfirmedHashFlag | kServedFlag | kStoppedFlag | kPairedFlag
	| kBounceFlag;
constexpr auto kDateFlag = quint32(1U << 0);
constexpr auto kFeeFlag = quint32(1U << 1);
constexpr auto kPeerFlag = quint32(1U << 2);
constexpr auto kFailedFlag = quint32(1U << 3);
constexpr auto kPrivateFlag = quint32(1U << 4);
constexpr auto kGaslessFlag = quint32(1U << 5);
constexpr auto kBounceableFlag = quint32(1U << 6);
constexpr auto kProjectionFlags = kDateFlag | kFeeFlag
	| kPeerFlag | kFailedFlag | kPrivateFlag | kGaslessFlag
	| kBounceableFlag;

[[nodiscard]] QByteArray ReadBytes(
		Serialize::ByteArrayReader &stream,
		int limit) {
	auto size = quint32();
	stream >> size;
	auto &raw = stream.underlying();
	if (!stream.ok()
		|| size > limit
		|| size > raw.device()->bytesAvailable()) {
		raw.setStatus(QDataStream::ReadCorruptData);
		return QByteArray();
	}
	auto result = QByteArray(int(size), Qt::Uninitialized);
	if (raw.readRawData(result.data(), int(size)) != int(size)) {
		raw.setStatus(QDataStream::ReadPastEnd);
		return QByteArray();
	}
	return result;
}

[[nodiscard]] QString ReadText(
		Serialize::ByteArrayReader &stream,
		int limit) {
	const auto bytes = ReadBytes(stream, limit);
	auto result = QString::fromUtf8(bytes);
	if (result.toUtf8() != bytes) {
		stream.underlying().setStatus(QDataStream::ReadCorruptData);
		return QString();
	}
	return result;
}

void WriteBytes(
		Serialize::ByteArrayWriter &stream,
		const QByteArray &bytes) {
	auto &raw = stream.underlying();
	if (raw.status() != QDataStream::Ok) {
		return;
	} else if (bytes.size() > kSubmittedTransferMaxBytes
		|| raw.device()->pos() + sizeof(quint32) + bytes.size()
			> kSubmittedTransferMaxBytes) {
		raw.setStatus(QDataStream::WriteFailed);
		return;
	}
	stream << quint32(bytes.size());
	if (raw.writeRawData(bytes.constData(), bytes.size()) != bytes.size()) {
		raw.setStatus(QDataStream::WriteFailed);
	}
}

void WriteText(Serialize::ByteArrayWriter &stream, const QString &text) {
	WriteBytes(stream, text.toUtf8());
}

[[nodiscard]] bool ValidText(const QString &text, int limit) {
	if (text.size() > limit) {
		return false;
	}
	const auto bytes = text.toUtf8();
	return bytes.size() <= limit && QString::fromUtf8(bytes) == text;
}

[[nodiscard]] bool ValidAddress(const QString &address) {
	return !address.isEmpty()
		&& ValidText(address, kAddressMaxBytes)
		&& CanonicalAddress(address) == address;
}

[[nodiscard]] bool ValidProjection(const SubmittedTransferProjection &item) {
	return !item.id.isEmpty()
		&& ValidText(item.id, kServerIdMaxBytes)
		&& (item.counterparty.isEmpty() || ValidAddress(item.counterparty))
		&& ValidText(item.counterpartyName, kDomainMaxBytes)
		&& ValidText(item.comment, kServerCommentMaxBytes)
		&& (!item.commentEncrypted || item.comment.isEmpty())
		&& (item.peerTransfer
			|| !item.collectible.isEmpty()
			|| !item.counterpartyPeer)
		&& (item.collectible.isEmpty()
			|| (!item.peerTransfer && ValidAddress(item.collectible)))
		&& item.amountNano >= 0;
}

[[nodiscard]] bool ValidRecord(const SubmittedTransferRecord &record) {
	return !record.recordId.isEmpty()
		&& ValidText(record.recordId, kIdentityMaxBytes)
		&& !record.operationId.empty()
		&& record.operationId.size() <= kIdentityMaxBytes
		&& (record.network == 1 || record.network == 2)
		&& ValidAddress(record.address)
		&& record.publicKey.size() == kCustodyPublicKeySize
		&& ValidAddress(record.destination)
		&& (record.collectible.isEmpty() || ValidAddress(record.collectible))
		&& ValidText(record.comment, kSendCommentMaxBytes)
		&& record.amountNano > 0
		&& record.posted > 0
		&& (record.handoff == TransferHandoff::Preparation
			|| record.handoff == TransferHandoff::Possible)
		&& record.terminal >= TransferTerminal::None
		&& record.terminal <= TransferTerminal::Cancelled
		&& (!record.messageHash
			|| (!record.messageHash->isEmpty()
				&& record.messageHash->size() <= kSubmittedTransferTokenMaxBytes))
		&& (record.confirmedHash.isEmpty()
			|| (record.confirmedHash.size() == kHashSize
				&& record.terminal == TransferTerminal::Confirmed))
		&& record.lookupAttempts >= 0
		&& record.lookupAttempts <= kSubmittedTransferLookupMaxAttempts
		&& (!record.lookupAttempts || record.messageHash)
		&& (!record.served || ValidProjection(*record.served))
		&& (record.handoff == TransferHandoff::Possible
			|| (!record.messageHash
				&& record.terminal == TransferTerminal::None
				&& !record.served
				&& !record.lookupAttempts
				&& !record.lookupStopped));
}

[[nodiscard]] bool SameIdentity(
		const SubmittedTransferRecord &a,
		const SubmittedTransferRecord &b) {
	return a.network == b.network
		&& a.address == b.address
		&& a.publicKey == b.publicKey
		&& a.operationId == b.operationId;
}

[[nodiscard]] SubmittedTransferProjection ReadProjection(
		Serialize::ByteArrayReader &stream,
		quint32 version) {
	auto result = SubmittedTransferProjection();
	auto flags = quint32();
	stream >> flags;
	if (flags & ~kProjectionFlags) {
		stream.underlying().setStatus(QDataStream::ReadCorruptData);
		return result;
	}
	result.id = ReadText(stream, kServerIdMaxBytes);
	result.counterparty = ReadText(stream, kAddressMaxBytes);
	result.counterpartyName = ReadText(stream, kDomainMaxBytes);
	result.comment = ReadText(stream, kServerCommentMaxBytes);
	stream >> result.counterpartyPeer >> result.amountNano;
	if (flags & kFeeFlag) {
		auto fee = qint64();
		stream >> fee;
		result.feeNano = fee;
	}
	if (flags & kDateFlag) {
		auto date = qint32();
		stream >> date;
		result.date = date;
	}
	if (version >= 2) {
		result.collectible = ReadText(stream, kAddressMaxBytes);
	}
	result.peerTransfer = (flags & kPeerFlag);
	result.failed = (flags & kFailedFlag);
	result.commentEncrypted = (flags & kPrivateFlag);
	result.gasless = (flags & kGaslessFlag);
	result.counterpartyBounceable = (flags & kBounceableFlag);
	return result;
}

void WriteProjection(
		Serialize::ByteArrayWriter &stream,
		const SubmittedTransferProjection &item) {
	stream << quint32((item.date ? kDateFlag : 0)
		| (item.feeNano ? kFeeFlag : 0)
		| (item.peerTransfer ? kPeerFlag : 0)
		| (item.failed ? kFailedFlag : 0)
		| (item.commentEncrypted ? kPrivateFlag : 0)
		| (item.gasless ? kGaslessFlag : 0)
		| (item.counterpartyBounceable ? kBounceableFlag : 0));
	WriteText(stream, item.id);
	WriteText(stream, item.counterparty);
	WriteText(stream, item.counterpartyName);
	WriteText(stream, item.comment);
	stream << item.counterpartyPeer << qint64(item.amountNano);
	if (item.feeNano) {
		stream << qint64(*item.feeNano);
	}
	if (item.date) {
		stream << qint32(*item.date);
	}
	WriteText(stream, item.collectible);
}

[[nodiscard]] std::optional<SubmittedTransferRecord> ReadRecord(
		Serialize::ByteArrayReader &stream,
		quint32 version) {
	auto result = SubmittedTransferRecord();
	auto network = qint32();
	auto posted = qint32();
	auto flags = quint32();
	auto terminal = quint32();
	auto attempts = qint32();
	result.recordId = ReadText(stream, kIdentityMaxBytes);
	result.address = ReadText(stream, kAddressMaxBytes);
	result.publicKey = ReadBytes(stream, kCustodyPublicKeySize);
	result.operationId = ReadBytes(stream, kIdentityMaxBytes).toStdString();
	stream >> network >> posted >> result.amountNano >> flags >> terminal;
	if (!stream.ok()
		|| (flags & ~kRecordFlags)
		|| terminal > quint32(TransferTerminal::Cancelled)) {
		return std::nullopt;
	}
	result.network = network;
	result.posted = posted;
	result.handoff = (flags & kHandoffFlag)
		? TransferHandoff::Possible
		: TransferHandoff::Preparation;
	result.terminal = TransferTerminal(terminal);
	result.lookupStopped = (flags & kStoppedFlag);
	result.paired = (flags & kPairedFlag);
	result.bounce = (flags & kBounceFlag);
	result.destination = ReadText(stream, kAddressMaxBytes);
	result.comment = ReadText(stream, kSendCommentMaxBytes);
	if (flags & kReceiptFlag) {
		result.messageHash = ReadBytes(stream, kSubmittedTransferTokenMaxBytes);
	}
	if (flags & kConfirmedHashFlag) {
		result.confirmedHash = ReadBytes(stream, kHashSize);
		if (result.confirmedHash.size() != kHashSize) {
			return std::nullopt;
		}
	}
	stream >> attempts;
	result.lookupAttempts = attempts;
	if (flags & kServedFlag) {
		result.served = ReadProjection(stream, version);
	}
	if (version >= 2) {
		auto recipient = quint64();
		stream >> recipient;
		result.recipient = UserId(recipient);
	}
	if (version >= 3) {
		result.collectible = ReadText(stream, kAddressMaxBytes);
	}
	return (stream.ok() && ValidRecord(result))
		? std::make_optional(std::move(result))
		: std::nullopt;
}

void WriteRecord(
		Serialize::ByteArrayWriter &stream,
		const SubmittedTransferRecord &record) {
	WriteText(stream, record.recordId);
	WriteText(stream, record.address);
	WriteBytes(stream, record.publicKey);
	WriteBytes(stream, QByteArray::fromStdString(record.operationId));
	stream
		<< qint32(record.network)
		<< qint32(record.posted)
		<< qint64(record.amountNano)
		<< quint32((record.handoff == TransferHandoff::Possible
				? kHandoffFlag : 0)
			| (record.messageHash ? kReceiptFlag : 0)
			| (!record.confirmedHash.isEmpty() ? kConfirmedHashFlag : 0)
			| (record.served ? kServedFlag : 0)
			| (record.lookupStopped ? kStoppedFlag : 0)
			| (record.paired ? kPairedFlag : 0)
			| (record.bounce ? kBounceFlag : 0))
		<< quint32(record.terminal);
	WriteText(stream, record.destination);
	WriteText(stream, record.comment);
	if (record.messageHash) {
		WriteBytes(stream, *record.messageHash);
	}
	if (!record.confirmedHash.isEmpty()) {
		WriteBytes(stream, record.confirmedHash);
	}
	stream << qint32(record.lookupAttempts);
	if (record.served) {
		WriteProjection(stream, *record.served);
	}
	stream << quint64(record.recipient.bare);
	WriteText(stream, record.collectible);
}

} // namespace

std::optional<SubmittedTransferStore> ReadSubmittedTransferStore(
		Storage::Account &local) {
	using State = Storage::WalletEngineValue::State;
	const auto value = local.readWalletEngineValue(kStorageKey);
	if (value.state == State::Absent) {
		return SubmittedTransferStore();
	} else if (value.state != State::Read
		|| value.bytes.size() > kSubmittedTransferMaxBytes) {
		return std::nullopt;
	}
	auto stream = Serialize::ByteArrayReader(value.bytes);
	auto version = quint32();
	auto count = quint32();
	stream >> version >> count;
	if (!stream.ok()
		|| !version
		|| version > kFormatVersion
		|| count > kSubmittedTransferMaxRecords) {
		return std::nullopt;
	}
	auto result = SubmittedTransferStore();
	for (auto i = quint32(0); i != count; ++i) {
		auto record = ReadRecord(stream, version);
		if (!record || ranges::any_of(result.records, [&](const auto &other) {
				return SameIdentity(other, *record);
			})) {
			return std::nullopt;
		}
		result.records.push_back(std::move(*record));
	}
	return (stream.ok() && stream.atEnd())
		? std::make_optional(std::move(result))
		: std::nullopt;
}

std::optional<int64> SubmittedTransferStoreSize(
		const SubmittedTransferStore &store) {
	auto size = int64(2 * sizeof(quint32));
	for (auto i = begin(store.records); i != end(store.records); ++i) {
		if (!ValidRecord(*i)
			|| ranges::any_of(begin(store.records), i, [&](const auto &other) {
				return SameIdentity(other, *i);
			})) {
			return std::nullopt;
		}
		auto stream = Serialize::ByteArrayWriter();
		WriteRecord(stream, *i);
		if (stream.underlying().status() != QDataStream::Ok) {
			return std::nullopt;
		}
		size += stream.underlying().device()->pos();
	}
	return size;
}

bool WriteSubmittedTransferStore(
		Storage::Account &local,
		const SubmittedTransferStore &store) {
	if (store.records.size() > kSubmittedTransferMaxRecords) {
		return false;
	}
	const auto size = SubmittedTransferStoreSize(store);
	if (!size || *size > kSubmittedTransferMaxBytes) {
		return false;
	}
	auto stream = Serialize::ByteArrayWriter();
	stream << kFormatVersion << quint32(store.records.size());
	for (const auto &record : store.records) {
		WriteRecord(stream, record);
	}
	if (stream.underlying().status() != QDataStream::Ok) {
		return false;
	}
	const auto bytes = std::move(stream).result();
	return bytes.size() <= kSubmittedTransferMaxBytes
		&& local.writeWalletEngineValue(kStorageKey, bytes);
}

} // namespace Wallet
