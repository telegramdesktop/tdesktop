/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Data {

struct BotCommand final {
	QString command;
	QString description;

	inline bool operator==(const BotCommand &other) const {
		return (command == other.command)
			&& (description == other.description);
	}
	inline bool operator!=(const BotCommand &other) const {
		return !(*this == other);
	}
};

[[nodiscard]] BotCommand BotCommandFromTL(const MTPBotCommand &result);

} // namespace Data
