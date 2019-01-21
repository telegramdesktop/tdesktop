/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Settings {

class PrivacySecurity : public Section {
public:
	PrivacySecurity(QWidget *parent, not_null<UserData*> self);

private:
	void setupContent();

	not_null<UserData*> _self;

};

} // namespace Settings
