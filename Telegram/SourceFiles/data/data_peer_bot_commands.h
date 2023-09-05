/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "data/data_peer_bot_command.h"

namespace Data {

struct BotCommands final {
	UserId userId;
	std::vector<BotCommand> commands;
};

struct ChatBotCommands final : public base::flat_map<
	UserId,
	std::vector<BotCommand>> {
public:
	using Changed = bool;

	using base::flat_map<UserId, std::vector<BotCommand>>::flat_map;

	Changed update(const std::vector<BotCommands> &list);
};

[[nodiscard]] BotCommands BotCommandsFromTL(const MTPBotInfo &result);

} // namespace Data
