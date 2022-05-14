/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_peer_bot_command.h"

#include "data/data_user.h"

namespace Data {

bool UpdateBotCommands(
		std::vector<Data::BotCommand> &commands,
		const MTPVector<MTPBotCommand> *data) {
	if (!data) {
		const auto changed = !commands.empty();
		commands.clear();
		return changed;
	}
	const auto &v = data->v;
	commands.reserve(v.size());
	auto result = false;
	auto index = 0;
	for (const auto &command : v) {
		command.match([&](const MTPDbotCommand &data) {
			const auto command = qs(data.vcommand());
			const auto description = qs(data.vdescription());
			if (commands.size() <= index) {
				commands.push_back({
					.command = command,
					.description = description,
				});
				result = true;
			} else {
				auto &entry = commands[index];
				if (entry.command != command
					|| entry.description != description) {
					entry.command = command;
					entry.description = description;
					result = true;
				}
			}
			++index;
		});
	}
	if (index < commands.size()) {
		result = true;
	}
	commands.resize(index);
	return result;
}

bool UpdateBotCommands(
		base::flat_map<UserId, std::vector<Data::BotCommand>> &commands,
		UserId botId,
		const MTPVector<MTPBotCommand> *data) {
	return (!data || data->v.isEmpty())
		? commands.remove(botId)
		: UpdateBotCommands(commands[botId], data);
}

bool UpdateBotCommands(
		base::flat_map<UserId, std::vector<Data::BotCommand>> &commands,
		const MTPVector<MTPBotInfo> &data) {
	auto result = false;
	auto filled = base::flat_set<UserId>();
	filled.reserve(data.v.size());
	for (const auto &item : data.v) {
		item.match([&](const MTPDbotInfo &data) {
			if (!data.vuser_id()) {
				LOG(("API Error: BotInfo without UserId for commands map."));
				return;
			}
			const auto id = UserId(*data.vuser_id());
			if (!filled.emplace(id).second) {
				LOG(("API Error: Two BotInfo for a single bot."));
				return;
			} else if (UpdateBotCommands(commands, id, data.vcommands())) {
				result = true;
			}
		});
	}
	for (auto i = begin(commands); i != end(commands);) {
		if (filled.contains(i->first)) {
			++i;
		} else {
			i = commands.erase(i);
			result = true;
		}
	}
	return result;
}

} // namespace Data
