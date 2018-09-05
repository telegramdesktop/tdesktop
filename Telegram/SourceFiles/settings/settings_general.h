/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Settings {

class General : public Section {
public:
	explicit General(QWidget *parent, UserData *self = nullptr);

private:
	void setupContent();

	UserData *_self = nullptr;

};

} // namespace Settings
