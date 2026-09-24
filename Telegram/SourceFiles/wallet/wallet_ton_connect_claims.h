/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_ton_connect.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <string>
#include <vector>

namespace Storage {
class Account;
} // namespace Storage

namespace Wallet {

enum class TonConnectClaimDecision : uchar {
	Confirm = 1,
	Answer = 2,
};

struct TonConnectClaimRecord {
	TonConnectSessionId sessionId = 0;
	int32 msgId = 0;
	QString requestId;
	QString traceId;
	TimeId expires = 0;
	TimeId created = 0;
	QString address;
	QByteArray publicKey;
	TonConnectClaimDecision decision = TonConnectClaimDecision::Answer;
	QByteArray answer;
	QByteArray notSent;
	std::string operationId;
	QString signedBoc;

	friend bool operator==(
		const TonConnectClaimRecord &,
		const TonConnectClaimRecord &) = default;
};

struct TonConnectClaimStore {
	std::vector<TonConnectClaimRecord> records;
};

inline constexpr auto kTonConnectClaimMaxRecords = 32;
inline constexpr auto kTonConnectClaimMaxBytes = 1024 * 1024;
inline constexpr auto kTonConnectClaimLifetime = TimeId(24 * 3600);

[[nodiscard]] bool TonConnectClaimMatches(
	const TonConnectClaimRecord &record,
	TonConnectSessionId sessionId,
	int32 msgId);

[[nodiscard]] std::optional<TonConnectClaimStore> ReadTonConnectClaims(
	Storage::Account &local);
[[nodiscard]] bool WriteTonConnectClaims(
	Storage::Account &local,
	const TonConnectClaimStore &store);

} // namespace Wallet
