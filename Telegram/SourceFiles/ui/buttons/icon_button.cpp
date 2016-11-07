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

void IconButton::setIcon(const style::icon *icon, const style::icon *iconOver) {
	_iconOverride = icon;
	_iconOverrideOver = iconOver;
	update();
}

void IconButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto over = _a_over.current(getms(), (_state & StateOver) ? 1. : 0.);
	auto overIcon = [this] {
		if (_iconOverrideOver) {
			return _iconOverrideOver;
		} else if (!_st.iconOver.empty()) {
			return &_st.iconOver;
		} else if (_iconOverride) {
			return _iconOverride;
		}
		return &_st.icon;
	};
	auto justIcon = [this] {
		if (_iconOverride) {
			return _iconOverride;
		}
		return &_st.icon;
	};
	auto icon = (over == 1.) ? overIcon() : justIcon();
	auto position = (_state & StateDown) ? _st.iconPositionDown : _st.iconPosition;
	if (position.x() < 0) {
		position.setX((width() - icon->width()) / 2);
	}
	icon->paint(p, position, width());
	if (over > 0. && over < 1.) {
		auto iconOver = overIcon();
		if (iconOver != icon) {
			p.setOpacity(over);
			iconOver->paint(p, position, width());
		}
	}
}

void IconButton::onStateChanged(int oldState, ButtonStateChangeSource source) {
	auto over = (_state & StateOver);
	if (over != (oldState & StateOver)) {
		if (_st.duration) {
			auto from = over ? 0. : 1.;
			auto to = over ? 1. : 0.;
			_a_over.start([this] { update(); }, from, to, _st.duration);
		} else {
			update();
		}
	}
}

MaskButton::MaskButton(QWidget *parent, const style::MaskButton &st) : Button(parent)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void MaskButton::onStateChanged(int oldState, ButtonStateChangeSource source) {
	auto over = (_state & StateOver);
	if (over != (oldState & StateOver)) {
		_a_iconOver.start([this] { update(); }, over ? 0. : 1., over ? 1. : 0., _st.duration);
	}
}

void MaskButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	auto position = (_state & StateDown) ? _st.iconPositionDown : _st.iconPosition;
	if (position.x() < 0) {
		position.setX((width() - _st.icon.width()) / 2);
	}
	if (position.y() < 0) {
		position.setY((height() - _st.icon.height()) / 2);
	}
	auto icon = myrtlrect(position.x(), position.y(), _st.icon.width(), _st.icon.height());
	if (!icon.contains(clip)) {
		p.fillRect(clip, _st.bg);
	}
	if (icon.intersects(clip)) {
		p.fillRect(icon.intersected(clip), anim::brush(_st.iconBg, _st.iconBgOver, _a_iconOver.current(getms(), (_state & StateOver) ? 1. : 0.)));
		_st.icon.paint(p, position, width());
	}
}

} // namespace Ui
