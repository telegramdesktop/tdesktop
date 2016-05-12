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
#include "profile/profile_widget.h"

#include "profile/profile_inner_widget.h"
#include "mainwindow.h"
#include "application.h"

namespace Profile {

Widget::Widget(QWidget *parent, PeerData *peer) : TWidget(parent)
, _scroll(this, st::setScroll)
, _inner(this, peer)
, _sideShadow(this, st::shadowColor) {
	_scroll->setWidget(_inner);
	_scroll->move(0, 0);
	_scroll->show();

	_sideShadow->setVisible(!Adaptive::OneColumn());

	connect(_scroll, SIGNAL(scrolled()), _inner, SLOT(updateSelected()));
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
}

PeerData *Widget::peer() const {
	return _inner->peer();
}

void Widget::setGeometryWithTopMoved(const QRect &newGeometry, int topDelta) {
	_topDelta = topDelta;
	bool willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		setGeometry(newGeometry);
	}
	if (!willBeResized) {
		resizeEvent(nullptr);
	}
	_topDelta = 0;
}

void Widget::showAnimated(SlideDirection direction, const QPixmap &oldContentCache) {
	show();
	_scroll->show();
	_sideShadow->hide();
	auto myContentCache = myGrab(this);
	_scroll->hide();
	_sideShadow->show();

	_showAnimation = std_::make_unique<SlideAnimation>();
	_showAnimation->setDirection(direction);
	_showAnimation->setRepaintCallback(func(this, &Widget::repaintCallback));
	_showAnimation->setFinishedCallback(func(this, &Widget::showFinished));
	_showAnimation->setPixmaps(oldContentCache, myContentCache);
	_showAnimation->start();
}

void Widget::setInnerFocus() {
	_inner->setFocus();
}

void Widget::resizeEvent(QResizeEvent *e) {
	int newScrollTop = _scroll->scrollTop() + _topDelta;
	if (_scroll->size() != size()) {
		_scroll->resize(size());
		if (_inner->width() != width()) {
			_inner->resizeToWidth(width(), _scroll->height());
		}
	}
	if (!_scroll->isHidden()) {
		if (_topDelta) {
			_scroll->scrollToY(newScrollTop);
		}
		int notDisplayedAtBottom = _scroll->scrollTopMax() - _scroll->scrollTop();
		if (notDisplayedAtBottom > 0) {
			_inner->decreaseAdditionalHeight(notDisplayedAtBottom);
		}
	}

	_sideShadow->resize(st::lineWidth, height());
	_sideShadow->moveToLeft(0, 0);
}

void Widget::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) return;

	if (_showAnimation) {
		Painter p(this);
		_showAnimation->paintContents(p, e->rect());
	}
}

void Widget::showFinished() {
	if (isHidden()) return;

	App::app()->mtpUnpause();
	_scroll->show();
	setInnerFocus();
}

} // namespace Profile
