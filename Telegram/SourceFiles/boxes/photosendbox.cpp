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

#include "app.h"
#include "mainwidget.h"
#include "photosendbox.h"

PhotoSendBox::PhotoSendBox(const ReadyLocalMedia &img) : _img(img),
	_sendButton(this, lang(lng_send_button), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
	a_opacity(0, 1) {

	connect(&_sendButton, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));

	int32 maxW = 0, maxH = 0;
	for (PreparedPhotoThumbs::const_iterator i = img.photoThumbs.cbegin(), e = img.photoThumbs.cend(); i != e; ++i) {
		if (i->width() >= maxW && i->height() >= maxH) {
			_thumb = *i;
			maxW = _thumb.width();
			maxH = _thumb.height();
		}
	}
	int32 tw = _thumb.width(), th = _thumb.height();
	if (!tw || !th) {
		tw = th = 1;
	}
	_width = st::confirmWidth;
	_thumbw = _width - st::boxPadding.left() - st::boxPadding.right();
	if (_thumb.width() < _thumbw) {
		_thumbw = (_thumb.width() > 20) ? _thumb.width() : 20;
	}
	int32 maxthumbh = qRound(1.5 * _thumbw);
	_thumbh = qRound(th * float64(_thumbw) / tw);
	if (_thumbh > maxthumbh) {
		_thumbw = qRound(_thumbw * float64(maxthumbh) / _thumbh);
		_thumbh = maxthumbh;
		if (_thumbw < 10) {
			_thumbw = 10;
		}
	}
	_height = _thumbh + st::boxPadding.top() + st::boxFont->height + st::boxPadding.bottom() + st::boxPadding.bottom() + _sendButton.height();

	_thumb = QPixmap::fromImage(_thumb.toImage().scaled(_thumbw, _thumbh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));

	resize(_width, _height);
}

void PhotoSendBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onSend();
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void PhotoSendBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	_sendButton.move(_width - _sendButton.width(), _height - _sendButton.height());
	_cancelButton.move(0, _height - _cancelButton.height());
	update();
}

void PhotoSendBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setOpacity(a_opacity.current());
	
	// fill bg
	p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

	// paint shadows
	p.fillRect(0, _height - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, _height - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

	p.setFont(st::boxFont->f);
	p.setPen(st::boxGrayTitle->p);
	p.drawText(QRect(st::boxPadding.left(), st::boxPadding.top(), _width - st::boxPadding.left() - st::boxPadding.right(), st::boxFont->height), lang(lng_really_send_image), style::al_center);
	p.drawPixmap((_width - _thumbw) / 2, st::boxPadding.top() * 2 + st::boxFont->height, _thumb);
}

void PhotoSendBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
	} else {
		a_opacity.update(ms, anim::linear);
	}
	_sendButton.setOpacity(a_opacity.current());
	_cancelButton.setOpacity(a_opacity.current());
	update();
}

void PhotoSendBox::onSend() {
	if (App::main()) App::main()->confirmSendImage(_img);
	emit closed();
}

void PhotoSendBox::onCancel() {
	if (App::main()) App::main()->cancelSendImage();
	emit closed();
}

void PhotoSendBox::startHide() {
	_hiding = true;
	a_opacity.start(0);
}

PhotoSendBox::~PhotoSendBox() {
	if (App::main()) App::main()->cancelSendImage();
}
