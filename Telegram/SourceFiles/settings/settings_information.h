/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Settings {

class Information : public Section {
public:
	Information(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<UserData*> self);

private:
	void setupContent(not_null<Window::Controller*> controller);

	not_null<UserData*> _self;
	//rpl::variable<bool> _canSaveChanges;
	//Fn<void(FnMut<void()> done)> _save;

};

} // namespace Settings
