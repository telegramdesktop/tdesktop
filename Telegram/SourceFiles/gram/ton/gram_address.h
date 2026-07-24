/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Gram {

struct Address {
	qint32 workchain = 0;
	QByteArray hash;

	friend bool operator==(const Address &, const Address &) = default;
};

struct ParsedAddress {
	Address address;
	bool friendly = false;
	bool bounceable = true;
	bool testnet = false;
};

[[nodiscard]] std::optional<ParsedAddress> ParseAddress(const QString &text);
[[nodiscard]] QString FormatRaw(const Address &address);
[[nodiscard]] QString FormatFriendly(
	const Address &address,
	bool bounceable,
	bool testnet = false);

} // namespace Gram
