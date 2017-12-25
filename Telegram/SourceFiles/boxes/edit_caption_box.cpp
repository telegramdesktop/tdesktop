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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/edit_caption_box.h"

#include "ui/widgets/input_fields.h"
#include "media/media_clip_reader.h"
#include "history/history_media_types.h"
#include "lang/lang_keys.h"
#include "window/window_controller.h"
#include "mainwidget.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"

EditCaptionBox::EditCaptionBox(
	QWidget*,
	not_null<HistoryMedia*> media,
	FullMsgId msgId)
: _msgId(msgId) {
	Expects(media->canEditCaption());

	QSize dimensions;
	ImagePtr image;
	QString caption;
	DocumentData *doc = nullptr;

	switch (media->type()) {
	case MediaTypeGif: {
		_animated = true;
		doc = static_cast<HistoryGif*>(media.get())->getDocument();
		dimensions = doc->dimensions;
		image = doc->thumb;
	} break;

	case MediaTypePhoto: {
		_photo = true;
		auto photo = static_cast<HistoryPhoto*>(media.get())->getPhoto();
		dimensions = QSize(photo->full->width(), photo->full->height());
		image = photo->full;
	} break;

	case MediaTypeVideo: {
		_animated = true;
		doc = static_cast<HistoryVideo*>(media.get())->getDocument();
		dimensions = doc->dimensions;
		image = doc->thumb;
	} break;

	case MediaTypeGrouped: {
		if (const auto photo = media->getPhoto()) {
			dimensions = QSize(photo->full->width(), photo->full->height());
			image = photo->full;
			_photo = true;
		} else if (const auto doc = media->getDocument()) {
			dimensions = doc->dimensions;
			image = doc->thumb;
			_animated = true;
		}
	} break;

	case MediaTypeFile:
	case MediaTypeMusicFile:
	case MediaTypeVoiceFile: {
		_doc = true;
		doc = static_cast<HistoryDocument*>(media.get())->getDocument();
		image = doc->thumb;
	} break;
	}
	caption = media->getCaption().text;

	if (!_animated && (dimensions.isEmpty() || doc || image->isNull())) {
		if (image->isNull()) {
			_thumbw = 0;
		} else {
			int32 tw = image->width(), th = image->height();
			if (tw > th) {
				_thumbw = (tw * st::msgFileThumbSize) / th;
			} else {
				_thumbw = st::msgFileThumbSize;
			}
			auto options = Images::Option::Smooth | Images::Option::RoundedSmall | Images::Option::RoundedTopLeft | Images::Option::RoundedTopRight | Images::Option::RoundedBottomLeft | Images::Option::RoundedBottomRight;
			_thumb = Images::pixmap(image->pix().toImage(), _thumbw * cIntRetinaFactor(), 0, options, st::msgFileThumbSize, st::msgFileThumbSize);
		}

		if (doc) {
			auto nameString = doc->isVoiceMessage()
				? lang(lng_media_audio)
				: doc->composeNameString();
			_name.setText(
				st::semiboldTextStyle,
				nameString,
				_textNameOptions);
			_status = formatSizeText(doc->size);
			_statusw = qMax(
				_name.maxWidth(),
				st::normalFont->width(_status));
			_isImage = doc->isImage();
			_isAudio = (doc->isVoiceMessage() || doc->isAudioFile());
		}
	} else {
		int32 maxW = 0, maxH = 0;
		if (_animated) {
			int32 limitW = st::sendMediaPreviewSize;
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
			_thumb = image->pixNoCache(maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), Images::Option::Smooth | Images::Option::Blurred, maxW, maxH);
			prepareGifPreview(doc);
		} else {
			maxW = dimensions.width();
			maxH = dimensions.height();
			_thumb = image->pixNoCache(maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), Images::Option::Smooth, maxW, maxH);
		}
		int32 tw = _thumb.width(), th = _thumb.height();
		if (!tw || !th) {
			tw = th = 1;
		}
		_thumbw = st::sendMediaPreviewSize;
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
		_thumbx = (st::boxWideWidth - _thumbw) / 2;

		_thumb = App::pixmapFromImageInPlace(_thumb.toImage().scaled(_thumbw * cIntRetinaFactor(), _thumbh * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		_thumb.setDevicePixelRatio(cRetinaFactor());
	}
	Assert(_animated || _photo || _doc);

	_field.create(this, st::confirmCaptionArea, langFactory(lng_photo_caption), caption);
	_field->setMaxLength(MaxPhotoCaption);
	_field->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
}

void EditCaptionBox::prepareGifPreview(DocumentData *document) {
	auto createGifPreview = [document] {
		return (document && document->isAnimation());
	};
	auto createGifPreviewResult = createGifPreview(); // Clang freeze workaround.
	if (createGifPreviewResult) {
		_gifPreview = Media::Clip::MakeReader(document, _msgId, [this](Media::Clip::Notification notification) {
			clipCallback(notification);
		});
		if (_gifPreview) _gifPreview->setAutoplay();
	}
}

void EditCaptionBox::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gifPreview && _gifPreview->state() == State::Error) {
			_gifPreview.setBad();
		}

		if (_gifPreview && _gifPreview->ready() && !_gifPreview->started()) {
			auto s = QSize(_thumbw, _thumbh);
			_gifPreview->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gifPreview && !_gifPreview->currentDisplayed()) {
			update();
		}
	} break;
	}
}

void EditCaptionBox::prepare() {
	addButton(langFactory(lng_settings_save), [this] { save(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	updateBoxSize();
	connect(_field, &Ui::InputArea::submitted, this, [this] { save(); });
	connect(_field, &Ui::InputArea::cancelled, this, [this] {
		closeBox();
	});
	connect(_field, &Ui::InputArea::resized, this, [this] {
		captionResized();
	});

	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);
}

void EditCaptionBox::captionResized() {
	updateBoxSize();
	resizeEvent(0);
	update();
}

void EditCaptionBox::updateBoxSize() {
	auto newHeight = st::boxPhotoPadding.top() + st::boxPhotoCaptionSkip + _field->height() + errorTopSkip() + st::normalFont->height;
	if (_photo || _animated) {
		newHeight += _thumbh;
	} else if (_thumbw) {
		newHeight += 0 + st::msgFileThumbSize + 0;
	} else if (_doc) {
		newHeight += 0 + st::msgFileSize + 0;
	} else {
		newHeight += st::boxTitleFont->height;
	}
	setDimensions(st::boxWideWidth, newHeight);
}

int EditCaptionBox::errorTopSkip() const {
	return (st::boxButtonPadding.top() / 2);
}

void EditCaptionBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_photo || _animated) {
		if (_thumbx > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _thumbx - st::boxPhotoPadding.left(), _thumbh, st::confirmBg);
		}
		if (_thumbx + _thumbw < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_thumbx + _thumbw, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _thumbx - _thumbw, _thumbh, st::confirmBg);
		}
		if (_gifPreview && _gifPreview->started()) {
			auto s = QSize(_thumbw, _thumbh);
			auto paused = controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
			auto frame = _gifPreview->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : getms());
			p.drawPixmap(_thumbx, st::boxPhotoPadding.top(), frame);
		} else {
			p.drawPixmap(_thumbx, st::boxPhotoPadding.top(), _thumb);
		}
		if (_animated && !_gifPreview) {
			QRect inner(_thumbx + (_thumbw - st::msgFileSize) / 2, st::boxPhotoPadding.top() + (_thumbh - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgDateImgBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			auto icon = &st::historyFileInPlay;
			icon->paintInCenter(p, inner);
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

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			auto icon = &(_isAudio ? st::historyFileInPlay : _isImage ? st::historyFileInImage : st::historyFileInDocument);
			icon->paintInCenter(p, inner);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

		auto &status = st::mediaInFg;
		p.setFont(st::normalFont);
		p.setPen(status);
		p.drawTextLeft(x + nameleft, y + statustop, width(), _status);
	} else {
		p.setFont(st::boxTitleFont);
		p.setPen(st::boxTextFg);
		p.drawTextLeft(_field->x(), st::boxPhotoPadding.top(), width(), lang(lng_edit_message));
	}

	if (!_error.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::boxTextFgError);
		p.drawTextLeft(_field->x(), _field->y() + _field->height() + errorTopSkip(), width(), _error);
	}
}

void EditCaptionBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_field->resize(st::sendMediaPreviewSize, _field->height());
	_field->moveToLeft(st::boxPhotoPadding.left(), height() - st::normalFont->height - errorTopSkip() - _field->height());
}

void EditCaptionBox::setInnerFocus() {
	_field->setFocusFast();
}

void EditCaptionBox::save() {
	if (_saveRequestId) return;

	auto item = App::histItemById(_msgId);
	if (!item) {
		_error = lang(lng_edit_deleted);
		update();
		return;
	}

	auto flags = MTPmessages_EditMessage::Flag::f_message | 0;
	if (_previewCancelled) {
		flags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	MTPVector<MTPMessageEntity> sentEntities;
	if (!sentEntities.v.isEmpty()) {
		flags |= MTPmessages_EditMessage::Flag::f_entities;
	}
	auto text = TextUtilities::PrepareForSending(_field->getLastText(), TextUtilities::PrepareTextOption::CheckLinks);
	_saveRequestId = MTP::send(
		MTPmessages_EditMessage(
			MTP_flags(flags),
			item->history()->peer->input,
			MTP_int(item->id),
			MTP_string(text),
			MTPnullMarkup,
			sentEntities,
			MTP_inputGeoPointEmpty()),
		rpcDone(&EditCaptionBox::saveDone),
		rpcFail(&EditCaptionBox::saveFail));
}

void EditCaptionBox::saveDone(const MTPUpdates &updates) {
	_saveRequestId = 0;
	closeBox();
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
		closeBox();
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
