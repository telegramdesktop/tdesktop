/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

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
	quint64 counterpartyPeer = 0;
	int64 amountNano = 0;
	std::optional<int64> feeNano;
	std::optional<TimeId> date;
	bool peerTransfer = false;
	bool failed = false;
	bool commentEncrypted = false;

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
	std::optional<QByteArray> messageHash;
	QByteArray confirmedHash;
	std::optional<SubmittedTransferProjection> served;
	int64 amountNano = 0;
	TimeId posted = 0;
	int network = 1;
	TransferHandoff handoff = TransferHandoff::Preparation;
	TransferTerminal terminal = TransferTerminal::None;
	int lookupAttempts = 0;
	bool lookupStopped = false;

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
