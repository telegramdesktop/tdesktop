/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "settings/settings_privacy_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "boxes/sessionsbox.h"

namespace Settings {

PrivacyWidget::PrivacyWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_privacy)) {
	refreshControls();
}

void PrivacyWidget::refreshControls() {
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins marginSkip(0, 0, 0, st::settingsSkip);
	style::margins slidedPadding(0, marginSmall.bottom() / 2, 0, marginSmall.bottom() - (marginSmall.bottom() / 2));

	addChildRow(_editPasscode, marginSmall, lang(lng_passcode_turn_on), SLOT(onEditPasscode()));
	addChildRow(_editPassword, marginSmall, lang(lng_cloud_password_set), SLOT(onEditPassword()));
	addChildRow(_showAllSessions, marginSmall, lang(lng_settings_show_sessions), SLOT(onShowSessions()));
}

void PrivacyWidget::onEditPasscode() {

}

void PrivacyWidget::onEditPassword() {

}

void PrivacyWidget::onShowSessions() {
	Ui::showLayer(new SessionsBox());
}

} // namespace Settings
