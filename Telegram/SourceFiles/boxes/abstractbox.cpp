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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "localstorage.h"

#include "abstractbox.h"
#include "mainwidget.h"
#include "window.h"

void BlueTitleShadow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect());
	p.drawPixmap(QRect(r.left(), 0, r.width(), height()), App::sprite(), st::boxBlueShadow);
}

BlueTitleClose::BlueTitleClose(QWidget *parent) : Button(parent)
, a_iconFg(st::boxBlueCloseBg->c)
, _a_over(animFunc(this, &BlueTitleClose::animStep_over)) {
	resize(st::boxTitleHeight, st::boxTitleHeight);
	setCursor(style::cur_pointer);
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
}

void BlueTitleClose::onStateChange(int oldState, ButtonStateChangeSource source) {
	if ((oldState & StateOver) != (_state & StateOver)) {
		a_iconFg.start(((_state & StateOver) ? st::white : st::boxBlueCloseBg)->c);
		_a_over.start();
	}
}

bool BlueTitleClose::animStep_over(float64 ms) {
	float64 dt = ms / st::boxBlueCloseDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_iconFg.finish();
	} else {
		a_iconFg.update(dt, anim::linear);
	}
	update((st::boxTitleHeight - st::boxBlueCloseIcon.pxWidth()) / 2, (st::boxTitleHeight - st::boxBlueCloseIcon.pxHeight()) / 2, st::boxBlueCloseIcon.pxWidth(), st::boxBlueCloseIcon.pxHeight());
	return res;

}

void BlueTitleClose::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect()), s((st::boxTitleHeight - st::boxBlueCloseIcon.pxWidth()) / 2, (st::boxTitleHeight - st::boxBlueCloseIcon.pxHeight()) / 2, st::boxBlueCloseIcon.pxWidth(), st::boxBlueCloseIcon.pxHeight());
	if (!s.contains(r)) {
		p.fillRect(r, st::boxBlueTitleBg->b);
	}
	if (s.intersects(r)) {
		p.fillRect(s.intersected(r), a_iconFg.current());
		p.drawSprite(s.topLeft(), st::boxBlueCloseIcon);
	}
}

AbstractBox::AbstractBox(int32 w) : LayeredWidget()
, _maxHeight(0)
, _hiding(false)
, a_opacity(0, 1)
, _blueTitle(false)
, _blueClose(0)
, _blueShadow(0) {
	resize(w, 0);
}

void AbstractBox::prepare() {
	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void AbstractBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	} else {
		LayeredWidget::keyPressEvent(e);
	}
}

void AbstractBox::resizeEvent(QResizeEvent *e) {
	if (_blueClose) {
		_blueClose->moveToRight(0, 0);
	}
	if (_blueShadow) {
		_blueShadow->moveToLeft(0, st::boxTitleHeight);
		_blueShadow->resize(width(), st::boxBlueShadow.pxHeight());
	}
}

void AbstractBox::parentResized() {
	int32 newHeight = countHeight();
	setGeometry((App::wnd()->width() - width()) / 2, (App::wnd()->height() - newHeight) / 2, width(), newHeight);
	update();
}

bool AbstractBox::paint(QPainter &p) {
	bool result = true;
	if (_cache.isNull()) {
		result = (_hiding && a_opacity.current() < 0.01);

		// fill bg
		p.fillRect(rect(), st::boxBg->b);
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
	return result;
}

void AbstractBox::paintTitle(Painter &p, const QString &title, const QString &additional) {
	p.setFont(st::boxTitleFont);
	if (_blueTitle) {
		p.fillRect(0, 0, width(), st::boxTitleHeight, st::boxBlueTitleBg->b);
		p.setPen(st::white);

		int32 titleWidth = st::boxTitleFont->width(title);
		p.drawTextLeft(st::boxBlueTitlePosition.x(), st::boxBlueTitlePosition.y(), width(), title, titleWidth);

		if (!additional.isEmpty()) {
			p.setFont(st::boxTextFont);
			p.setPen(st::boxBlueTitleAdditionalFg);
			p.drawTextLeft(st::boxBlueTitlePosition.x() + titleWidth + st::boxBlueTitleAdditionalSkip, st::boxBlueTitlePosition.y(), width(), additional);
		}
	} else {
		p.setPen(st::boxTitleFg);
		p.drawTextLeft(st::boxTitlePosition.x(), st::boxTitlePosition.y(), width(), title);
	}
}

void AbstractBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (paint(p)) return;
}

void AbstractBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		setAttribute(Qt::WA_OpaquePaintEvent);
		if (!_hiding) {
			showAll();
			showDone();
		}
	} else {
		a_opacity.update(ms, anim::linear);
	}
	update();
}

void AbstractBox::setMaxHeight(int32 maxHeight) {
	resizeMaxHeight(width(), maxHeight);
}

void AbstractBox::resizeMaxHeight(int32 newWidth, int32 maxHeight) {
	if (width() != newWidth || _maxHeight != maxHeight) {
		QRect g(geometry());
		_maxHeight = maxHeight;
		resize(newWidth, countHeight());
		if (parentWidget()) {
			parentWidget()->update(geometry().united(g).marginsAdded(QMargins(st::boxShadow.pxWidth(), st::boxShadow.pxHeight(), st::boxShadow.pxWidth(), st::boxShadow.pxHeight())));
		}
	}
}

int32 AbstractBox::countHeight() const {
	return qMin(_maxHeight, App::wnd()->height() - int32(2 * st::boxVerticalMargin));
}

void AbstractBox::onClose() {
	closePressed();
	emit closed();
}

void AbstractBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void AbstractBox::setBlueTitle(bool blue) {
	_blueTitle = blue;
	delete _blueShadow;
	_blueShadow = new BlueTitleShadow(this);
	delete _blueClose;
	_blueClose = new BlueTitleClose(this);
	_blueClose->setAttribute(Qt::WA_OpaquePaintEvent);
	connect(_blueClose, SIGNAL(clicked()), this, SLOT(onClose()));
}

void AbstractBox::raiseShadow() {
	if (_blueShadow) {
		_blueShadow->raise();
	}
}

void ScrollableBoxShadow::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::boxScrollShadowBg->b);
}

ScrollableBox::ScrollableBox(const style::flatScroll &scroll, int32 w) : AbstractBox(w),
_scroll(this, scroll), _innerPtr(0), _topSkip(st::boxTitleHeight), _bottomSkip(st::boxScrollSkip) {
	setBlueTitle(true);
}

void ScrollableBox::resizeEvent(QResizeEvent *e) {
	_scroll.setGeometry(0, _topSkip, width(), height() - _topSkip - _bottomSkip);
	AbstractBox::resizeEvent(e);
}

void ScrollableBox::init(QWidget *inner, int32 bottomSkip, int32 topSkip) {
	_bottomSkip = bottomSkip;
	_topSkip = topSkip;
	_innerPtr = inner;
	_scroll.setWidget(_innerPtr);
	_scroll.setFocusPolicy(Qt::NoFocus);
	ScrollableBox::resizeEvent(0);
}

void ScrollableBox::hideAll() {
	_scroll.hide();
	AbstractBox::hideAll();
}

void ScrollableBox::showAll() {
	_scroll.show();
	AbstractBox::showAll();
}

ItemListBox::ItemListBox(const style::flatScroll &scroll, int32 w) : ScrollableBox(scroll, w) {
	setMaxHeight(st::boxMaxListHeight);
}
