/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_peer_bot_commands.h"

namespace Data {

ChatBotCommands::Changed ChatBotCommands::update(
		const std::vector<BotCommands> &list) {
	auto changed = false;
	if (list.empty()) {
		changed = (list.empty() != empty());
		clear();
	} else {
		for (const auto &commands : list) {
			auto &value = operator[](commands.userId);
			changed |= commands.commands.empty()
				? remove(commands.userId)
				: !ranges::equal(value, commands.commands);
			value = commands.commands;
		}
	}
	return changed;
}

BotCommands BotCommandsFromTL(const MTPBotInfo &result) {
	return result.match([](const MTPDbotInfo &data) {
		auto commands = ranges::views::all(
			data.vcommands().v
		) | ranges::views::transform(BotCommandFromTL) | ranges::to_vector;
		return BotCommands{
			.userId = UserId(data.vuser_id().v),
			.commands = std::move(commands),
		};
	});
}

} // namespace Data
