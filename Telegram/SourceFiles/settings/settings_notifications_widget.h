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
	void onWindowsNative();
	void onPlaySound();
	void onIncludeMuted();

private:
	void createControls();
	void desktopEnabledUpdated();
	void viewParamUpdated();

	ChildWidget<Checkbox> _desktopNotifications = { nullptr };
	ChildWidget<Ui::WidgetSlideWrap<Checkbox>> _showSenderName = { nullptr };
	ChildWidget<Ui::WidgetSlideWrap<Checkbox>> _showMessagePreview = { nullptr };
	ChildWidget<Checkbox> _windowsNative = { nullptr };
	ChildWidget<Checkbox> _playSound = { nullptr };
	ChildWidget<Checkbox> _includeMuted = { nullptr };

};

} // namespace Settings
