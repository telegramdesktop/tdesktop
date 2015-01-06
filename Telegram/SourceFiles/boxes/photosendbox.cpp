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
#include "style.h"
#include "lang.h"

#include "app.h"
#include "mainwidget.h"
#include "photosendbox.h"

PhotoSendBox::PhotoSendBox(const ReadyLocalMedia &img) : _img(new ReadyLocalMedia(img)),
	_thumbx(0), _thumby(0), _thumbw(0), _thumbh(0), _namew(0), _textw(0),
	_compressed(this, lang(lng_send_image_compressed), cCompressPastedImage()),
	_sendButton(this, lang(lng_send_button), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
	a_opacity(0, 1) {
	connect(&_sendButton, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));

	_width = st::confirmWidth;
	if (_img->type == ToPreparePhoto) {
		int32 maxW = 0, maxH = 0;
		for (PreparedPhotoThumbs::const_iterator i = _img->photoThumbs.cbegin(), e = _img->photoThumbs.cend(); i != e; ++i) {
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
		_thumbw = _width - st::boxPadding.left() - st::boxPadding.right();
		if (_thumb.width() < _thumbw) {
			_thumbw = (_thumb.width() > 20) ? _thumb.width() : 20;
		}
		int32 maxthumbh = qMin(qRound(1.5 * _thumbw), int(st::confirmMaxHeight));
		_thumbh = qRound(th * float64(_thumbw) / tw);
		if (_thumbh > maxthumbh) {
			_thumbw = qRound(_thumbw * float64(maxthumbh) / _thumbh);
			_thumbh = maxthumbh;
			if (_thumbw < 10) {
				_thumbw = 10;
			}
		}
		_height = _thumbh + st::boxPadding.top() + st::boxFont->height + st::boxPadding.bottom() + st::boxPadding.bottom() + _compressed.height() + _sendButton.height();

		_thumb = QPixmap::fromImage(_thumb.toImage().scaled(_thumbw, _thumbh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly);
	} else {
		_compressed.hide();
		if (!_img->photoThumbs.isEmpty()) {
			_thumb = _img->photoThumbs.cbegin().value();
			int32 tw = _thumb.width(), th = _thumb.height();
			if (_thumb.isNull() || !tw || !th) {
				_thumbw = _thumbx = _thumby = 0;
			} else if (tw > th) {
				_thumbw = (tw * st::mediaThumbSize) / th;
				_thumbx = (_thumbw - st::mediaThumbSize) / 2;
				_thumby = 0;
			} else {
				_thumbw = st::mediaThumbSize;
				_thumbx = 0;
				_thumby = ((th * _thumbw) / tw - st::mediaThumbSize) / 2;
			}
		}
		if (_thumbw) {
			_thumb = QPixmap::fromImage(_thumb.toImage().scaledToWidth(_thumbw * cIntRetinaFactor(), Qt::SmoothTransformation), Qt::ColorOnly);
			_thumb.setDevicePixelRatio(cRetinaFactor());
		}
		_height = st::boxPadding.top() + st::boxFont->height + st::boxPadding.bottom() + st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom() + st::boxPadding.bottom() + _sendButton.height();

		_name = _img->filename;
		_namew = st::mediaFont->m.width(_name);
		_size = formatSizeText(_img->filesize);
		_textw = qMax(_namew, st::mediaFont->m.width(_size));
	}

	resize(_width, _height);
}

PhotoSendBox::PhotoSendBox(const QString &phone, const QString &fname, const QString &lname) : _img(0),
_thumbx(0), _thumby(0), _thumbw(0), _thumbh(0), _namew(0), _textw(0),
_compressed(this, lang(lng_send_image_compressed), true),
_sendButton(this, lang(lng_send_button), st::btnSelectDone),
_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
_phone(phone), _fname(fname), _lname(lname),
a_opacity(0, 1) {
	connect(&_sendButton, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));

	_width = st::confirmWidth;
	_compressed.hide();
	_height = st::boxPadding.top() + st::boxFont->height + st::boxPadding.bottom() + st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom() + st::boxPadding.bottom() + _sendButton.height();

	_name = _fname + QChar(' ') + _lname;
	_namew = st::mediaFont->m.width(_name);
	_size = _phone;
	_textw = qMax(_namew, st::mediaFont->m.width(_size));

	resize(_width, _height);
}


void PhotoSendBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onSend((e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier)) && e->modifiers().testFlag(Qt::ShiftModifier));
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void PhotoSendBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	_sendButton.move(_width - _sendButton.width(), _height - _sendButton.height());
	_cancelButton.move(0, _height - _cancelButton.height());
	_compressed.move((width() - _compressed.width()) / 2, _height - _cancelButton.height() - _compressed.height() - st::confirmCompressedSkip);
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
	if (_img && _img->type == ToPreparePhoto) {
		p.drawText(QRect(st::boxPadding.left(), st::boxPadding.top(), _width - st::boxPadding.left() - st::boxPadding.right(), st::boxFont->height), lang(lng_really_send_image), style::al_center);
		p.drawPixmap((_width - _thumbw) / 2, st::boxPadding.top() * 2 + st::boxFont->height, _thumb);
	} else {
		p.drawText(QRect(st::boxPadding.left(), st::boxPadding.top(), _width - st::boxPadding.left() - st::boxPadding.right(), st::boxFont->height), lang(_img ? lng_really_send_file : lng_really_share_contact), style::al_center);

		int32 w = _width - st::boxPadding.left() - st::boxPadding.right(), h = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
		int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
		int32 twidth = w - tleft - st::mediaPadding.right();
		if (twidth > _textw) {
			w -= (twidth - _textw);
			twidth = _textw;
		}
		int32 x = (_width - w) / 2, y = st::boxPadding.top() * 2 + st::boxFont->height;

		p.fillRect(QRect(x, y, w, h), st::msgOutBG->b);
		p.fillRect(x, y + h, w, st::msgShadow, st::msgOutShadow->b);
		if (_thumbw) {
			int32 rf(cIntRetinaFactor());
			p.drawPixmap(QPoint(x + st::mediaPadding.left(), y + st::mediaPadding.top()), _thumb, QRect(_thumbx * rf, _thumby * rf, st::mediaThumbSize * rf, st::mediaThumbSize * rf));
		} else if (_img) {
			p.drawPixmap(QPoint(x + st::mediaPadding.left(), y + st::mediaPadding.top()), App::sprite(), st::mediaDocOutImg);
		} else {
			p.drawPixmap(x + st::mediaPadding.left(), y + st::mediaPadding.top(), userDefPhoto(1)->pix(st::mediaThumbSize));
		}

		p.setFont(st::mediaFont->f);
		p.setPen(st::black->c);
		if (twidth < _namew) {
			p.drawText(x + tleft, y + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, st::mediaFont->m.elidedText(_name, Qt::ElideRight, twidth));
		} else {
			p.drawText(x + tleft, y + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, _name);
		}

		p.setPen(st::mediaOutColor->p);
		p.drawText(x + tleft, y + st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->descent, _size);
	}
}

void PhotoSendBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
	} else {
		a_opacity.update(ms, anim::linear);
	}
	_sendButton.setOpacity(a_opacity.current());
	_cancelButton.setOpacity(a_opacity.current());
	_compressed.setOpacity(a_opacity.current());
	update();
}

void PhotoSendBox::onSend(bool ctrlShiftEnter) {
	if (!_img) {
		if (App::main()) App::main()->confirmShareContact(ctrlShiftEnter, _phone, _fname, _lname);
	} else {
		if (!_compressed.isHidden()) {
			cSetCompressPastedImage(_compressed.checked());
			App::writeUserConfig();
		}
		if (_compressed.isHidden() || _compressed.checked()) {
			_img->ctrlShiftEnter = ctrlShiftEnter;
			if (App::main()) App::main()->confirmSendImage(*_img);
		} else {
			if (App::main()) App::main()->confirmSendImageUncompressed(ctrlShiftEnter);
		}
	}
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
	delete _img;
	if (App::main()) App::main()->cancelSendImage();
}
