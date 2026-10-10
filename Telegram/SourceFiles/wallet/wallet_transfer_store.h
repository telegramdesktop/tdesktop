/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_peer_id.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <string>
#include <vector>

namespace Storage {
class Account;
} // namespace Storage

namespace Wallet {

inline constexpr auto kSendCommentMaxBytes = 960;

// How close to the limit a comment gets before the field starts counting
// down. Almost no comment is long enough to ever show the counter.
inline constexpr auto kSendCommentWarnBytes = 50;

// What a comment field accepts, in UTF-16 units. It is not the limit, which
// counts bytes and is enforced by refusing to send; it only bounds how far
// past the limit a paste can carry a comment.
inline constexpr auto kSendCommentMaxLength = 2 * kSendCommentMaxBytes;
inline constexpr auto kSubmittedTransferMaxRecords = 64;
inline constexpr auto kSubmittedTransferMaxBytes = 4 * 1024 * 1024;
inline constexpr auto kSubmittedTransferTokenMaxBytes = 1024 * 1024;
inline constexpr auto kSubmittedTransferLookupMaxAttempts = 1024;

enum class TransferHandoff {
	Preparation = 0,
	Possible = 1,
};

enum class TransferTerminal {
	None = 0,
	Confirmed = 1,
	Replaced = 2,
	SequenceNumberConsumed = 3,
	Expired = 4,
	Superseded = 5,
	Failed = 6,
	Cancelled = 7,
};

struct SubmittedTransferProjection {
	QString id;
	QString counterparty;
	QString counterpartyName;
	QString comment;
	QString collectible;
	quint64 counterpartyPeer = 0;
	int64 amountNano = 0;
	std::optional<int64> feeNano;
	std::optional<TimeId> date;
	bool peerTransfer = false;
	bool failed = false;
	bool commentEncrypted = false;
	bool gasless = false;
	bool counterpartyBounceable = false;

	friend bool operator==(
		const SubmittedTransferProjection &,
		const SubmittedTransferProjection &) = default;
};

struct SubmittedTransferRecord {
	QString recordId;
	QString address;
	QByteArray publicKey;
	std::string operationId;
	QString destination;
	QString comment;
	QString collectible;
	std::optional<QByteArray> messageHash;
	QByteArray confirmedHash;
	std::optional<SubmittedTransferProjection> served;
	int64 amountNano = 0;
	UserId recipient;
	TimeId posted = 0;
	int network = 1;
	TransferHandoff handoff = TransferHandoff::Preparation;
	TransferTerminal terminal = TransferTerminal::None;
	int lookupAttempts = 0;
	bool lookupStopped = false;
	// The send offered the server a fee-free alternative of the same
	// transfer, so the sequence number can be consumed by a message other
	// than the one the engine journaled.
	bool paired = false;
	bool bounce = false;

	friend bool operator==(
		const SubmittedTransferRecord &,
		const SubmittedTransferRecord &) = default;
};

struct SubmittedTransferStore {
	std::vector<SubmittedTransferRecord> records;
};

[[nodiscard]] std::optional<SubmittedTransferStore> ReadSubmittedTransferStore(
	Storage::Account &local);
[[nodiscard]] std::optional<int64> SubmittedTransferStoreSize(
	const SubmittedTransferStore &store);
[[nodiscard]] bool WriteSubmittedTransferStore(
	Storage::Account &local,
	const SubmittedTransferStore &store);

} // namespace Wallet
