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

PhotoSendBox::PhotoSendBox(const FileLoadResultPtr &file) : AbstractBox(st::boxWideWidth)
, _file(file)
, _animated(false)
, _caption(this, st::confirmCaptionArea, lang(lng_photo_caption))
, _compressedFromSettings(_file->type == PrepareAuto)
, _compressed(this, lang(lng_send_image_compressed), _compressedFromSettings ? cCompressPastedImage() : true)
, _send(this, lang(lng_send_button), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _thumbx(0)
, _thumby(0)
, _thumbw(0)
, _thumbh(0)
, _statusw(0)
, _isImage(false)
, _replyTo(_file->to.replyTo)
, _confirmed(false) {
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	_animated = false;
	QSize dimensions;
	if (_file->photo.type() != mtpc_photoEmpty) {
		_file->type = PreparePhoto;
	} else if (_file->document.type() == mtpc_document) {
		const MTPDdocument &document(_file->document.c_document());
		const QVector<MTPDocumentAttribute> &attributes(document.vattributes.c_vector().v);
		for (int32 i = 0, l = attributes.size(); i < l; ++i) {
			if (attributes.at(i).type() == mtpc_documentAttributeAnimated) {
				_animated = true;
			} else if (attributes.at(i).type() == mtpc_documentAttributeImageSize) {
				dimensions = QSize(attributes.at(i).c_documentAttributeImageSize().vw.v, attributes.at(i).c_documentAttributeImageSize().vh.v);
			} else if (attributes.at(i).type() == mtpc_documentAttributeVideo) {
				dimensions = QSize(attributes.at(i).c_documentAttributeVideo().vw.v, attributes.at(i).c_documentAttributeVideo().vh.v);
			}
		}
		if (dimensions.isEmpty()) _animated = false;
	}
	if (_file->type == PreparePhoto || _animated) {
		int32 maxW = 0, maxH = 0;
		if (_animated) {
			int32 limitW = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
			int32 limitH = st::confirmMaxHeight;
			maxW = dimensions.width();
			maxH = dimensions.height();
			if (maxW * limitH > maxH * limitW) {
				if (maxW < limitW) {
					maxH = maxH * limitW / maxW;
					maxW = limitW;
				}
			} else {
				if (maxH < limitH) {
					maxW = maxW * limitH / maxH;
					maxH = limitH;
				}
			}
			_thumb = imagePix(_file->thumb.toImage(), maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), true, true, false, maxW, maxH);
		} else {
			for (PreparedPhotoThumbs::const_iterator i = _file->photoThumbs.cbegin(), e = _file->photoThumbs.cend(); i != e; ++i) {
				if (i->width() >= maxW && i->height() >= maxH) {
					_thumb = *i;
					maxW = _thumb.width();
					maxH = _thumb.height();
				}
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
		if (_file->thumb.isNull()) {
			_thumbw = 0;
		} else {
			_thumb = _file->thumb;
			int32 tw = _thumb.width(), th = _thumb.height();
			if (tw > th) {
				_thumbw = (tw * st::msgFileThumbSize) / th;
			} else {
				_thumbw = st::msgFileThumbSize;
			}
			_thumb = imagePix(_thumb.toImage(), _thumbw * cIntRetinaFactor(), 0, true, false, true, st::msgFileThumbSize, st::msgFileThumbSize);
		}

		_name.setText(st::semiboldFont, _file->filename, _textNameOptions);
		_status = formatSizeText(_file->filesize);
		_statusw = qMax(_name.maxWidth(), st::normalFont->width(_status));
		_isImage = fileIsImage(_file->filename, _file->filemime);
	}
	if (_file->type != PreparePhoto) {
		_compressed.hide();
	}

	updateBoxSize();
	_caption.setMaxLength(MaxPhotoCaption);
	_caption.setCtrlEnterSubmit(CtrlEnterSubmitBoth);
	connect(&_compressed, SIGNAL(changed()), this, SLOT(onCompressedChange()));
	connect(&_caption, SIGNAL(resized()), this, SLOT(onCaptionResized()));
	connect(&_caption, SIGNAL(submitted(bool)), this, SLOT(onSend(bool)));
	connect(&_caption, SIGNAL(cancelled()), this, SLOT(onClose()));

	prepare();
}

PhotoSendBox::PhotoSendBox(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo) : AbstractBox(st::boxWideWidth)
, _caption(this, st::confirmCaptionArea, lang(lng_photo_caption))
, _compressed(this, lang(lng_send_image_compressed), true)
, _send(this, lang(lng_send_button), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _thumbx(0)
, _thumby(0)
, _thumbw(0)
, _thumbh(0)
, _statusw(0)
, _isImage(false)
, _phone(phone)
, _fname(fname)
, _lname(lname)
, _replyTo(replyTo)
, _confirmed(false) {
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	_compressed.hide();

	_name.setText(st::semiboldFont, lng_full_name(lt_first_name, _fname, lt_last_name, _lname), _textNameOptions);
	_status = _phone;
	_statusw = qMax(_name.maxWidth(), st::normalFont->width(_status));

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
	if (_file && (_file->type == PreparePhoto || _animated)) {
		setMaxHeight(st::boxPhotoPadding.top() + _thumbh + st::boxPhotoPadding.bottom() + (_animated ? 0 : (st::boxPhotoCompressedPadding.top() + _compressed.height())) + st::boxPhotoCompressedPadding.bottom() + _caption.height() + st::boxButtonPadding.top() + _send.height() + st::boxButtonPadding.bottom());
	} else if (_thumbw) {
		setMaxHeight(st::boxPhotoPadding.top() + st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom() + (_file ? (st::boxPhotoCompressedPadding.bottom() + _caption.height()) : 0) + st::boxPhotoPadding.bottom() + st::boxButtonPadding.top() + _send.height() + st::boxButtonPadding.bottom());
	} else {
		setMaxHeight(st::boxPhotoPadding.top() + st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom() + (_file ? (st::boxPhotoCompressedPadding.bottom() + _caption.height()) : 0) + st::boxPhotoPadding.bottom() + st::boxButtonPadding.top() + _send.height() + st::boxButtonPadding.bottom());
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

	if (_file && (_file->type == PreparePhoto || _animated)) {
		if (_thumbx > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _thumbx - st::boxPhotoPadding.left(), _thumbh, st::confirmBg->b);
		}
		if (_thumbx + _thumbw < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_thumbx + _thumbw, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _thumbx - _thumbw, _thumbh, st::confirmBg->b);
		}
		p.drawPixmap(_thumbx, st::boxPhotoPadding.top(), _thumb);
		if (_animated) {
			QRect inner(_thumbx + (_thumbw - st::msgFileSize) / 2, st::boxPhotoPadding.top() + (_thumbh - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgDateImgBg);

			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.drawEllipse(inner);
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);

			p.drawSpriteCenter(inner, st::msgFileInPlay);
		}
	} else {
		int32 w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		int32 h = _thumbw ? (st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom()) : (st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom());
		int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
		if (_thumbw) {
			nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
			nametop = st::msgFileThumbNameTop;
			nameright = st::msgFileThumbPadding.left();
			statustop = st::msgFileThumbStatusTop;
			linktop = st::msgFileThumbLinkTop;
		} else {
			nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
			nametop = st::msgFileNameTop;
			nameright = st::msgFilePadding.left();
			statustop = st::msgFileStatusTop;
		}
		int32 namewidth = w - nameleft - (_thumbw ? st::msgFileThumbPadding.left() : st::msgFilePadding.left());
		if (namewidth > _statusw) {
			w -= (namewidth - _statusw);
			namewidth = _statusw;
		}
		int32 x = (width() - w) / 2, y = st::boxPhotoPadding.top();

		App::roundRect(p, x, y, w, h, st::msgOutBg, MessageOutCorners, &st::msgOutShadow);

		if (_thumbw) {
			QRect rthumb(rtlrect(x + st::msgFileThumbPadding.left(), y + st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, width()));
			p.drawPixmap(rthumb.topLeft(), _thumb);
		} else if (_file) {
			QRect inner(rtlrect(x + st::msgFilePadding.left(), y + st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, width()));
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgFileOutBg);

			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.drawEllipse(inner);
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);

			p.drawSpriteCenter(inner, _isImage ? st::msgFileOutImage : st::msgFileOutFile);
		} else {
			p.drawPixmapLeft(x + st::msgFilePadding.left(), y + st::msgFilePadding.top(), width(), userDefPhoto(1)->pixRounded(st::msgFileSize));
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::black);
		_name.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

		style::color status(st::mediaOutFg);
		p.setFont(st::normalFont);
		p.setPen(status);
		p.drawTextLeft(x + nameleft, y + statustop, width(), _status);
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
	if (!_confirmed && App::main()) {
		if (_file) {
			App::main()->onSendFileCancel(_file);
		} else {
			App::main()->onShareContactCancel();
		}
	}
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
	if (_file) {
		if (_file->type == PreparePhoto) {
			_compressed.show();
		}
		_caption.show();
	} else {
		_caption.hide();
		_compressed.hide();
	}
}

void PhotoSendBox::showDone() {
	setInnerFocus();
}

void PhotoSendBox::onSend(bool ctrlShiftEnter) {
	if (App::main()) {
		if (_file) {
			if (_compressed.isHidden()) {
				if (_file->type == PrepareAuto) {
					_file->type = PrepareDocument;
				}
			} else {
				if (_compressedFromSettings && _compressed.checked() != cCompressPastedImage()) {
					cSetCompressPastedImage(_compressed.checked());
					Local::writeUserSettings();
				}
				if (_compressed.checked()) {
					_file->type = PreparePhoto;
				} else {
					_file->type = PrepareDocument;
				}
			}
			if (!_caption.isHidden()) {
				_file->caption = prepareText(_caption.getLastText(), true);
			}
			App::main()->onSendFileConfirm(_file, ctrlShiftEnter);
		} else {
			App::main()->onShareContactConfirm(_phone, _fname, _lname, _replyTo, ctrlShiftEnter);
		}
	}
	_confirmed = true;
	onClose();
}
