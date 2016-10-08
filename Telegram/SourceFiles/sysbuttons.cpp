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
#include "sysbuttons.h"

#include "lang.h"
#include "shortcuts.h"
#include "application.h"
#include "autoupdater.h"

SysBtn::SysBtn(QWidget *parent, const style::sysButton &st, const QString &text) : Button(parent)
, _st(st)
, a_color(_st.color->c)
, _a_color(animation(this, &SysBtn::step_color))
, _text(text) {
	int32 w = _st.size.width() + (_text.isEmpty() ? 0 : ((_st.size.width() - _st.icon.width()) / 2 + st::titleTextButton.font->width(_text)));
	resize(w, _st.size.height());
	setCursor(style::cur_default);
}

void SysBtn::setText(const QString &text) {
	_text = text;
	int32 w = _st.size.width() + (_text.isEmpty() ? 0 : ((_st.size.width() - _st.icon.width()) / 2 + st::titleTextButton.font->width(_text)));
	resize(w, _st.size.height());
}

void SysBtn::setOverLevel(float64 level) {
	_overLevel = level;
	update();
}

void SysBtn::onStateChanged(int oldState, ButtonStateChangeSource source) {
	a_color.start((_state & StateOver ? _st.overColor : _st.color)->c);

	if (source == ButtonByUser || source == ButtonByPress) {
		_a_color.stop();
		a_color.finish();
		update();
	} else {
		_a_color.start();
	}
}

void SysBtn::paintEvent(QPaintEvent *e) {
	Painter p(this);

	int x = width() - ((_st.size.width() + _st.icon.width()) / 2), y = (height() - _st.icon.height()) / 2;
	QColor c = a_color.current();
	if (_overLevel > 0) {
		if (_overLevel >= 1) {
			c = _st.overColor->c;
		} else {
			c.setRedF(c.redF() * (1 - _overLevel) + _st.overColor->c.redF() * _overLevel);
			c.setGreenF(c.greenF() * (1 - _overLevel) + _st.overColor->c.greenF() * _overLevel);
			c.setBlueF(c.blueF() * (1 - _overLevel) + _st.overColor->c.blueF() * _overLevel);
		}
	}
	p.fillRect(x, y, _st.icon.width(), _st.icon.height(), c);
	_st.icon.paint(p, x, y, width());

	if (!_text.isEmpty()) {
		p.setFont(st::titleTextButton.font->f);
		p.setPen(c);
		p.drawText((_st.size.width() - _st.icon.width()) / 2, st::titleTextButton.textTop + st::titleTextButton.font->ascent, _text);
	}
}

void SysBtn::setSysBtnStyle(const style::sysButton &st) {
	_st = st;
	update();
}

HitTestType SysBtn::hitTest(const QPoint &p) const {
	int x(p.x()), y(p.y()), w(width()), h(height());
	if (x >= 0 && y >= 0 && x < w && y < h && isVisible()) {
		return HitTestType::SysButton;
	}
	return HitTestType::None;
}

void SysBtn::step_color(float64 ms, bool timer) {
	float64 dt = ms / _st.duration;
	if (dt >= 1) {
		_a_color.stop();
		a_color.finish();
	} else {
		a_color.update(dt, anim::linear);
	}
	if (timer) update();
}

MinimizeBtn::MinimizeBtn(QWidget *parent) : SysBtn(parent, st::sysMin) {
	setClickedCallback([this]() {
		window()->setWindowState(Qt::WindowMinimized);
	});
}

MaximizeBtn::MaximizeBtn(QWidget *parent) : SysBtn(parent, st::sysMax) {
	setClickedCallback([this]() {
		window()->setWindowState(Qt::WindowMaximized);
	});
}

RestoreBtn::RestoreBtn(QWidget *parent) : SysBtn(parent, st::sysRes) {
	setClickedCallback([this]() {
		window()->setWindowState(Qt::WindowNoState);
	});
}

CloseBtn::CloseBtn(QWidget *parent) : SysBtn(parent, st::sysCls) {
	setClickedCallback([this]() {
		window()->close();
	});
}

UpdateBtn::UpdateBtn(QWidget *parent) : SysBtn(parent, st::sysUpd, lang(lng_menu_update)) {
	setClickedCallback([]() {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
		checkReadyUpdate();
		if (Sandbox::updatingState() == Application::UpdatingReady) {
			cSetRestartingUpdate(true);
		} else
#endif
		{
			cSetRestarting(true);
			cSetRestartingToSettings(false);
		}
		App::quit();
	});
}

LockBtn::LockBtn(QWidget *parent) : SysBtn(parent, st::sysLock) {
	setClickedCallback([] {
		Shortcuts::launch(qsl("lock_telegram"));
	});
}
