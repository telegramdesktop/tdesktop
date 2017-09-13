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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "settings/settings_block_widget.h"

namespace Settings {

class NotificationsWidget : public BlockWidget {
	Q_OBJECT

public:
	NotificationsWidget(QWidget *parent, UserData *self);

private slots:
	void onDesktopNotifications();
	void onShowSenderName();
	void onShowMessagePreview();
	void onNativeNotifications();
	void onPlaySound();
	void onIncludeMuted();
	void onAdvanced();

private:
	void createControls();
	void createNotificationsControls();
	void desktopEnabledUpdated();
	void viewParamUpdated();

	Ui::Checkbox *_desktopNotifications = nullptr;
	Ui::SlideWrap<Ui::Checkbox> *_showSenderName = nullptr;
	Ui::SlideWrap<Ui::Checkbox> *_showMessagePreview = nullptr;
	Ui::Checkbox *_nativeNotifications = nullptr;
	Ui::Checkbox *_playSound = nullptr;
	Ui::Checkbox *_includeMuted = nullptr;
	Ui::SlideWrap<Ui::LinkButton> *_advanced = nullptr;

};

} // namespace Settings
