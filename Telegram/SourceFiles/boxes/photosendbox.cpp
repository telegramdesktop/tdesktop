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
#include "boxes/photosendbox.h"

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
		const auto &document(_file->document.c_document());
		const auto &attributes(document.vattributes.c_vector().v);
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
			maxW = qMax(dimensions.width(), 1);
			maxH = qMax(dimensions.height(), 1);
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
			_thumb = imagePix(_file->thumb.toImage(), maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), ImagePixSmooth | ImagePixBlurred, maxW, maxH);
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
			_thumb = imagePix(_thumb.toImage(), _thumbw * cIntRetinaFactor(), 0, ImagePixSmooth | ImagePixRounded, st::msgFileThumbSize, st::msgFileThumbSize);
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
			p.drawPixmapLeft(x + st::msgFilePadding.left(), y + st::msgFilePadding.top(), width(), userDefPhoto(1)->pixCircled(st::msgFileSize));
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

EditCaptionBox::EditCaptionBox(HistoryItem *msg) : AbstractBox(st::boxWideWidth)
, _msgId(msg->fullId())
, _animated(false)
, _photo(false)
, _doc(false)
, _field(0)
, _save(this, lang(lng_settings_save), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _thumbx(0)
, _thumby(0)
, _thumbw(0)
, _thumbh(0)
, _statusw(0)
, _isImage(false)
, _previewCancelled(false)
, _saveRequestId(0) {
	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	QSize dimensions;
	ImagePtr image;
	QString caption;
	DocumentData *doc = 0;
	if (HistoryMedia *media = msg->getMedia()) {
		HistoryMediaType t = media->type();
		switch (t) {
		case MediaTypeGif: {
			_animated = true;
			doc = static_cast<HistoryGif*>(media)->getDocument();
			dimensions = doc->dimensions;
			image = doc->thumb;
		} break;

		case MediaTypePhoto: {
			_photo = true;
			PhotoData *photo = static_cast<HistoryPhoto*>(media)->photo();
			dimensions = QSize(photo->full->width(), photo->full->height());
			image = photo->full;
		} break;

		case MediaTypeVideo: {
			_animated = true;
			doc = static_cast<HistoryVideo*>(media)->getDocument();
			dimensions = doc->dimensions;
			image = doc->thumb;
		} break;

		case MediaTypeFile:
		case MediaTypeMusicFile:
		case MediaTypeVoiceFile: {
			_doc = true;
			doc = static_cast<HistoryDocument*>(media)->getDocument();
			image = doc->thumb;
		} break;
		}
		caption = media->getCaption();
	}
	if ((!_animated && (dimensions.isEmpty() || doc)) || image->isNull()) {
		_animated = false;
		if (image->isNull()) {
			_thumbw = 0;
		} else {
			int32 tw = image->width(), th = image->height();
			if (tw > th) {
				_thumbw = (tw * st::msgFileThumbSize) / th;
			} else {
				_thumbw = st::msgFileThumbSize;
			}
			_thumb = imagePix(image->pix().toImage(), _thumbw * cIntRetinaFactor(), 0, ImagePixSmooth | ImagePixRounded, st::msgFileThumbSize, st::msgFileThumbSize);
		}

		if (doc) {
			if (doc->voice()) {
				_name.setText(st::semiboldFont, lang(lng_media_audio), _textNameOptions);
			} else {
				_name.setText(st::semiboldFont, documentName(doc), _textNameOptions);
			}
			_status = formatSizeText(doc->size);
			_statusw = qMax(_name.maxWidth(), st::normalFont->width(_status));
			_isImage = doc->isImage();
		}
	} else {
		int32 maxW = 0, maxH = 0;
		if (_animated) {
			int32 limitW = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
			int32 limitH = st::confirmMaxHeight;
			maxW = qMax(dimensions.width(), 1);
			maxH = qMax(dimensions.height(), 1);
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
			_thumb = image->pixNoCache(maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), ImagePixSmooth | ImagePixBlurred, maxW, maxH);
		} else {
			maxW = dimensions.width();
			maxH = dimensions.height();
			_thumb = image->pixNoCache(maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), ImagePixSmooth | ImagePixRounded, maxW, maxH);
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
	}

	if (_animated || _photo || _doc) {
		_field = new InputArea(this, st::confirmCaptionArea, lang(lng_photo_caption), caption);
		_field->setMaxLength(MaxPhotoCaption);
		_field->setCtrlEnterSubmit(CtrlEnterSubmitBoth);
	} else {
		QString text = textApplyEntities(msg->originalText(), msg->originalEntities());
		_field = new InputArea(this, st::editTextArea, lang(lng_photo_caption), text);
//		_field->setMaxLength(MaxMessageSize); // entities can make text in input field larger but still valid
		_field->setCtrlEnterSubmit(cCtrlEnter() ? CtrlEnterSubmitCtrlEnter : CtrlEnterSubmitEnter);
	}
	updateBoxSize();
	connect(_field, SIGNAL(submitted(bool)), this, SLOT(onSave(bool)));
	connect(_field, SIGNAL(cancelled()), this, SLOT(onClose()));
	connect(_field, SIGNAL(resized()), this, SLOT(onCaptionResized()));

	QTextCursor c(_field->textCursor());
	c.movePosition(QTextCursor::End);
	_field->setTextCursor(c);

	prepare();
}

bool EditCaptionBox::captionFound() const {
	return _animated || _photo || _doc;
}

void EditCaptionBox::onCaptionResized() {
	updateBoxSize();
	resizeEvent(0);
	update();
}

void EditCaptionBox::updateBoxSize() {
	int32 bottomh = st::boxPhotoCompressedPadding.bottom() + _field->height() + st::normalFont->height + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom();
	if (_photo || _animated) {
		setMaxHeight(st::boxPhotoPadding.top() + _thumbh + bottomh);
	} else if (_thumbw) {
		setMaxHeight(st::boxPhotoPadding.top() + 0 + st::msgFileThumbSize + 0 + bottomh);
	} else if (_doc) {
		setMaxHeight(st::boxPhotoPadding.top() + 0 + st::msgFileSize + 0 + bottomh);
	} else {
		setMaxHeight(st::boxPhotoPadding.top() + st::boxTitleFont->height + bottomh);
	}
}

void EditCaptionBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	if (_photo || _animated) {
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
	} else if (_doc) {
		int32 w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		int32 h = _thumbw ? (0 + st::msgFileThumbSize + 0) : (0 + st::msgFileSize + 0);
		int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0;
		if (_thumbw) {
			nameleft = 0 + st::msgFileThumbSize + st::msgFileThumbPadding.right();
			nametop = st::msgFileThumbNameTop - st::msgFileThumbPadding.top();
			nameright = 0;
			statustop = st::msgFileThumbStatusTop - st::msgFileThumbPadding.top();
		} else {
			nameleft = 0 + st::msgFileSize + st::msgFilePadding.right();
			nametop = st::msgFileNameTop - st::msgFilePadding.top();
			nameright = 0;
			statustop = st::msgFileStatusTop - st::msgFilePadding.top();
		}
		int32 namewidth = w - nameleft - 0;
		if (namewidth > _statusw) {
			//w -= (namewidth - _statusw);
			//namewidth = _statusw;
		}
		int32 x = (width() - w) / 2, y = st::boxPhotoPadding.top();

//		App::roundRect(p, x, y, w, h, st::msgInBg, MessageInCorners, &st::msgInShadow);

		if (_thumbw) {
			QRect rthumb(rtlrect(x + 0, y + 0, st::msgFileThumbSize, st::msgFileThumbSize, width()));
			p.drawPixmap(rthumb.topLeft(), _thumb);
		} else {
			QRect inner(rtlrect(x + 0, y + 0, st::msgFileSize, st::msgFileSize, width()));
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgFileInBg);

			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.drawEllipse(inner);
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);

			p.drawSpriteCenter(inner, _isImage ? st::msgFileInImage : st::msgFileInFile);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::black);
		_name.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

		style::color status(st::mediaInFg);
		p.setFont(st::normalFont);
		p.setPen(status);
		p.drawTextLeft(x + nameleft, y + statustop, width(), _status);
	} else {
		p.setFont(st::boxTitleFont);
		p.setPen(st::black);
		p.drawTextLeft(_field->x(), st::boxPhotoPadding.top(), width(), lang(lng_edit_message));
	}

	if (!_error.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::setErrColor);
		p.drawTextLeft(_field->x(), _field->y() + _field->height() + (st::boxButtonPadding.top() / 2), width(), _error);
	}
}

void EditCaptionBox::resizeEvent(QResizeEvent *e) {
	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _save.width() + st::boxButtonPadding.left(), _save.y());
	_field->resize(st::boxWideWidth - st::boxPhotoPadding.left() - st::boxPhotoPadding.right(), _field->height());
	_field->moveToLeft(st::boxPhotoPadding.left(), _save.y() - st::boxButtonPadding.top() - st::normalFont->height - _field->height());
}

void EditCaptionBox::hideAll() {
	_save.hide();
	_cancel.hide();
	_field->hide();
}

void EditCaptionBox::showAll() {
	_save.show();
	_cancel.show();
	_field->show();
}

void EditCaptionBox::showDone() {
	setInnerFocus();
}

void EditCaptionBox::onSave(bool ctrlShiftEnter) {
	if (_saveRequestId) return;

	HistoryItem *item = App::histItemById(_msgId);
	if (!item) {
		_error = lang(lng_edit_deleted);
		update();
		return;
	}

	MTPmessages_EditMessage::Flags flags = MTPmessages_EditMessage::Flag::f_message;
	if (_previewCancelled) {
		flags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	MTPVector<MTPMessageEntity> sentEntities;
	if (!sentEntities.c_vector().v.isEmpty()) {
		flags |= MTPmessages_EditMessage::Flag::f_entities;
	}
	_saveRequestId = MTP::send(MTPmessages_EditMessage(MTP_flags(flags), item->history()->peer->input, MTP_int(item->id), MTP_string(_field->getLastText()), MTPnullMarkup, sentEntities), rpcDone(&EditCaptionBox::saveDone), rpcFail(&EditCaptionBox::saveFail));
}

void EditCaptionBox::saveDone(const MTPUpdates &updates) {
	_saveRequestId = 0;
	onClose();
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
}

bool EditCaptionBox::saveFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	QString err = error.type();
	if (err == qstr("MESSAGE_ID_INVALID") || err == qstr("CHAT_ADMIN_REQUIRED") || err == qstr("MESSAGE_EDIT_TIME_EXPIRED")) {
		_error = lang(lng_edit_error);
	} else if (err == qstr("MESSAGE_NOT_MODIFIED")) {
		onClose();
		return true;
	} else if (err == qstr("MESSAGE_EMPTY")) {
		_field->setFocus();
		_field->showError();
	} else {
		_error = lang(lng_edit_error);
	}
	update();
	return true;
}
