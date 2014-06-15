/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "sysbuttons.h"
#include "window.h"
#include "application.h"

SysBtn::SysBtn(QWidget *parent, const style::sysButton &st) : Button(parent),
	_st(st), a_color(_st.color->c) {
	resize(_st.size);
	setCursor(style::cur_default);
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
}

void SysBtn::onStateChange(int oldState, ButtonStateChangeSource source) {
	a_color.start((_state & StateOver ? _st.overColor : _st.color)->c);

	if (source == ButtonByUser || source == ButtonByPress) {
		anim::stop(this);
		a_color.finish();
		update();
	} else {
		anim::start(this);
	}
}

void SysBtn::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	int x = (width() - _st.img.pxWidth()) / 2, y = (height() - _st.img.pxHeight()) / 2;
	p.fillRect(x, y, _st.img.pxWidth(), _st.img.pxHeight(), a_color.current());
	p.drawPixmap(QPoint(x, y), App::sprite(), _st.img);
}

HitTestType SysBtn::hitTest(const QPoint &p) const {
	int x(p.x()), y(p.y()), w(width()), h(height());
	if (x >= 0 && y >= 0 && x < w && y < h && isVisible()) {
		return HitTestSysButton;
	}
	return HitTestNone;
}

bool SysBtn::animStep(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_color.finish();
		res = false;
	} else {
		a_color.update(dt, anim::linear);
	}
	update();
	return res;
}

MinimizeBtn::MinimizeBtn(QWidget *parent, Window *window) : SysBtn(parent, st::sysMin), wnd(window) {
	connect(this, SIGNAL(clicked()), this, SLOT(onClick()));
}

void MinimizeBtn::onClick() {
	wnd->setWindowState(Qt::WindowMinimized);
}

MaximizeBtn::MaximizeBtn(QWidget *parent, Window *window) : SysBtn(parent, st::sysMax), wnd(window) {
	connect(this, SIGNAL(clicked()), this, SLOT(onClick()));
}

void MaximizeBtn::onClick() {
	wnd->setWindowState(Qt::WindowMaximized);
}

RestoreBtn::RestoreBtn(QWidget *parent, Window *window) : SysBtn(parent, st::sysRes), wnd(window) {
	connect(this, SIGNAL(clicked()), this, SLOT(onClick()));
}

void RestoreBtn::onClick() {
	wnd->setWindowState(Qt::WindowNoState);
}

CloseBtn::CloseBtn(QWidget *parent, Window *window) : SysBtn(parent, st::sysCls), wnd(window) {
	connect(this, SIGNAL(clicked()), this, SLOT(onClick()));
}

void CloseBtn::onClick() {
	wnd->close();
}

UpdateBtn::UpdateBtn(QWidget *parent, Window *window) : SysBtn(parent, st::sysUpd), wnd(window) {
	connect(this, SIGNAL(clicked()), this, SLOT(onClick()));
}

void UpdateBtn::onClick() {
	psCheckReadyUpdate();
	if (App::app()->updatingState() == Application::UpdatingReady) {
		cSetRestartingUpdate(true);
	} else {
		cSetRestarting(true);
	}
	App::quit();
}
