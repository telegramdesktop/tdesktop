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

void Shadow::paint(Painter &p, const QRect &box, int outerWidth, const style::Shadow &st, Sides sides) {
	auto left = (sides & Side::Left);
	auto top = (sides & Side::Top);
	auto right = (sides & Side::Right);
	auto bottom = (sides & Side::Bottom);
	if (left) {
		auto from = box.y();
		auto to = from + box.height();
		if (top && !st.topLeft.empty()) {
			st.topLeft.paint(p, box.x() - st.extend.left(), box.y() - st.extend.top(), outerWidth);
			from += st.topLeft.height() - st.extend.top();
		}
		if (bottom && !st.bottomLeft.empty()) {
			st.bottomLeft.paint(p, box.x() - st.extend.left(), box.y() + box.height() + st.extend.bottom() - st.bottomLeft.height(), outerWidth);
			to -= st.bottomLeft.height() - st.extend.bottom();
		}
		if (to > from && !st.left.empty()) {
			st.left.fill(p, rtlrect(box.x() - st.extend.left(), from, st.left.width(), to - from, outerWidth));
		}
	}
	if (right) {
		auto from = box.y();
		auto to = from + box.height();
		if (top && !st.topRight.empty()) {
			st.topRight.paint(p, box.x() + box.width() + st.extend.right() - st.topRight.width(), box.y() - st.extend.top(), outerWidth);
			from += st.topRight.height() - st.extend.top();
		}
		if (bottom && !st.bottomRight.empty()) {
			st.bottomRight.paint(p, box.x() + box.width() + st.extend.right() - st.bottomRight.width(), box.y() + box.height() + st.extend.bottom() - st.bottomRight.height(), outerWidth);
			to -= st.bottomRight.height() - st.extend.bottom();
		}
		if (to > from && !st.right.empty()) {
			st.right.fill(p, rtlrect(box.x() + box.width() + st.extend.right() - st.right.width(), from, st.right.width(), to - from, outerWidth));
		}
	}
	if (top && !st.top.empty()) {
		auto from = box.x();
		auto to = from + box.width();
		if (left && !st.topLeft.empty()) from += st.topLeft.width() - st.extend.left();
		if (right && !st.topRight.empty()) to -= st.topRight.width() - st.extend.right();
		if (to > from) {
			st.top.fill(p, rtlrect(from, box.y() - st.extend.top(), to - from, st.top.height(), outerWidth));
		}
	}
	if (bottom && !st.bottom.empty()) {
		auto from = box.x();
		auto to = from + box.width();
		if (left && !st.bottomLeft.empty()) from += st.bottomLeft.width() - st.extend.left();
		if (right && !st.bottomRight.empty()) to -= st.bottomRight.width() - st.extend.right();
		if (to > from) {
			st.bottom.fill(p, rtlrect(from, box.y() + box.height() + st.extend.bottom() - st.bottom.height(), to - from, st.bottom.height(), outerWidth));
		}
	}
}

} // namespace Ui
