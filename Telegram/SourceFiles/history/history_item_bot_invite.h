/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

struct PreparedServiceText;

[[nodiscard]] PreparedServiceText PrepareBotInviteText(
	not_null<UserData*> inviter,
	bool fromBot,
	TextWithEntities fromLinkText,
	ClickHandlerPtr fromLink);
