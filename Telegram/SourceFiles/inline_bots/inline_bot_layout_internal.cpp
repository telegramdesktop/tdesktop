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
#include "inline_bots/inline_bot_layout_internal.h"

#include "styles/style_overview.h"
#include "inline_bots/inline_bot_result.h"
#include "localstorage.h"
#include "mainwidget.h"
#include "lang.h"
#include "playerwidget.h"

namespace InlineBots {
namespace Layout {
namespace internal {

FileBase::FileBase(Result *result) : ItemBase(result) {
}

FileBase::FileBase(DocumentData *document) : ItemBase(document) {
}

DocumentData *FileBase::getShownDocument() const {
	if (DocumentData *result = getDocument()) {
		return result;
	}
	return getResultDocument();
}

int FileBase::content_width() const {
	DocumentData *document = getShownDocument();
	if (document->dimensions.width() > 0) {
		return document->dimensions.width();
	}
	if (!document->thumb->isNull()) {
		return convertScale(document->thumb->width());
	}
	return 0;
}

int FileBase::content_height() const {
	DocumentData *document = getShownDocument();
	if (document->dimensions.height() > 0) {
		return document->dimensions.height();
	}
	if (!document->thumb->isNull()) {
		return convertScale(document->thumb->height());
	}
	return 0;
}

int FileBase::content_duration() const {
	if (DocumentData *document = getShownDocument()) {
		if (document->duration() > 0) {
			return document->duration();
		} else if (SongData *song = document->song()) {
			if (song->duration) {
				return song->duration;
			}
		}
	}
	return getResultDuration();
}

ImagePtr FileBase::content_thumb() const {
	if (DocumentData *document = getShownDocument()) {
		if (!document->thumb->isNull()) {
			return document->thumb;
		}
	}
	return getResultThumb();
}

Gif::Gif(Result *result) : FileBase(result) {
}

Gif::Gif(DocumentData *document, bool hasDeleteButton) : FileBase(document)
, _delete(hasDeleteButton ? new DeleteSavedGifClickHandler(document) : nullptr) {
}

void Gif::initDimensions() {
	int32 w = content_width(), h = content_height();
	if (w <= 0 || h <= 0) {
		_maxw = 0;
	} else {
		w = w * st::inlineMediaHeight / h;
		_maxw = qMax(w, int32(st::inlineResultsMinWidth));
	}
	_minh = st::inlineMediaHeight + st::inlineResultsSkip;
}

void Gif::setPosition(int32 position) {
	ItemBase::setPosition(position);
	if (_position < 0) {
		if (gif()) delete _gif;
		_gif = 0;
	}
}

void DeleteSavedGifClickHandler::onClickImpl() const {
	int32 index = cSavedGifs().indexOf(_data);
	if (index >= 0) {
		cRefSavedGifs().remove(index);
		Local::writeSavedGifs();

		MTP::send(MTPmessages_SaveGif(_data->mtpInput(), MTP_bool(true)));
	}
	if (App::main()) emit App::main()->savedGifsUpdated();
}

void Gif::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	DocumentData *document = getShownDocument();
	document->automaticLoad(nullptr);

	bool loaded = document->loaded(), loading = document->loading(), displayLoading = document->displayLoading();
	if (loaded && !gif() && _gif != BadClipReader) {
		Gif *that = const_cast<Gif*>(this);
		that->_gif = new ClipReader(document->location(), document->data(), func(that, &Gif::clipCallback));
		if (gif()) _gif->setAutoplay();
	}

	bool animating = (gif() && _gif->started());
	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(document->progress());
		}
	}
	bool radial = isRadialAnimation(context->ms);

	int32 height = st::inlineMediaHeight;
	QSize frame = countFrameSize();

	QRect r(0, 0, _width, height);
	if (animating) {
		if (!_thumb.isNull()) _thumb = QPixmap();
		p.drawPixmap(r.topLeft(), _gif->current(frame.width(), frame.height(), _width, height, context->paused ? 0 : context->ms));
	} else {
		prepareThumb(_width, height, frame);
		if (_thumb.isNull()) {
			p.fillRect(r, st::overviewPhotoBg);
		} else {
			p.drawPixmap(r.topLeft(), _thumb);
		}
	}

	if (radial || (!_gif && !loaded && !loading) || (_gif == BadClipReader)) {
		float64 radialOpacity = (radial && loaded) ? _animation->radial.opacity() : 1;
		if (_animation && _animation->_a_over.animating(context->ms)) {
			float64 over = _animation->_a_over.current();
			p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
			p.fillRect(r, st::black);
		} else {
			p.fillRect(r, (_state & StateFlag::Over) ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		p.setOpacity(radialOpacity);
		style::sprite icon;
		if (loaded && !radial) {
			icon = st::msgFileInPlay;
		} else if (radial || loading) {
			icon = st::msgFileInCancel;
		} else {
			icon = st::msgFileInDownload;
		}
		QRect inner((_width - st::msgFileSize) / 2, (height - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.drawSpriteCenter(inner, icon);
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, st::msgInBg);
		}
	}

	if (_delete && (_state & StateFlag::Over)) {
		float64 deleteOver = _a_deleteOver.current(context->ms, (_state & StateFlag::DeleteOver) ? 1 : 0);
		QPoint deletePos = QPoint(_width - st::stickerPanDelete.pxWidth(), 0);
		p.setOpacity(deleteOver + (1 - deleteOver) * st::stickerPanDeleteOpacity);
		p.drawSpriteLeft(deletePos, _width, st::stickerPanDelete);
		p.setOpacity(1);
	}
}

void Gif::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	if (x >= 0 && x < _width && y >= 0 && y < st::inlineMediaHeight) {
		if (_delete && (rtl() ? _width - x : x) >= _width - st::stickerPanDelete.pxWidth() && y < st::stickerPanDelete.pxHeight()) {
			link = _delete;
		} else {
			link = _send;
		}
	}
}

void Gif::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!p) return;

	if (_delete && p == _delete) {
		bool wasactive = (_state & StateFlag::DeleteOver);
		if (active != wasactive) {
			auto from = active ? 0. : 1., to = active ? 1. : 0.;
			START_ANIMATION(_a_deleteOver, func(this, &Gif::update), from, to, st::stickersRowDuration, anim::linear);
			if (active) {
				_state |= StateFlag::DeleteOver;
			} else {
				_state &= ~StateFlag::DeleteOver;
			}
		}
	}
	if (p == _delete || p == _send) {
		bool wasactive = (_state & StateFlag::Over);
		if (active != wasactive) {
			if (!getShownDocument()->loaded()) {
				ensureAnimation();
				auto from = active ? 0. : 1., to = active ? 1. : 0.;
				START_ANIMATION(_animation->_a_over, func(this, &Gif::update), from, to, st::stickersRowDuration, anim::linear);
			}
			if (active) {
				_state |= StateFlag::Over;
			} else {
				_state &= ~StateFlag::Over;
			}
		}
	}
	ItemBase::clickHandlerActiveChanged(p, active);
}

QSize Gif::countFrameSize() const {
	bool animating = (gif() && _gif->ready());
	int32 framew = animating ? _gif->width() : content_width(), frameh = animating ? _gif->height() : content_height(), height = st::inlineMediaHeight;
	if (framew * height > frameh * _width) {
		if (framew < st::maxStickerSize || frameh > height) {
			if (frameh > height || (framew * height / frameh) <= st::maxStickerSize) {
				framew = framew * height / frameh;
				frameh = height;
			} else {
				frameh = int32(frameh * st::maxStickerSize) / framew;
				framew = st::maxStickerSize;
			}
		}
	} else {
		if (frameh < st::maxStickerSize || framew > _width) {
			if (framew > _width || (frameh * _width / framew) <= st::maxStickerSize) {
				frameh = frameh * _width / framew;
				framew = _width;
			} else {
				framew = int32(framew * st::maxStickerSize) / frameh;
				frameh = st::maxStickerSize;
			}
		}
	}
	return QSize(framew, frameh);
}

Gif::~Gif() {
	if (gif()) deleteAndMark(_gif);
	deleteAndMark(_animation);
}

void Gif::prepareThumb(int32 width, int32 height, const QSize &frame) const {
	if (DocumentData *document = getShownDocument()) {
		if (!document->thumb->isNull()) {
			if (document->thumb->loaded()) {
				if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
					_thumb = document->thumb->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), ImagePixSmooth, width, height);
				}
			} else {
				document->thumb->load();
			}
		}
	} else {
		ImagePtr thumb = getResultThumb();
		if (!thumb->isNull()) {
			if (thumb->loaded()) {
				if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
					_thumb = thumb->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), ImagePixSmooth, width, height);
				}
			} else {
				thumb->load();
			}
		}
	}
}

void Gif::ensureAnimation() const {
	if (!_animation) {
		_animation = new AnimationData(animation(const_cast<Gif*>(this), &Gif::step_radial));
	}
}

bool Gif::isRadialAnimation(uint64 ms) const {
	if (!_animation || !_animation->radial.animating()) return false;

	_animation->radial.step(ms);
	return _animation && _animation->radial.animating();
}

void Gif::step_radial(uint64 ms, bool timer) {
	if (timer) {
		update();
	} else {
		DocumentData *document = getShownDocument();
		_animation->radial.update(document->progress(), !document->loading() || document->loaded(), ms);
		if (!_animation->radial.animating() && document->loaded()) {
			delete _animation;
			_animation = nullptr;
		}
	}
}

void Gif::clipCallback(ClipReaderNotification notification) {
	switch (notification) {
	case ClipReaderReinit: {
		if (gif()) {
			if (_gif->state() == ClipError) {
				delete _gif;
				_gif = BadClipReader;
				getShownDocument()->forget();
			} else if (_gif->ready() && !_gif->started()) {
				int32 height = st::inlineMediaHeight;
				QSize frame = countFrameSize();
				_gif->start(frame.width(), frame.height(), _width, height, false);
			} else if (_gif->paused() && !Ui::isInlineItemVisible(this)) {
				delete _gif;
				_gif = nullptr;
				getShownDocument()->forget();
			}
		}

		update();
	} break;

	case ClipReaderRepaint: {
		if (gif() && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

Sticker::Sticker(Result *result) : FileBase(result) {
}

void Sticker::initDimensions() {
	_maxw = st::stickerPanSize.width();
	_minh = st::stickerPanSize.height();
}

void Sticker::preload() const {
	if (DocumentData *document = getShownDocument()) {
		bool goodThumb = !document->thumb->isNull() && ((document->thumb->width() >= 128) || (document->thumb->height() >= 128));
		if (goodThumb) {
			document->thumb->load();
		} else {
			document->checkSticker();
		}
	} else {
		ImagePtr thumb = getResultThumb();
		if (!thumb->isNull()) {
			thumb->load();
		}
	}
}

void Sticker::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	bool loaded = getShownDocument()->loaded();

	float64 over = _a_over.isNull() ? (_active ? 1 : 0) : _a_over.current();
	if (over > 0) {
		p.setOpacity(over);
		App::roundRect(p, QRect(QPoint(0, 0), st::stickerPanSize), st::emojiPanHover, StickerHoverCorners);
		p.setOpacity(1);
	}

	prepareThumb();
	if (!_thumb.isNull()) {
		int w = _thumb.width() / cIntRetinaFactor(), h = _thumb.height() / cIntRetinaFactor();
		QPoint pos = QPoint((st::stickerPanSize.width() - w) / 2, (st::stickerPanSize.height() - h) / 2);
		p.drawPixmap(pos, _thumb);
	}
}

void Sticker::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	if (x >= 0 && x < _width && y >= 0 && y < st::inlineMediaHeight) {
		link = _send;
	}
}

void Sticker::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!p) return;

	if (p == _send) {
		if (active != _active) {
			_active = active;

			auto from = active ? 0. : 1., to = active ? 1. : 0.;
			START_ANIMATION(_a_over, func(this, &Sticker::update), from, to, st::stickersRowDuration, anim::linear);
		}
	}
	ItemBase::clickHandlerActiveChanged(p, active);
}

QSize Sticker::getThumbSize() const {
	int width = qMax(content_width(), 1), height = qMax(content_height(), 1);
	float64 coefw = (st::stickerPanSize.width() - st::msgRadius * 2) / float64(width);
	float64 coefh = (st::stickerPanSize.height() - st::msgRadius * 2) / float64(height);
	float64 coef = qMin(qMin(coefw, coefh), 1.);
	int w = qRound(coef * content_width()), h = qRound(coef * content_height());
	return QSize(qMax(w, 1), qMax(h, 1));
}

void Sticker::prepareThumb() const {
	if (DocumentData *document = getShownDocument()) {
		bool goodThumb = !document->thumb->isNull() && ((document->thumb->width() >= 128) || (document->thumb->height() >= 128));
		if (goodThumb) {
			document->thumb->load();
		} else {
			document->checkSticker();
		}

		ImagePtr sticker = goodThumb ? document->thumb : document->sticker()->img;
		if (!_thumbLoaded && sticker->loaded()) {
			QSize thumbSize = getThumbSize();
			_thumb = sticker->pix(thumbSize.width(), thumbSize.height());
			_thumbLoaded = true;
		}
	} else {
		ImagePtr thumb = getResultThumb();
		if (thumb->loaded()) {
			if (!_thumbLoaded) {
				QSize thumbSize = getThumbSize();
				_thumb = thumb->pix(thumbSize.width(), thumbSize.height());
				_thumbLoaded = true;
			}
		} else {
			thumb->load();
		}
	}
}

Photo::Photo(Result *result) : ItemBase(result) {
}

void Photo::initDimensions() {
	PhotoData *photo = getShownPhoto();
	int32 w = photo->full->width(), h = photo->full->height();
	if (w <= 0 || h <= 0) {
		_maxw = 0;
	} else {
		w = w * st::inlineMediaHeight / h;
		_maxw = qMax(w, int32(st::inlineResultsMinWidth));
	}
	_minh = st::inlineMediaHeight + st::inlineResultsSkip;
}

void Photo::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int32 height = st::inlineMediaHeight;
	QSize frame = countFrameSize();

	QRect r(0, 0, _width, height);

	prepareThumb(_width, height, frame);
	if (_thumb.isNull()) {
		p.fillRect(r, st::overviewPhotoBg);
	} else {
		p.drawPixmap(r.topLeft(), _thumb);
	}
}

void Photo::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	if (x >= 0 && x < _width && y >= 0 && y < st::inlineMediaHeight) {
		link = _send;
	}
}

PhotoData *Photo::getShownPhoto() const {
	if (PhotoData *result = getPhoto()) {
		return result;
	}
	return getResultPhoto();
}

QSize Photo::countFrameSize() const {
	PhotoData *photo = getShownPhoto();
	int32 framew = photo->full->width(), frameh = photo->full->height(), height = st::inlineMediaHeight;
	if (framew * height > frameh * _width) {
		if (framew < st::maxStickerSize || frameh > height) {
			if (frameh > height || (framew * height / frameh) <= st::maxStickerSize) {
				framew = framew * height / frameh;
				frameh = height;
			} else {
				frameh = int32(frameh * st::maxStickerSize) / framew;
				framew = st::maxStickerSize;
			}
		}
	} else {
		if (frameh < st::maxStickerSize || framew > _width) {
			if (framew > _width || (frameh * _width / framew) <= st::maxStickerSize) {
				frameh = frameh * _width / framew;
				framew = _width;
			} else {
				framew = int32(framew * st::maxStickerSize) / frameh;
				frameh = st::maxStickerSize;
			}
		}
	}
	return QSize(framew, frameh);
}

void Photo::prepareThumb(int32 width, int32 height, const QSize &frame) const {
	if (PhotoData *photo = getShownPhoto()) {
		if (photo->medium->loaded()) {
			if (!_thumbLoaded || _thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
				_thumb = photo->medium->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), ImagePixSmooth, width, height);
			}
			_thumbLoaded = true;
		} else {
			if (photo->thumb->loaded()) {
				if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
					_thumb = photo->thumb->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), ImagePixSmooth, width, height);
				}
			}
			photo->medium->load();
		}
	} else {
		ImagePtr thumb = getResultThumb();
		if (thumb->loaded()) {
			if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
				_thumb = thumb->pixNoCache(frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), ImagePixSmooth, width, height);
			}
		} else {
			thumb->load();
		}
	}
}

Video::Video(Result *result) : FileBase(result)
, _link(getResultContentUrlHandler())
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
	if (int duration = content_duration()) {
		_duration = formatDurationText(duration);
		_durationWidth = st::normalFont->width(_duration);
	}
}

void Video::initDimensions() {
	bool withThumb = !content_thumb()->isNull();

	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : 0);
	TextParseOptions titleOpts = { 0, _maxw, 2 * st::semiboldFont->height, Qt::LayoutDirectionAuto };
	QString title = textOneLine(_result->getLayoutTitle());
	if (title.isEmpty()) {
		title = lang(lng_media_video);
	}
	_title.setText(st::semiboldFont, title, titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), 2 * st::semiboldFont->height);

	int32 descriptionLines = withThumb ? (titleHeight > st::semiboldFont->height ? 1 : 2) : 3;

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	QString description = _result->getLayoutDescription();
	if (description.isEmpty()) {
		description = _duration;
	}
	_description.setText(st::normalFont, description, descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(_maxw), descriptionLines * st::normalFont->height);

	_minh = st::inlineThumbSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void Video::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int left = st::inlineThumbSize + st::inlineThumbSkip;

	bool withThumb = !content_thumb()->isNull();
	if (withThumb) {
		prepareThumb(st::inlineThumbSize, st::inlineThumbSize);
		if (_thumb.isNull()) {
			p.fillRect(rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width), st::overviewPhotoBg);
		} else {
			p.drawPixmapLeft(0, st::inlineRowMargin, _width, _thumb);
		}
	} else {
		p.fillRect(rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width), st::black);
	}

	if (!_duration.isEmpty()) {
		int durationTop = st::inlineRowMargin + st::inlineThumbSize - st::normalFont->height - st::inlineDurationMargin;
		int durationW = _durationWidth + 2 * st::msgDateImgPadding.x(), durationH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		int durationX = (st::inlineThumbSize - durationW) / 2, durationY = st::inlineRowMargin + st::inlineThumbSize - durationH;
		App::roundRect(p, durationX, durationY - st::msgDateImgPadding.y(), durationW, durationH, st::msgDateImgBg, DateCorners);
		p.setPen(st::black);
		p.setFont(st::normalFont);
		p.drawText(durationX + st::msgDateImgPadding.x(), durationTop + st::normalFont->ascent, _duration);
	}

	p.setPen(st::black);
	_title.drawLeftElided(p, left, st::inlineRowMargin, _width - left, _width, 2);
	int32 titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);

	p.setPen(st::inlineDescriptionFg);
	int32 descriptionLines = withThumb ? (titleHeight > st::semiboldFont->height ? 1 : 2) : 3;
	_description.drawLeftElided(p, left, st::inlineRowMargin + titleHeight, _width - left, _width, descriptionLines);

	if (!context->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

void Video::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	if (x >= 0 && x < st::inlineThumbSize && y >= st::inlineRowMargin && y < st::inlineRowMargin + st::inlineThumbSize) {
		link = _link;
		return;
	}
	if (x >= st::inlineThumbSize + st::inlineThumbSkip && x < _width && y >= 0 && y < _height) {
		link = _send;
		return;
	}
}

void Video::prepareThumb(int32 width, int32 height) const {
	ImagePtr thumb = content_thumb();
	if (thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int32 w = qMax(convertScale(thumb->width()), 1), h = qMax(convertScale(thumb->height()), 1);
			if (w * height > h * width) {
				if (height < h) {
					w = w * height / h;
					h = height;
				}
			} else {
				if (width < w) {
					h = h * width / w;
					w = width;
				}
			}
			_thumb = thumb->pixNoCache(w * cIntRetinaFactor(), h * cIntRetinaFactor(), ImagePixSmooth, width, height);
		}
	} else {
		thumb->load();
	}
}

void OpenFileClickHandler::onClickImpl() const {
	_result->openFile();
}

void CancelFileClickHandler::onClickImpl() const {
	_result->cancelFile();
}

File::File(Result *result) : FileBase(result)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::msgFileSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::msgFileSize - st::inlineThumbSkip)
, _open(new OpenFileClickHandler(result))
, _cancel(new CancelFileClickHandler(result)) {
	updateStatusText();
	regDocumentItem(getShownDocument(), this);
}

void File::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int textWidth = _maxw - (st::msgFileSize + st::inlineThumbSkip);

	TextParseOptions titleOpts = { 0, _maxw, st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldFont, textOneLine(_result->getLayoutTitle()), titleOpts);

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::normalFont, _result->getLayoutDescription(), descriptionOpts);

	_minh = st::msgFileSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void File::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int32 left = st::msgFileSize + st::inlineThumbSkip;

	DocumentData *document = getShownDocument();
	bool loaded = document->loaded(), displayLoading = document->displayLoading();
	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(document->progress());
		}
	}
	bool showPause = updateStatusText();
	bool radial = isRadialAnimation(context->ms);

	QRect iconCircle = rtlrect(0, st::inlineRowMargin, st::msgFileSize, st::msgFileSize, _width);
	p.setPen(Qt::NoPen);
	if (isThumbAnimation(context->ms)) {
		float64 over = _animation->a_thumbOver.current();
		p.setBrush(style::interpolate(st::msgFileInBg, st::msgFileInBgOver, over));
	} else {
		bool over = ClickHandler::showAsActive(document->loading() ? _cancel : _open);
		p.setBrush((over ? st::msgFileInBgOver : st::msgFileInBg));
	}

	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.drawEllipse(iconCircle);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	if (radial) {
		QRect radialCircle(iconCircle.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
		_animation->radial.draw(p, radialCircle, st::msgFileRadialLine, st::msgInBg);
	}

	style::sprite icon;
	if (showPause) {
		icon = st::msgFileInPause;
	} else if (radial || document->loading()) {
		icon = st::msgFileInCancel;
	} else if (true || document->loaded()) {
		if (document->isImage()) {
			icon = st::msgFileInImage;
		} else if (document->voice() || document->song()) {
			icon = st::msgFileInPlay;
		} else {
			icon = st::msgFileInFile;
		}
	} else {
		icon = st::msgFileInDownload;
	}
	p.drawSpriteCenter(iconCircle, icon);

	int titleTop = st::inlineRowMargin + st::inlineRowFileNameTop;
	int descriptionTop = st::inlineRowMargin + st::inlineRowFileDescriptionTop;

	p.setPen(st::black);
	_title.drawLeftElided(p, left, titleTop, _width - left, _width);

	p.setPen(st::inlineDescriptionFg);
	bool drawStatusSize = true;
	if (_statusSize == FileStatusSizeReady || _statusSize == FileStatusSizeLoaded || _statusSize == FileStatusSizeFailed) {
		if (!_description.isEmpty()) {
			_description.drawLeftElided(p, left, descriptionTop, _width - left, _width);
			drawStatusSize = false;
		}
	}
	if (drawStatusSize) {
		p.setFont(st::normalFont);
		p.drawTextLeft(left, descriptionTop, _width, _statusText);
	}

	if (!context->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

void File::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	if (x >= 0 && x < st::msgFileSize && y >= st::inlineRowMargin && y < st::inlineRowMargin + st::msgFileSize) {
		link = getShownDocument()->loading() ? _cancel : _open;
		return;
	}
	if (x >= st::msgFileSize + st::inlineThumbSkip && x < _width && y >= 0 && y < _height) {
		link = _send;
		return;
	}
}

void File::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (p == _open || p == _cancel) {
		if (active) {
			ensureAnimation();
			_animation->a_thumbOver.start(1);
		} else {
			if (!_animation) {
				ensureAnimation();
				_animation->a_thumbOver = anim::fvalue(1, 1);
			}
			_animation->a_thumbOver.start(0);
		}
		_animation->_a_thumbOver.start();
	}
}

File::~File() {
	unregDocumentItem(getShownDocument(), this);
}

void File::step_thumbOver(float64 ms, bool timer) {
	float64 dt = ms / st::msgFileOverDuration;
	if (dt >= 1) {
		_animation->a_thumbOver.finish();
		_animation->_a_thumbOver.stop();
		checkAnimationFinished();
	} else if (!timer) {
		_animation->a_thumbOver.update(dt, anim::linear);
	}
	if (timer) {
		Ui::repaintInlineItem(this);
	}
}

void File::step_radial(uint64 ms, bool timer) {
	if (timer) {
		Ui::repaintInlineItem(this);
	} else {
		DocumentData *document = getShownDocument();
		_animation->radial.update(document->progress(), !document->loading() || document->loaded(), ms);
		if (!_animation->radial.animating()) {
			checkAnimationFinished();
		}
	}
}

void File::ensureAnimation() const {
	if (!_animation) {
		_animation.reset(new AnimationData(
			animation(const_cast<File*>(this), &File::step_thumbOver),
			animation(const_cast<File*>(this), &File::step_radial)));
	}
}

void File::checkAnimationFinished() {
	if (_animation && !_animation->_a_thumbOver.animating() && !_animation->radial.animating()) {
		if (getShownDocument()->loaded()) {
			_animation = nullptr;
		}
	}
}

bool File::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	DocumentData *document = getShownDocument();
	if (document->status == FileDownloadFailed || document->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (document->status == FileUploading) {
		statusSize = document->uploadOffset;
	} else if (document->loading()) {
		statusSize = document->loadOffset();
	} else if (document->loaded()) {
		if (document->voice()) {
			AudioMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			int64 playingPosition = 0, playingDuration = 0;
			int32 playingFrequency = 0;
			if (audioPlayer()) {
				audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
			}

			if (playing == AudioMsgId(document, FullMsgId()) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusSize = FileStatusSizeLoaded;
			}
		} else if (document->song()) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			int64 playingPosition = 0, playingDuration = 0;
			int32 playingFrequency = 0;
			if (audioPlayer()) {
				audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
			}

			if (playing == SongMsgId(document, FullMsgId()) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusSize = FileStatusSizeLoaded;
			}
			if (!showPause && (playing == SongMsgId(document, FullMsgId())) && App::main() && App::main()->player()->seekingSong(playing)) {
				showPause = true;
			}
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		int32 duration = document->song() ? document->song()->duration : (document->voice() ? document->voice()->duration : -1);
		setStatusSize(statusSize, document->size, duration, realDuration);
	}
	return showPause;
}

void File::setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const {
	_statusSize = newSize;
	if (_statusSize == FileStatusSizeReady) {
		_statusText = (duration >= 0) ? formatDurationAndSizeText(duration, fullSize) : (duration < -1 ? formatGifAndSizeText(fullSize) : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeLoaded) {
		_statusText = (duration >= 0) ? formatDurationText(duration) : (duration < -1 ? qsl("GIF") : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeFailed) {
		_statusText = lang(lng_attach_failed);
	} else if (_statusSize >= 0) {
		_statusText = formatDownloadText(_statusSize, fullSize);
	} else {
		_statusText = formatPlayedText(-_statusSize - 1, realDuration);
	}
}

Contact::Contact(Result *result) : ItemBase(result)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
}

void Contact::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (st::inlineThumbSize + st::inlineThumbSkip);
	TextParseOptions titleOpts = { 0, _maxw, st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldFont, textOneLine(_result->getLayoutTitle()), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), st::semiboldFont->height);

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::normalFont, _result->getLayoutDescription(), descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(_maxw), st::normalFont->height);

	_minh = st::msgFileSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

int32 Contact::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	_height = _minh;
	return _height;
}

void Contact::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int32 left = st::emojiPanHeaderLeft - st::inlineResultsLeft;

	left = st::msgFileSize + st::inlineThumbSkip;
	prepareThumb(st::msgFileSize, st::msgFileSize);
	QRect rthumb(rtlrect(0, st::inlineRowMargin, st::msgFileSize, st::msgFileSize, _width));
	p.drawPixmapLeft(rthumb.topLeft(), _width, _thumb);

	int titleTop = st::inlineRowMargin + st::inlineRowFileNameTop;
	int descriptionTop = st::inlineRowMargin + st::inlineRowFileDescriptionTop;

	p.setPen(st::black);
	_title.drawLeftElided(p, left, titleTop, _width - left, _width);

	p.setPen(st::inlineDescriptionFg);
	_description.drawLeftElided(p, left, descriptionTop, _width - left, _width);

	if (!context->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

void Contact::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	int left = (st::msgFileSize + st::inlineThumbSkip);
	if (x >= 0 && x < left - st::inlineThumbSkip && y >= st::inlineRowMargin && y < st::inlineRowMargin + st::inlineThumbSize) {
		return;
	}
	if (x >= left && x < _width && y >= 0 && y < _height) {
		link = _send;
		return;
	}
}

void Contact::prepareThumb(int width, int height) const {
	ImagePtr thumb = getResultThumb();
	if (thumb->isNull()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			_thumb = getResultContactAvatar(width, height);
		}
		return;
	}

	if (thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int w = qMax(convertScale(thumb->width()), 1), h = qMax(convertScale(thumb->height()), 1);
			if (w * height > h * width) {
				if (height < h) {
					w = w * height / h;
					h = height;
				}
			} else {
				if (width < w) {
					h = h * width / w;
					w = width;
				}
			}
			_thumb = thumb->pixNoCache(w * cIntRetinaFactor(), h * cIntRetinaFactor(), ImagePixSmooth, width, height);
		}
	} else {
		thumb->load();
	}
}

Article::Article(Result *result, bool withThumb) : ItemBase(result)
, _url(getResultUrlHandler())
, _link(getResultContentUrlHandler())
, _withThumb(withThumb)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
	LocationCoords location;
	if (!_link && result->getLocationCoords(&location)) {
		_link.reset(new LocationClickHandler(location));
	}
	_thumbLetter = getResultThumbLetter();
}

void Article::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (_withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : 0);
	TextParseOptions titleOpts = { 0, _maxw, 2 * st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldFont, textOneLine(_result->getLayoutTitle()), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), 2 * st::semiboldFont->height);

	int32 descriptionLines = (_withThumb || _url) ? 2 : 3;
	QString description = _result->getLayoutDescription();
	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::normalFont, description, descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(_maxw), descriptionLines * st::normalFont->height);

	_minh = titleHeight + descriptionHeight;
	if (_url) _minh += st::normalFont->height;
	if (_withThumb) _minh = qMax(_minh, int32(st::inlineThumbSize));
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

int32 Article::resizeGetHeight(int32 width) {
	_width = qMin(width, _maxw);
	if (_url) {
		_urlText = getResultUrl();
		_urlWidth = st::normalFont->width(_urlText);
		if (_urlWidth > _width - st::inlineThumbSize - st::inlineThumbSkip) {
			_urlText = st::normalFont->elided(_urlText, _width - st::inlineThumbSize - st::inlineThumbSkip);
			_urlWidth = st::normalFont->width(_urlText);
		}
	}
	_height = _minh;
	return _height;
}

void Article::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int32 left = st::emojiPanHeaderLeft - st::inlineResultsLeft;
	if (_withThumb) {
		left = st::inlineThumbSize + st::inlineThumbSkip;
		prepareThumb(st::inlineThumbSize, st::inlineThumbSize);
		QRect rthumb(rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width));
		if (_thumb.isNull()) {
			ImagePtr thumb = getResultThumb();
			if (thumb->isNull() && !_thumbLetter.isEmpty()) {
				int32 index = (_thumbLetter.at(0).unicode() % 4);
				style::color colors[] = { st::msgFileRedColor, st::msgFileYellowColor, st::msgFileGreenColor, st::msgFileBlueColor };

				p.fillRect(rthumb, colors[index]);
				if (!_thumbLetter.isEmpty()) {
					p.setFont(st::linksLetterFont);
					p.setPen(st::black);
					p.drawText(rthumb, _thumbLetter, style::al_center);
				}
			} else {
				p.fillRect(rthumb, st::overviewPhotoBg);
			}
		} else {
			p.drawPixmapLeft(rthumb.topLeft(), _width, _thumb);
		}
	}

	p.setPen(st::black);
	_title.drawLeftElided(p, left, st::inlineRowMargin, _width - left, _width, 2);
	int32 titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);

	p.setPen(st::inlineDescriptionFg);
	int32 descriptionLines = (_withThumb || _url) ? 2 : 3;
	_description.drawLeftElided(p, left, st::inlineRowMargin + titleHeight, _width - left, _width, descriptionLines);

	if (_url) {
		int32 descriptionHeight = qMin(_description.countHeight(_width - left), st::normalFont->height * descriptionLines);
		p.drawTextLeft(left, st::inlineRowMargin + titleHeight + descriptionHeight, _width, _urlText, _urlWidth);
	}

	if (!context->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

void Article::getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const {
	int left = _withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : 0;
	if (x >= 0 && x < left - st::inlineThumbSkip && y >= st::inlineRowMargin && y < st::inlineRowMargin + st::inlineThumbSize) {
		link = _link;
		return;
	}
	if (x >= left && x < _width && y >= 0 && y < _height) {
		if (_url) {
			int32 left = st::inlineThumbSize + st::inlineThumbSkip;
			int32 titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);
			int32 descriptionLines = 2;
			int32 descriptionHeight = qMin(_description.countHeight(_width - left), st::normalFont->height * descriptionLines);
			if (rtlrect(left, st::inlineRowMargin + titleHeight + descriptionHeight, _urlWidth, st::normalFont->height, _width).contains(x, y)) {
				link = _url;
				return;
			}
		}
		link = _send;
		return;
	}
}

void Article::prepareThumb(int width, int height) const {
	ImagePtr thumb = getResultThumb();
	if (thumb->isNull()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			_thumb = getResultContactAvatar(width, height);
		}
		return;
	}

	if (thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int w = qMax(convertScale(thumb->width()), 1), h = qMax(convertScale(thumb->height()), 1);
			if (w * height > h * width) {
				if (height < h) {
					w = w * height / h;
					h = height;
				}
			} else {
				if (width < w) {
					h = h * width / w;
					w = width;
				}
			}
			_thumb = thumb->pixNoCache(w * cIntRetinaFactor(), h * cIntRetinaFactor(), ImagePixSmooth, width, height);
		}
	} else {
		thumb->load();
	}
}

} // namespace internal
} // namespace Layout
} // namespace InlineBots
