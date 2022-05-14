/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_peer_bot_command.h"

namespace Data {

BotCommand BotCommandFromTL(const MTPBotCommand &result) {
	return result.match([](const MTPDbotCommand &data) {
		return BotCommand {
			.command = qs(data.vcommand().v),
			.description = qs(data.vdescription().v),
		};
	});
}

} // namespace Data
