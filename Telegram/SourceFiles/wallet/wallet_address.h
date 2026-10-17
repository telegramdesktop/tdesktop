/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <optional>

namespace Wallet {

struct ParsedAddress {
	QString raw;
	bool friendly = false;
	bool bounceable = true;
	bool testnet = false;
};

[[nodiscard]] std::optional<ParsedAddress> ParseAddress(const QString &text);
[[nodiscard]] QString CanonicalAddress(const QString &text);
[[nodiscard]] QString FormatFriendly(
	const QString &raw,
	bool bounceable,
	bool testnet = false);

struct TransferLink {
	QString address;
	int64 amountNano = 0;
	QString comment;
	std::optional<uint64> expiresAt;
};

[[nodiscard]] bool TransferLinkExpired(std::optional<uint64> expiresAt);

[[nodiscard]] std::optional<TransferLink> ParseTransferLink(
	const QString &url);

} // namespace Wallet
