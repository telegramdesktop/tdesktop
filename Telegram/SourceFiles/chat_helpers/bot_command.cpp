/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/bot_command.h"

#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "history/history_item.h"

namespace Bot {

QString WrapCommandInChat(
		not_null<PeerData*> peer,
		const QString &command,
		const FullMsgId &context) {
	auto result = command;
	if (const auto item = peer->owner().message(context)) {
		if (const auto user = item->fromOriginal()->asUser()) {
			return WrapCommandInChat(peer, command, user);
		}
	}
	return result;
}

QString WrapCommandInChat(
		not_null<PeerData*> peer,
		const QString &command,
		not_null<UserData*> bot) {
	if (!bot->isBot() || bot->username.isEmpty()) {
		return command;
	}
	const auto botStatus = peer->isChat()
		? peer->asChat()->botStatus
		: peer->isMegagroup()
		? peer->asChannel()->mgInfo->botStatus
		: -1;
	return ((command.indexOf('@') < 2) && (botStatus == 0 || botStatus == 2))
		? command + '@' + bot->username
		: command;
}

} // namespace Bot
