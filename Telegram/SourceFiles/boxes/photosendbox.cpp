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
#include "style.h"
#include "lang.h"

#include "localstorage.h"

#include "mainwidget.h"
#include "photosendbox.h"

PhotoSendBox::PhotoSendBox(const ReadyLocalMedia &img) : AbstractBox(st::boxWideWidth)
, _img(new ReadyLocalMedia(img))
, _thumbx(0)
, _thumby(0)
, _thumbw(0)
, _thumbh(0)
, _namew(0)
, _textw(0)
, _caption(this, st::confirmCaptionArea, lang(lng_photo_caption))
, _compressed(this, lang(lng_send_image_compressed), cCompressPastedImage())
, _send(this, lang(lng_send_button), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _replyTo(img.replyTo) {
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

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
		_thumbw = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
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
		_thumbx = (width() - _thumbw) / 2;

		_thumb = QPixmap::fromImage(_thumb.toImage().scaled(_thumbw * cIntRetinaFactor(), _thumbh * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly);
		_thumb.setDevicePixelRatio(cRetinaFactor());
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

		_name = _img->filename;
		_namew = st::mediaFont->width(_name);
		_size = formatSizeText(_img->filesize);
		_textw = qMax(_namew, st::mediaFont->width(_size));
	}
	updateBoxSize();
	_caption.setMaxLength(MaxPhotoCaption);
	_caption.setCtrlEnterSubmit(false);
	connect(&_compressed, SIGNAL(changed()), this, SLOT(onCompressedChange()));
	connect(&_caption, SIGNAL(resized()), this, SLOT(onCaptionResized()));
	connect(&_caption, SIGNAL(submitted(bool)), this, SLOT(onSend(bool)));
	connect(&_caption, SIGNAL(cancelled()), this, SLOT(onClose()));
	prepare();
}

PhotoSendBox::PhotoSendBox(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo) : AbstractBox(st::boxWideWidth)
, _img(0)
, _thumbx(0)
, _thumby(0)
, _thumbw(0)
, _thumbh(0)
, _namew(0)
, _textw(0)
, _caption(this, st::confirmCaptionArea, lang(lng_photo_caption))
, _compressed(this, lang(lng_send_image_compressed), true)
, _send(this, lang(lng_send_button), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _phone(phone), _fname(fname), _lname(lname), _replyTo(replyTo) {
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	_compressed.hide();

	_name = lng_full_name(lt_first_name, _fname, lt_last_name, _lname);
	_namew = st::mediaFont->width(_name);
	_size = _phone;
	_textw = qMax(_namew, st::mediaFont->width(_size));

	updateBoxSize();
	prepare();
}

void PhotoSendBox::onCompressedChange() {
	showAll();
	if (_caption.isHidden()) {
		setFocus();
	} else {
		_caption.setFocus();
	}
	updateBoxSize();
	resizeEvent(0);
	update();
}

void PhotoSendBox::onCaptionResized() {
	updateBoxSize();
	resizeEvent(0);
	update();
}

void PhotoSendBox::updateBoxSize() {
	if (_img && _img->type == ToPreparePhoto) {
		setMaxHeight(st::boxPhotoPadding.top() + _thumbh + st::boxPhotoPadding.bottom() + st::boxPhotoCompressedPadding.top() + _compressed.height() + (_compressed.checked() ? (st::boxPhotoCompressedPadding.bottom() + _caption.height()) : 0) + st::boxButtonPadding.top() + _send.height() + st::boxButtonPadding.bottom());
	} else {
		setMaxHeight(st::boxPhotoPadding.top() + st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom() + st::boxPhotoPadding.bottom() + st::boxButtonPadding.top() + _send.height() + st::boxButtonPadding.bottom());
	}
}

void PhotoSendBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onSend((e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier)) && e->modifiers().testFlag(Qt::ShiftModifier));
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void PhotoSendBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	if (_img && _img->type == ToPreparePhoto) {
		if (_thumbx > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _thumbx - st::boxPhotoPadding.left(), _thumbh, st::confirmBg->b);
		}
		if (_thumbx + _thumbw < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_thumbx + _thumbw, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _thumbx - _thumbw, _thumbh, st::confirmBg->b);
		}
		p.drawPixmap(_thumbx, st::boxPhotoPadding.top(), _thumb);
	} else {
		int32 w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right(), h = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
		int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
		int32 twidth = w - tleft - st::mediaPadding.right();
		if (twidth > _textw) {
			w -= (twidth - _textw);
			twidth = _textw;
		}
		int32 x = (width() - w) / 2, y = st::boxPhotoPadding.top();

		App::roundRect(p, x, y, w, h, st::msgOutBg, MessageOutCorners, &st::msgOutShadow);
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
			p.drawText(x + tleft, y + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, st::mediaFont->elided(_name, twidth));
		} else {
			p.drawText(x + tleft, y + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, _name);
		}

		p.setPen(st::mediaOutColor->p);
		p.drawText(x + tleft, y + st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->descent, _size);
	}
}

void PhotoSendBox::resizeEvent(QResizeEvent *e) {
	_send.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _send.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _send.width() + st::boxButtonPadding.left(), _send.y());
	_caption.resize(st::boxWideWidth - st::boxPhotoPadding.left() - st::boxPhotoPadding.right(), _caption.height());
	_caption.moveToLeft(st::boxPhotoPadding.left(), _send.y() - st::boxButtonPadding.top() - _caption.height());
	_compressed.moveToLeft(st::boxPhotoPadding.left(), st::boxPhotoPadding.top() + _thumbh + st::boxPhotoPadding.bottom() + st::boxPhotoCompressedPadding.top());
}

void PhotoSendBox::closePressed() {
	if (App::main()) App::main()->cancelSendImage();
}

void PhotoSendBox::hideAll() {
	_send.hide();
	_cancel.hide();
	_caption.hide();
	_compressed.hide();
}

void PhotoSendBox::showAll() {
	_send.show();
	_cancel.show();
	if (_img && _img->type == ToPreparePhoto) {
		_compressed.show();
		if (_compressed.checked()) {
			_caption.show();
		} else {
			_caption.hide();
		}
	} else {
		_caption.hide();
		_compressed.hide();
	}
}

void PhotoSendBox::showDone() {
	setInnerFocus();
}

void PhotoSendBox::onSend(bool ctrlShiftEnter) {
	if (!_img) {
		if (App::main()) App::main()->confirmShareContact(ctrlShiftEnter, _phone, _fname, _lname, _replyTo);
	} else {
		if (!_compressed.isHidden()) {
			if (_compressed.checked() != cCompressPastedImage()) {
				cSetCompressPastedImage(_compressed.checked());
				Local::writeUserSettings();
			}
		}
		if (_compressed.isHidden() || _compressed.checked()) {
			_img->ctrlShiftEnter = ctrlShiftEnter;
			_img->caption = _caption.isHidden() ? QString() : prepareSentText(_caption.getLastText());
			if (App::main()) App::main()->confirmSendImage(*_img);
		} else {
			if (App::main()) App::main()->confirmSendImageUncompressed(ctrlShiftEnter, _replyTo);
		}
	}
	emit confirmed();
	emit closed();
}

PhotoSendBox::~PhotoSendBox() {
	delete _img;
	closePressed();
}
