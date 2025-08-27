/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct BotCommand final {
	QString command;
	QString description;

	friend inline bool operator==(
		const BotCommand &,
		const BotCommand &) = default;
};

[[nodiscard]] BotCommand BotCommandFromTL(const MTPBotCommand &result);

} // namespace Data
