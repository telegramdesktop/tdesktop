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
#include "settings/settings_fixed_bar.h"

#include "styles/style_settings.h"
#include "ui/buttons/icon_button.h"
#include "mainwindow.h"
#include "lang.h"

namespace Settings {

FixedBar::FixedBar(QWidget *parent) : TWidget(parent)
, _close(this, st::settingsFixedBarClose) {
	_close->setClickedCallback([]() {
		Ui::hideSettingsAndLayer();
	});
}

int FixedBar::resizeGetHeight(int newWidth) {
	return st::settingsFixedBarHeight;
}

void FixedBar::resizeEvent(QResizeEvent *e) {
	_close->moveToRight(0, 0);
}

void FixedBar::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(st::settingsFixedBarFont);
	p.setPen(st::windowTextFg);
	p.drawTextLeft(st::settingsFixedBarTextLeft, st::settingsFixedBarTextTop, width(), lang(lng_menu_settings));
}

} // namespace Settings
