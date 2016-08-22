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
#include "settings/settings_notifications_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "localstorage.h"
#include "ui/widgets/widget_slide_wrap.h"
#include "ui/flatcheckbox.h"
#include "mainwindow.h"

namespace Settings {

NotificationsWidget::NotificationsWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_notify)) {
	createControls();
}

void NotificationsWidget::createControls() {
	style::margins margin(0, 0, 0, st::settingsSmallSkip);
	style::margins slidedPadding(0, margin.bottom() / 2, 0, margin.bottom() - (margin.bottom() / 2));
	addChildRow(_desktopNotifications, margin, lang(lng_settings_desktop_notify), SLOT(onDesktopNotifications()), cDesktopNotify());
	addChildRow(_showSenderName, margin, slidedPadding, lang(lng_settings_show_name), SLOT(onShowSenderName()), cNotifyView() <= dbinvShowName);
	addChildRow(_showMessagePreview, margin, slidedPadding, lang(lng_settings_show_preview), SLOT(onShowMessagePreview()), cNotifyView() <= dbinvShowPreview);
	if (!_showSenderName->entity()->checked()) {
		_showMessagePreview->hideFast();
	}
	if (!_desktopNotifications->checked()) {
		_showSenderName->hideFast();
		_showMessagePreview->hideFast();
	}
#ifdef Q_OS_WIN
	if (App::wnd()->psHasNativeNotifications()) {
		addChildRow(_windowsNative, margin, lang(lng_settings_use_windows), SLOT(onWindowsNative()), cWindowsNotifications());
	}
#endif // Q_OS_WIN
	addChildRow(_playSound, margin, lang(lng_settings_sound_notify), SLOT(onPlaySound()), cSoundNotify());
	addChildRow(_includeMuted, margin, lang(lng_settings_include_muted), SLOT(onIncludeMuted()), cIncludeMuted());
}

void NotificationsWidget::onDesktopNotifications() {
	cSetDesktopNotify(_desktopNotifications->checked());
	Local::writeUserSettings();
	if (App::wnd()) App::wnd()->updateTrayMenu();

	if (_desktopNotifications->checked()) {
		_showSenderName->slideDown();
		if (_showSenderName->entity()->checked()) {
			_showMessagePreview->slideDown();
		}
	} else {
		App::wnd()->notifyClear();
		_showSenderName->slideUp();
		_showMessagePreview->slideUp();
	}
}

void NotificationsWidget::onShowSenderName() {
	if (_showSenderName->entity()->checked()) {
		_showMessagePreview->slideDown();
	} else {
		_showMessagePreview->slideUp();
	}

	if (!_showSenderName->entity()->checked()) {
		cSetNotifyView(dbinvShowNothing);
	} else if (!_showMessagePreview->entity()->checked()) {
		cSetNotifyView(dbinvShowName);
	} else {
		cSetNotifyView(dbinvShowPreview);
	}
	Local::writeUserSettings();
	App::wnd()->notifyUpdateAll();
}

void NotificationsWidget::onShowMessagePreview() {
	if (_showMessagePreview->entity()->checked()) {
		cSetNotifyView(dbinvShowPreview);
	} else if (_showSenderName->entity()->checked()) {
		cSetNotifyView(dbinvShowName);
	} else {
		cSetNotifyView(dbinvShowNothing);
	}
	Local::writeUserSettings();
	App::wnd()->notifyUpdateAll();
}

#ifdef Q_OS_WIN
void NotificationsWidget::onWindowsNative() {
	if (cPlatform() != dbipWindows) return;
	cSetWindowsNotifications(!cWindowsNotifications());
	App::wnd()->notifyClearFast();
	cSetCustomNotifies(!cWindowsNotifications());
	Local::writeUserSettings();
}
#endif // Q_OS_WIN

void NotificationsWidget::onPlaySound() {
	cSetSoundNotify(_playSound->checked());
	Local::writeUserSettings();
}

void NotificationsWidget::onIncludeMuted() {
	cSetIncludeMuted(_includeMuted->checked());
	Notify::unreadCounterUpdated();
	Local::writeUserSettings();
}

} // namespace Settings
