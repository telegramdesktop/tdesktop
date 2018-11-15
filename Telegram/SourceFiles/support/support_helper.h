/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "support/support_templates.h"

class AuthSession;

namespace Window {
class Controller;
} // namespace Window

namespace Support {

class Helper {
public:
	explicit Helper(not_null<AuthSession*> session);

	void registerWindow(not_null<Window::Controller*> controller);
	void cloudDraftChanged(not_null<History*> history);

	void chatOccupiedUpdated(not_null<History*> history);

	Templates &templates();

private:
	void checkOccupiedChats();
	void updateOccupiedHistory(
		not_null<Window::Controller*> controller,
		History *history);
	void occupyInDraft();
	void reoccupy();

	not_null<AuthSession*> _session;
	Templates _templates;

	History *_occupiedHistory = nullptr;
	base::Timer _reoccupyTimer;
	base::Timer _checkOccupiedTimer;
	base::flat_map<not_null<History*>, TimeId> _occupiedChats;

	rpl::lifetime _lifetime;

};

bool IsOccupiedByMe(History *history);
bool IsOccupiedBySomeone(History *history);
QString ChatOccupiedString();

} // namespace Support
