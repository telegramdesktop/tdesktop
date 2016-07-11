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
#include "ui/buttons/icon_button.h"

namespace Ui {

IconButton::IconButton(QWidget *parent, const style::IconButton &st) : Button(parent)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
}

void IconButton::setIcon(const style::icon *icon) {
	_iconOverride = icon;
	update();
}

void IconButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto over = _a_over.current(getms(), (_state & StateOver) ? 1. : 0.);
	p.setOpacity(over * _st.overOpacity + (1. - over) * _st.opacity);

	auto position = (_state & StateDown) ? _st.downIconPosition : _st.iconPosition;
	(_iconOverride ? _iconOverride : &_st.icon)->paint(p, position, width());
}

void IconButton::onStateChanged(int oldState, ButtonStateChangeSource source) {
	auto over = (_state & StateOver);
	if (over != (oldState & StateOver)) {
		auto from = over ? 0. : 1.;
		auto to = over ? 1. : 0.;
		START_ANIMATION(_a_over, func(this, &IconButton::updateCallback), from, to, _st.duration, anim::linear);
	}
}

} // namespace Ui
