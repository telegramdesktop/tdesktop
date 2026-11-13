/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_bot_invite.h"

#include "data/data_user.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"

PreparedServiceText PrepareBotInviteText(
		not_null<UserData*> inviter,
		bool fromBot,
		TextWithEntities fromLinkText,
		ClickHandlerPtr fromLink) {
	auto result = PreparedServiceText();
	if (fromBot) {
		result.links.push_back(inviter->createOpenLink());
		result.text = tr::lng_action_bot_invited_you(
			tr::now,
			lt_user,
			tr::link(inviter->name(), 1),
			tr::marked);
	} else {
		result.links.push_back(std::move(fromLink));
		result.links.push_back(inviter->createOpenLink());
		result.text = tr::lng_action_bot_invited_user(
			tr::now,
			lt_from,
			std::move(fromLinkText),
			lt_user,
			tr::link(inviter->name(), 2),
			tr::marked);
	}
	return result;
}
