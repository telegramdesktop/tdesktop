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
#include "ui/widgets/shadow.h"

namespace Ui {

void ToggleableShadow::setMode(Mode mode) {
	if (mode == Mode::ShownFast || mode == Mode::HiddenFast) {
		if (_a_opacity.animating()) {
			_a_opacity.finish();
			update();
		}
	}
	if (_shown && (mode == Mode::Hidden || mode == Mode::HiddenFast)) {
		_shown = false;
		if (mode == Mode::Hidden) {
			_a_opacity.start([this] { update(); }, 1., 0., st::shadowToggleDuration);
		}
	} else if (!_shown && (mode == Mode::Shown || mode == Mode::ShownFast)) {
		_shown = true;
		if (mode == Mode::Shown) {
			_a_opacity.start([this] { update(); }, 0., 1., st::shadowToggleDuration);
		}
	}
}

void ToggleableShadow::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (_a_opacity.animating(getms())) {
		p.setOpacity(_a_opacity.current());
	} else if (!_shown) {
		return;
	}
	p.fillRect(e->rect(), _color);
}

} // namespace Ui
