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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "localstorage.h"

#include "abstractbox.h"
#include "mainwidget.h"
#include "window.h"

AbstractBox::AbstractBox(int32 w) : _maxHeight(0), _hiding(false), a_opacity(0, 1) {
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
		p.fillRect(rect(), st::boxBG->b);
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
	return result;
}

void AbstractBox::paintTitle(Painter &p, const QString &title, bool withShadow) {
	if (withShadow) {
		// paint shadow
		p.fillRect(0, st::old_boxTitleHeight, width(), st::scrollDef.topsh, st::scrollDef.shColor->b);
	}

	// paint box title
	p.setFont(st::old_boxTitleFont->f);
	p.setPen(st::black->p);
	p.drawTextLeft(st::old_boxTitlePos.x(), st::old_boxTitlePos.y(), width(), title);
}

void AbstractBox::paintGrayTitle(QPainter &p, const QString &title) {
	// draw box title
	p.setFont(st::boxFont->f);
	p.setPen(st::boxGrayTitle->p);
	p.drawText(QRect(st::old_boxTitlePos.x(), st::old_boxTitlePos.y(), width() - 2 * st::old_boxTitlePos.x(), st::boxFont->height), title, style::al_top);
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
		if (parentWidget()) parentWidget()->update(geometry().united(g).marginsAdded(QMargins(st::boxShadow.pxWidth(), st::boxShadow.pxHeight(), st::boxShadow.pxWidth(), st::boxShadow.pxHeight())));
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

ScrollableBox::ScrollableBox(const style::flatScroll &scroll) : AbstractBox(),
_scroll(this, scroll), _innerPtr(0), _topSkip(st::old_boxTitleHeight), _bottomSkip(0) {
}

void ScrollableBox::resizeEvent(QResizeEvent *e) {
	_scroll.setGeometry(0, _topSkip, width(), height() - _topSkip - _bottomSkip);
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
}

void ScrollableBox::showAll() {
	_scroll.show();
}

ItemListBox::ItemListBox(const style::flatScroll &scroll) : ScrollableBox(scroll) {
	setMaxHeight(st::boxMaxListHeight);
}
