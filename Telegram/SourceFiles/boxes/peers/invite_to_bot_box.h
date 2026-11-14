/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Main {
class SessionShow;
} // namespace Main

[[nodiscard]] bool CanInviteToBot(not_null<UserData*> bot);

void ShowInviteToBotBox(
	std::shared_ptr<Main::SessionShow> show,
	not_null<UserData*> bot);
