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
#include "lang.h"

#include "mediaview.h"
#include "window.h"

MediaView::MediaView() : QWidget(App::wnd()),
_photo(0), _maxWidth(0), _maxHeight(0), _x(0), _y(0), _w(0) {
	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint);
	moveToScreen();
	setAttribute(Qt::WA_PaintOnScreen, true);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
	hide();
}

void MediaView::moveToScreen() {
	QPoint wndCenter(App::wnd()->x() + App::wnd()->width() / 2, App::wnd()->y() + App::wnd()->height() / 2);
	QRect geom = QDesktopWidget().screenGeometry(wndCenter);
	if (geom != geometry()) {
		setGeometry(geom);
	}
	_maxWidth = width() - 2 * st::medviewNavBarWidth;
	_maxHeight = height() - st::medviewTopSkip - st::medviewBottomSkip;
}

void MediaView::showPhoto(PhotoData *photo, const QRect &opaque) {
	_photo = photo;
	_opaqueRect = opaque;
	_photo->full->load();
	_w = photo->full->width();
	int h = photo->full->height();
	switch (cScale()) {
	case dbisOneAndQuarter: _w = qRound(float64(_w) * 1.25 - 0.01); h = qRound(float64(h) * 1.25 - 0.01); break;
	case dbisOneAndHalf: _w = qRound(float64(_w) * 1.5 - 0.01); h = qRound(float64(h) * 1.5 - 0.01); break;
	case dbisTwo: _w *= 2; h *= 2; break;
	}
	if (_w > _maxWidth) {
		h = qRound(h * _maxWidth / float64(_w));
		_w = _maxWidth;
	}
	if (h > _maxHeight) {
		_w = qRound(_w * _maxHeight / float64(h));
		h = _maxHeight;
	}
	_x = (width() - _w) / 2;
	_y = (height() - h) / 2;
	if (isHidden()) {
		moveToScreen();
		bool wm = testAttribute(Qt::WA_Mapped), wv = testAttribute(Qt::WA_WState_Visible);
		if (!wm) setAttribute(Qt::WA_Mapped, true);
		if (!wv) setAttribute(Qt::WA_WState_Visible, true);
		update();
		QEvent e(QEvent::UpdateRequest);
		event(&e);
		if (!wm) setAttribute(Qt::WA_Mapped, false);
		if (!wv) setAttribute(Qt::WA_WState_Visible, false);
		show();
	}
	update();
}

void MediaView::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setOpacity(st::medviewLightOpacity);
	p.fillRect(QRect(0, 0, st::medviewNavBarWidth, height()), st::black->b);
	p.fillRect(QRect(width() - st::medviewNavBarWidth, 0, st::medviewNavBarWidth, height()), st::black->b);
	p.fillRect(QRect(st::medviewNavBarWidth, 0, width() - 2 * st::medviewNavBarWidth, height()), st::black->b);
	p.setOpacity(1);
	p.drawPixmap(_x, _y, (_photo->full->loaded() ? _photo->full : _photo->thumb)->pixNoCache(_w, 0, true));
}

void MediaView::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		App::wnd()->layerHidden();
	}
}

void MediaView::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		App::wnd()->layerHidden();
	}
}
