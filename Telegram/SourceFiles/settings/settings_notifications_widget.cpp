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

	subscribe(Global::RefNotifySettingsChanged(), [this](const Notify::ChangeType &type) {
		if (type == Notify::ChangeType::DesktopEnabled) {
			desktopEnabledUpdated();
		} else if (type == Notify::ChangeType::ViewParams) {
			viewParamUpdated();
		} else if (type == Notify::ChangeType::SoundEnabled) {
			_playSound->setChecked(Global::SoundNotify());
		}
	});
}

void NotificationsWidget::createControls() {
	style::margins margin(0, 0, 0, st::settingsSkip);
	style::margins slidedPadding(0, margin.bottom() / 2, 0, margin.bottom() - (margin.bottom() / 2));
	addChildRow(_desktopNotifications, margin, lang(lng_settings_desktop_notify), SLOT(onDesktopNotifications()), Global::DesktopNotify());
	addChildRow(_showSenderName, margin, slidedPadding, lang(lng_settings_show_name), SLOT(onShowSenderName()), Global::NotifyView() <= dbinvShowName);
	addChildRow(_showMessagePreview, margin, slidedPadding, lang(lng_settings_show_preview), SLOT(onShowMessagePreview()), Global::NotifyView() <= dbinvShowPreview);
	if (!_showSenderName->entity()->checked()) {
		_showMessagePreview->hideFast();
	}
	if (!_desktopNotifications->checked()) {
		_showSenderName->hideFast();
		_showMessagePreview->hideFast();
	}
#ifdef Q_OS_WIN
	if (App::wnd()->psHasNativeNotifications()) {
		addChildRow(_windowsNative, margin, lang(lng_settings_use_windows), SLOT(onWindowsNative()), Global::WindowsNotifications());
	}
#endif // Q_OS_WIN
	addChildRow(_playSound, margin, lang(lng_settings_sound_notify), SLOT(onPlaySound()), Global::SoundNotify());
	addChildRow(_includeMuted, margin, lang(lng_settings_include_muted), SLOT(onIncludeMuted()), Global::IncludeMuted());
}

void NotificationsWidget::onDesktopNotifications() {
	if (Global::DesktopNotify() == _desktopNotifications->checked()) {
		return;
	}
	Global::SetDesktopNotify(_desktopNotifications->checked());
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::DesktopEnabled);
}

void NotificationsWidget::desktopEnabledUpdated() {
	_desktopNotifications->setChecked(Global::DesktopNotify());
	if (Global::DesktopNotify()) {
		_showSenderName->slideDown();
		if (_showSenderName->entity()->checked()) {
			_showMessagePreview->slideDown();
		}
	} else {
		_showSenderName->slideUp();
		_showMessagePreview->slideUp();
	}
}

void NotificationsWidget::onShowSenderName() {
	auto viewParam = ([this]() {
		if (!_showSenderName->entity()->checked()) {
			return dbinvShowNothing;
		} else if (!_showMessagePreview->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowPreview;
	})();
	if (viewParam == Global::NotifyView()) {
		return;
	}
	Global::SetNotifyView(viewParam);
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::ViewParams);
}

void NotificationsWidget::onShowMessagePreview() {
	auto viewParam = ([this]() {
		if (_showMessagePreview->entity()->checked()) {
			return dbinvShowPreview;
		} else if (_showSenderName->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowNothing;
	})();
	if (viewParam == Global::NotifyView()) {
		return;
	}

	Global::SetNotifyView(viewParam);
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::ViewParams);
}

void NotificationsWidget::viewParamUpdated() {
	if (_showSenderName->entity()->checked()) {
		_showMessagePreview->slideDown();
	} else {
		_showMessagePreview->slideUp();
	}
}

void NotificationsWidget::onWindowsNative() {
#ifdef Q_OS_WIN
	if (Global::WindowsNotifications() == _windowsNative->checked()) {
		return;
	}

	Global::SetWindowsNotifications(_windowsNative->checked());
	Global::SetCustomNotifies(!Global::WindowsNotifications());
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::UseNative);
#endif // Q_OS_WIN
}

void NotificationsWidget::onPlaySound() {
	if (_playSound->checked() == Global::SoundNotify()) {
		return;
	}

	Global::SetSoundNotify(_playSound->checked());
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::SoundEnabled);
}

void NotificationsWidget::onIncludeMuted() {
	Global::SetIncludeMuted(_includeMuted->checked());
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::IncludeMuted);
}

} // namespace Settings
