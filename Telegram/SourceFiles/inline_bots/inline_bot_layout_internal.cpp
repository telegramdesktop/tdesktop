/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_layout_internal.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "styles/style_overview.h"
#include "styles/style_history.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"
#include "inline_bots/inline_bot_result.h"
#include "media/media_audio.h"
#include "media/media_clip_reader.h"
#include "media/player/media_player_instance.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_cursor_state.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"

namespace InlineBots {
namespace Layout {
namespace internal {

using TextState = HistoryView::TextState;

FileBase::FileBase(not_null<Context*> context, not_null<Result*> result)
: ItemBase(context, result) {
}

FileBase::FileBase(not_null<Context*> context, DocumentData *document)
: ItemBase(context, document) {
}

DocumentData *FileBase::getShownDocument() const {
	if (const auto result = getDocument()) {
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
		return ConvertScale(document->thumb->width());
	}
	return 0;
}

int FileBase::content_height() const {
	DocumentData *document = getShownDocument();
	if (document->dimensions.height() > 0) {
		return document->dimensions.height();
	}
	if (!document->thumb->isNull()) {
		return ConvertScale(document->thumb->height());
	}
	return 0;
}

int FileBase::content_duration() const {
	if (const auto document = getShownDocument()) {
		if (document->duration() > 0) {
			return document->duration();
		} else if (const auto song = document->song()) {
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

Gif::Gif(not_null<Context*> context, Result *result) : FileBase(context, result) {
}

Gif::Gif(not_null<Context*> context, DocumentData *document, bool hasDeleteButton) : FileBase(context, document) {
	if (hasDeleteButton) {
		_delete = std::make_shared<DeleteSavedGifClickHandler>(document);
	}
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
		_gif.reset();
	}
}

void DeleteSavedGifClickHandler::onClickImpl() const {
	Auth().api().toggleSavedGif(_data, Data::FileOriginSavedGifs(), false);

	const auto index = Auth().data().savedGifs().indexOf(_data);
	if (index >= 0) {
		Auth().data().savedGifsRef().remove(index);
		Local::writeSavedGifs();
	}
	Auth().data().notifySavedGifsUpdated();
}

int Gif::resizeGetHeight(int width) {
	_width = width;
	_height = _minh;
	return _height;
}

void Gif::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	const auto document = getShownDocument();
	document->automaticLoad(fileOrigin(), nullptr);

	bool loaded = document->loaded(), loading = document->loading(), displayLoading = document->displayLoading();
	if (loaded && !_gif && !_gif.isBad()) {
		auto that = const_cast<Gif*>(this);
		that->_gif = Media::Clip::MakeReader(document, FullMsgId(), [that](Media::Clip::Notification notification) {
			that->clipCallback(notification);
		});
		if (_gif) _gif->setAutoplay();
	}

	bool animating = (_gif && _gif->started());
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
		auto pixmap = _gif->current(frame.width(), frame.height(), _width, height, ImageRoundRadius::None, RectPart::None, context->paused ? 0 : context->ms);
		p.drawPixmap(r.topLeft(), pixmap);
	} else {
		prepareThumb(_width, height, frame);
		if (_thumb.isNull()) {
			p.fillRect(r, st::overviewPhotoBg);
		} else {
			p.drawPixmap(r.topLeft(), _thumb);
		}
	}

	if (radial || _gif.isBad() || (!_gif && !loaded && !loading)) {
		auto radialOpacity = (radial && loaded) ? _animation->radial.opacity() : 1.;
		if (_animation && _animation->_a_over.animating(context->ms)) {
			auto over = _animation->_a_over.current();
			p.fillRect(r, anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = (_state & StateFlag::Over);
			p.fillRect(r, over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		p.setOpacity(radialOpacity);
		auto icon = ([loaded, radial, loading] {
			if (loaded && !radial) {
				return &st::historyFileInPlay;
			} else if (radial || loading) {
				return &st::historyFileInCancel;
			}
			return &st::historyFileInDownload;
		})();
		QRect inner((_width - st::msgFileSize) / 2, (height - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		icon->paintInCenter(p, inner);
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, st::historyFileThumbRadialFg);
		}
	}

	if (_delete && (_state & StateFlag::Over)) {
		auto deleteSelected = (_state & StateFlag::DeleteOver);
		auto deletePos = QPoint(_width - st::stickerPanDeleteIconBg.width(), 0);
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityBgOver : st::stickerPanDeleteOpacityBg);
		st::stickerPanDeleteIconBg.paint(p, deletePos, width());
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityFgOver : st::stickerPanDeleteOpacityFg);
		st::stickerPanDeleteIconFg.paint(p, deletePos, width());
		p.setOpacity(1.);
	}
}

TextState Gif::getState(
		QPoint point,
		StateRequest request) const {
	if (QRect(0, 0, _width, st::inlineMediaHeight).contains(point)) {
		if (_delete && rtlpoint(point, _width).x() >= _width - st::stickerPanDeleteIconBg.width() && point.y() < st::stickerPanDeleteIconBg.height()) {
			return { nullptr, _delete };
		} else {
			return { nullptr, _send };
		}
	}
	return {};
}

void Gif::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!p) return;

	if (_delete && p == _delete) {
		bool wasactive = (_state & StateFlag::DeleteOver);
		if (active != wasactive) {
			auto from = active ? 0. : 1., to = active ? 1. : 0.;
			_a_deleteOver.start([this] { update(); }, from, to, st::stickersRowDuration);
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
				_animation->_a_over.start([this] { update(); }, from, to, st::stickersRowDuration);
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
	bool animating = (_gif && _gif->ready());
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

void Gif::prepareThumb(int32 width, int32 height, const QSize &frame) const {
	const auto origin = fileOrigin();
	if (const auto document = getShownDocument()) {
		if (!document->thumb->isNull()) {
			if (document->thumb->loaded()) {
				if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
					_thumb = document->thumb->pixNoCache(origin, frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), Images::Option::Smooth, width, height);
				}
			} else {
				document->thumb->load(origin);
			}
		}
	} else {
		const auto thumb = getResultThumb();
		if (!thumb->isNull()) {
			if (thumb->loaded()) {
				if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
					_thumb = thumb->pixNoCache(origin, frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), Images::Option::Smooth, width, height);
				}
			} else {
				thumb->load(origin);
			}
		}
	}
}

void Gif::ensureAnimation() const {
	if (!_animation) {
		_animation = std::make_unique<AnimationData>(animation(const_cast<Gif*>(this), &Gif::step_radial));
	}
}

bool Gif::isRadialAnimation(TimeMs ms) const {
	if (!_animation || !_animation->radial.animating()) return false;

	_animation->radial.step(ms);
	return _animation && _animation->radial.animating();
}

void Gif::step_radial(TimeMs ms, bool timer) {
	const auto document = getShownDocument();
	const auto updateRadial = [&] {
		return _animation->radial.update(
			document->progress(),
			!document->loading() || document->loaded(),
			ms);
	};
	if (timer) {
		if (!anim::Disabled() || updateRadial()) {
			update();
		}
	} else {
		updateRadial();
		if (!_animation->radial.animating() && document->loaded()) {
			_animation.reset();
		}
	}
}

void Gif::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gif) {
			if (_gif->state() == State::Error) {
				_gif.setBad();
				getShownDocument()->forget();
			} else if (_gif->ready() && !_gif->started()) {
				auto height = st::inlineMediaHeight;
				auto frame = countFrameSize();
				_gif->start(frame.width(), frame.height(), _width, height, ImageRoundRadius::None, RectPart::None);
			} else if (_gif->autoPausedGif() && !context()->inlineItemVisible(this)) {
				_gif.reset();
				getShownDocument()->forget();
			}
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gif && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

Sticker::Sticker(not_null<Context*> context, Result *result)
: FileBase(context, result) {
}

void Sticker::initDimensions() {
	_maxw = st::stickerPanSize.width();
	_minh = st::stickerPanSize.height();
}

void Sticker::preload() const {
	if (const auto document = getShownDocument()) {
		document->checkStickerThumb();
	} else if (const auto thumb = getResultThumb()) {
		thumb->load(fileOrigin());
	}
}

void Sticker::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	bool loaded = getShownDocument()->loaded();

	auto over = _a_over.current(context->ms, _active ? 1. : 0.);
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

TextState Sticker::getState(
		QPoint point,
		StateRequest request) const {
	if (QRect(0, 0, _width, st::inlineMediaHeight).contains(point)) {
		return { nullptr, _send };
	}
	return {};
}

void Sticker::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!p) return;

	if (p == _send) {
		if (active != _active) {
			_active = active;

			auto from = active ? 0. : 1., to = active ? 1. : 0.;
			_a_over.start([this] { update(); }, from, to, st::stickersRowDuration);
		}
	}
	ItemBase::clickHandlerActiveChanged(p, active);
}

QSize Sticker::getThumbSize() const {
	int width = qMax(content_width(), 1), height = qMax(content_height(), 1);
	float64 coefw = (st::stickerPanSize.width() - st::buttonRadius * 2) / float64(width);
	float64 coefh = (st::stickerPanSize.height() - st::buttonRadius * 2) / float64(height);
	float64 coef = qMin(qMin(coefw, coefh), 1.);
	int w = qRound(coef * content_width()), h = qRound(coef * content_height());
	return QSize(qMax(w, 1), qMax(h, 1));
}

void Sticker::prepareThumb() const {
	if (const auto document = getShownDocument()) {
		document->checkStickerThumb();

		const auto sticker = document->getStickerThumb();
		if (!_thumbLoaded && !sticker->isNull() && sticker->loaded()) {
			const auto thumbSize = getThumbSize();
			_thumb = sticker->pix(
				document->stickerSetOrigin(),
				thumbSize.width(),
				thumbSize.height());
			_thumbLoaded = true;
		}
	} else {
		const auto origin = fileOrigin();
		const auto thumb = getResultThumb();
		if (thumb->loaded()) {
			if (!_thumbLoaded) {
				const auto thumbSize = getThumbSize();
				_thumb = thumb->pix(
					origin,
					thumbSize.width(),
					thumbSize.height());
				_thumbLoaded = true;
			}
		} else {
			thumb->load(origin);
		}
	}
}

Photo::Photo(not_null<Context*> context, Result *result)
: ItemBase(context, result) {
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

TextState Photo::getState(
		QPoint point,
		StateRequest request) const {
	if (QRect(0, 0, _width, st::inlineMediaHeight).contains(point)) {
		return { nullptr, _send };
	}
	return {};
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
	const auto origin = fileOrigin();
	if (const auto photo = getShownPhoto()) {
		if (photo->medium->loaded()) {
			if (!_thumbLoaded || _thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
				_thumb = photo->medium->pixNoCache(origin, frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), Images::Option::Smooth, width, height);
			}
			_thumbLoaded = true;
		} else {
			if (photo->thumb->loaded()) {
				if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
					_thumb = photo->thumb->pixNoCache(origin, frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), Images::Option::Smooth, width, height);
				}
			}
			photo->medium->load(origin);
		}
	} else {
		const auto thumb = getResultThumb();
		if (thumb->loaded()) {
			if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
				_thumb = thumb->pixNoCache(origin, frame.width() * cIntRetinaFactor(), frame.height() * cIntRetinaFactor(), Images::Option::Smooth, width, height);
			}
		} else {
			thumb->load(origin);
		}
	}
}

Video::Video(not_null<Context*> context, Result *result) : FileBase(context, result)
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
	auto title = TextUtilities::SingleLine(_result->getLayoutTitle());
	if (title.isEmpty()) {
		title = lang(lng_media_video);
	}
	_title.setText(st::semiboldTextStyle, title, titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), 2 * st::semiboldFont->height);

	int32 descriptionLines = withThumb ? (titleHeight > st::semiboldFont->height ? 1 : 2) : 3;

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	QString description = _result->getLayoutDescription();
	if (description.isEmpty()) {
		description = _duration;
	}
	_description.setText(st::defaultTextStyle, description, descriptionOpts);
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
		p.fillRect(rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width), st::overviewVideoBg);
	}

	if (!_duration.isEmpty()) {
		int durationTop = st::inlineRowMargin + st::inlineThumbSize - st::normalFont->height - st::inlineDurationMargin;
		int durationW = _durationWidth + 2 * st::msgDateImgPadding.x(), durationH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		int durationX = (st::inlineThumbSize - durationW) / 2, durationY = st::inlineRowMargin + st::inlineThumbSize - durationH;
		App::roundRect(p, durationX, durationY - st::msgDateImgPadding.y(), durationW, durationH, st::msgDateImgBg, DateCorners);
		p.setPen(st::msgDateImgFg);
		p.setFont(st::normalFont);
		p.drawText(durationX + st::msgDateImgPadding.x(), durationTop + st::normalFont->ascent, _duration);
	}

	p.setPen(st::inlineTitleFg);
	_title.drawLeftElided(p, left, st::inlineRowMargin, _width - left, _width, 2);
	int32 titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);

	p.setPen(st::inlineDescriptionFg);
	int32 descriptionLines = withThumb ? (titleHeight > st::semiboldFont->height ? 1 : 2) : 3;
	_description.drawLeftElided(p, left, st::inlineRowMargin + titleHeight, _width - left, _width, descriptionLines);

	if (!context->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

TextState Video::getState(
		QPoint point,
		StateRequest request) const {
	if (QRect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize).contains(point)) {
		return { nullptr, _link };
	}
	if (QRect(st::inlineThumbSize + st::inlineThumbSkip, 0, _width - st::inlineThumbSize - st::inlineThumbSkip, _height).contains(point)) {
		return { nullptr, _send };
	}
	return {};
}

void Video::prepareThumb(int32 width, int32 height) const {
	const auto thumb = content_thumb();
	const auto origin = fileOrigin();
	if (thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int32 w = qMax(ConvertScale(thumb->width()), 1), h = qMax(ConvertScale(thumb->height()), 1);
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
			_thumb = thumb->pixNoCache(origin, w * cIntRetinaFactor(), h * cIntRetinaFactor(), Images::Option::Smooth, width, height);
		}
	} else {
		thumb->load(origin);
	}
}

void OpenFileClickHandler::onClickImpl() const {
	_result->openFile();
}

void CancelFileClickHandler::onClickImpl() const {
	_result->cancelFile();
}

File::File(not_null<Context*> context, not_null<Result*> result)
: FileBase(context, result)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::msgFileSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::msgFileSize - st::inlineThumbSkip)
, _open(std::make_shared<OpenFileClickHandler>(result))
, _cancel(std::make_shared<CancelFileClickHandler>(result))
, _document(getShownDocument()) {
	updateStatusText();

	// We have to save document, not read it from Result every time.
	// Because we first delete the Result and then delete this File.
	// So in destructor we have to remember _document, we can't read it.
	regDocumentItem(_document, this);
}

void File::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int textWidth = _maxw - (st::msgFileSize + st::inlineThumbSkip);

	TextParseOptions titleOpts = { 0, _maxw, st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldTextStyle, TextUtilities::SingleLine(_result->getLayoutTitle()), titleOpts);

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::defaultTextStyle, _result->getLayoutDescription(), descriptionOpts);

	_minh = st::msgFileSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void File::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	const auto left = st::msgFileSize + st::inlineThumbSkip;

	const auto loaded = _document->loaded();
	const auto displayLoading = _document->displayLoading();
	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_document->progress());
		}
	}
	bool showPause = updateStatusText();
	bool radial = isRadialAnimation(context->ms);

	auto inner = rtlrect(0, st::inlineRowMargin, st::msgFileSize, st::msgFileSize, _width);
	p.setPen(Qt::NoPen);
	if (isThumbAnimation(context->ms)) {
		auto over = _animation->a_thumbOver.current();
		p.setBrush(anim::brush(st::msgFileInBg, st::msgFileInBgOver, over));
	} else {
		bool over = ClickHandler::showAsActive(_document->loading() ? _cancel : _open);
		p.setBrush(over ? st::msgFileInBgOver : st::msgFileInBg);
	}

	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}

	if (radial) {
		auto radialCircle = inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine));
		_animation->radial.draw(p, radialCircle, st::msgFileRadialLine, st::historyFileInRadialFg);
	}

	auto icon = ([&] {
		if (showPause) {
			return &st::historyFileInPause;
		} else if (radial || _document->loading()) {
			return &st::historyFileInCancel;
		} else if (true || _document->loaded()) {
			if (_document->isImage()) {
				return &st::historyFileInImage;
			} else if (_document->isVoiceMessage()
				|| _document->isAudioFile()) {
				return &st::historyFileInPlay;
			}
			return &st::historyFileInDocument;
		}
		return &st::historyFileInDownload;
	})();
	icon->paintInCenter(p, inner);

	int titleTop = st::inlineRowMargin + st::inlineRowFileNameTop;
	int descriptionTop = st::inlineRowMargin + st::inlineRowFileDescriptionTop;

	p.setPen(st::inlineTitleFg);
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

TextState File::getState(
		QPoint point,
		StateRequest request) const {
	if (QRect(0, st::inlineRowMargin, st::msgFileSize, st::msgFileSize).contains(point)) {
		return { nullptr, _document->loading() ? _cancel : _open };
	} else {
		auto left = st::msgFileSize + st::inlineThumbSkip;
		if (QRect(left, 0, _width - left, _height).contains(point)) {
			return { nullptr, _send };
		}
	}
	return {};
}

void File::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (p == _open || p == _cancel) {
		ensureAnimation();
		_animation->a_thumbOver.start([this] { thumbAnimationCallback(); }, active ? 0. : 1., active ? 1. : 0., st::msgFileOverDuration);
	}
}

File::~File() {
	unregDocumentItem(_document, this);
}

void File::thumbAnimationCallback() {
	update();
}

void File::step_radial(TimeMs ms, bool timer) {
	const auto updateRadial = [&] {
		return _animation->radial.update(
			_document->progress(),
			!_document->loading() || _document->loaded(),
			ms);
	};
	if (timer) {
		if (!anim::Disabled() || updateRadial()) {
			update();
		}
	} else {
		updateRadial();
		if (!_animation->radial.animating()) {
			checkAnimationFinished();
		}
	}
}

void File::ensureAnimation() const {
	if (!_animation) {
		_animation = std::make_unique<AnimationData>(animation(const_cast<File*>(this), &File::step_radial));
	}
}

void File::checkAnimationFinished() const {
	if (_animation
		&& !_animation->a_thumbOver.animating()
		&& !_animation->radial.animating()) {
		if (_document->loaded()) {
			_animation.reset();
		}
	}
}

bool File::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_document->status == FileDownloadFailed || _document->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_document->uploading()) {
		statusSize = _document->uploadingData->offset;
	} else if (_document->loading()) {
		statusSize = _document->loadOffset();
	} else if (_document->loaded()) {
		using State = Media::Player::State;
		if (_document->isVoiceMessage()) {
			statusSize = FileStatusSizeLoaded;
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(_document, FullMsgId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.length / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			}
		} else if (_document->isAudioFile()) {
			statusSize = FileStatusSizeLoaded;
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(_document, FullMsgId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.length / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			}
			if (!showPause && (state.id == AudioMsgId(_document, FullMsgId())) && Media::Player::instance()->isSeeking(AudioMsgId::Type::Song)) {
				showPause = true;
			}
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		int32 duration = _document->isSong()
			? _document->song()->duration
			: (_document->isVoiceMessage()
				? _document->voice()->duration
				: -1);
		setStatusSize(statusSize, _document->size, duration, realDuration);
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

Contact::Contact(not_null<Context*> context, Result *result) : ItemBase(context, result)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
}

void Contact::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (st::inlineThumbSize + st::inlineThumbSkip);
	TextParseOptions titleOpts = { 0, _maxw, st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldTextStyle, TextUtilities::SingleLine(_result->getLayoutTitle()), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), st::semiboldFont->height);

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::defaultTextStyle, _result->getLayoutDescription(), descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(_maxw), st::normalFont->height);

	_minh = st::msgFileSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void Contact::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int32 left = st::emojiPanHeaderLeft - st::inlineResultsLeft;

	left = st::msgFileSize + st::inlineThumbSkip;
	prepareThumb(st::msgFileSize, st::msgFileSize);
	QRect rthumb(rtlrect(0, st::inlineRowMargin, st::msgFileSize, st::msgFileSize, _width));
	p.drawPixmapLeft(rthumb.topLeft(), _width, _thumb);

	int titleTop = st::inlineRowMargin + st::inlineRowFileNameTop;
	int descriptionTop = st::inlineRowMargin + st::inlineRowFileDescriptionTop;

	p.setPen(st::inlineTitleFg);
	_title.drawLeftElided(p, left, titleTop, _width - left, _width);

	p.setPen(st::inlineDescriptionFg);
	_description.drawLeftElided(p, left, descriptionTop, _width - left, _width);

	if (!context->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

TextState Contact::getState(
		QPoint point,
		StateRequest request) const {
	if (!QRect(0, st::inlineRowMargin, st::msgFileSize, st::inlineThumbSize).contains(point)) {
		auto left = (st::msgFileSize + st::inlineThumbSkip);
		if (QRect(left, 0, _width - left, _height).contains(point)) {
			return { nullptr, _send };
		}
	}
	return {};
}

void Contact::prepareThumb(int width, int height) const {
	const auto thumb = getResultThumb();
	if (thumb->isNull()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			_thumb = getResultContactAvatar(width, height);
		}
		return;
	}

	const auto origin = fileOrigin();
	if (thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int w = qMax(ConvertScale(thumb->width()), 1), h = qMax(ConvertScale(thumb->height()), 1);
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
			_thumb = thumb->pixNoCache(origin, w * cIntRetinaFactor(), h * cIntRetinaFactor(), Images::Option::Smooth, width, height);
		}
	} else {
		thumb->load(origin);
	}
}

Article::Article(not_null<Context*> context, Result *result, bool withThumb) : ItemBase(context, result)
, _url(getResultUrlHandler())
, _link(getResultContentUrlHandler())
, _withThumb(withThumb)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
	LocationCoords location;
	if (!_link && result->getLocationCoords(&location)) {
		_link = std::make_shared<LocationClickHandler>(location);
	}
	_thumbLetter = getResultThumbLetter();
}

void Article::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (_withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : 0);
	TextParseOptions titleOpts = { 0, _maxw, 2 * st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldTextStyle, TextUtilities::SingleLine(_result->getLayoutTitle()), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), 2 * st::semiboldFont->height);

	int32 descriptionLines = (_withThumb || _url) ? 2 : 3;
	QString description = _result->getLayoutDescription();
	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::defaultTextStyle, description, descriptionOpts);
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
				style::color colors[] = {
					st::msgFile3Bg,
					st::msgFile4Bg,
					st::msgFile2Bg,
					st::msgFile1Bg
				};

				p.fillRect(rthumb, colors[index]);
				if (!_thumbLetter.isEmpty()) {
					p.setFont(st::linksLetterFont);
					p.setPen(st::linksLetterFg);
					p.drawText(rthumb, _thumbLetter, style::al_center);
				}
			} else {
				p.fillRect(rthumb, st::overviewPhotoBg);
			}
		} else {
			p.drawPixmapLeft(rthumb.topLeft(), _width, _thumb);
		}
	}

	p.setPen(st::inlineTitleFg);
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

TextState Article::getState(
		QPoint point,
		StateRequest request) const {
	if (_withThumb && QRect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize).contains(point)) {
		return { nullptr, _link };
	}
	auto left = _withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : 0;
	if (QRect(left, 0, _width - left, _height).contains(point)) {
		if (_url) {
			auto left = st::inlineThumbSize + st::inlineThumbSkip;
			auto titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);
			auto descriptionLines = 2;
			auto descriptionHeight = qMin(_description.countHeight(_width - left), st::normalFont->height * descriptionLines);
			if (rtlrect(left, st::inlineRowMargin + titleHeight + descriptionHeight, _urlWidth, st::normalFont->height, _width).contains(point)) {
				return { nullptr, _url };
			}
		}
		return { nullptr, _send };
	}
	return {};
}

void Article::prepareThumb(int width, int height) const {
	const auto thumb = getResultThumb();
	if (thumb->isNull()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			_thumb = getResultContactAvatar(width, height);
		}
		return;
	}

	const auto origin = fileOrigin();
	if (thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int w = qMax(ConvertScale(thumb->width()), 1), h = qMax(ConvertScale(thumb->height()), 1);
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
			_thumb = thumb->pixNoCache(origin, w * cIntRetinaFactor(), h * cIntRetinaFactor(), Images::Option::Smooth, width, height);
		}
	} else {
		thumb->load(origin);
	}
}

Game::Game(not_null<Context*> context, Result *result) : ItemBase(context, result)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
	countFrameSize();
}

void Game::countFrameSize() {
	if (auto document = getResultDocument()) {
		if (document->isAnimation()) {
			auto documentSize = document->dimensions;
			if (documentSize.isEmpty()) {
				documentSize = QSize(st::inlineThumbSize, st::inlineThumbSize);
			}
			auto resizeByHeight1 = (documentSize.width() > documentSize.height()) && (documentSize.height() >= st::inlineThumbSize);
			auto resizeByHeight2 = (documentSize.height() >= documentSize.width()) && (documentSize.width() < st::inlineThumbSize);
			if (resizeByHeight1 || resizeByHeight2) {
				if (documentSize.height() > st::inlineThumbSize) {
					_frameSize = QSize((documentSize.width() * st::inlineThumbSize) / documentSize.height(), st::inlineThumbSize);
				}
			} else {
				if (documentSize.width() > st::inlineThumbSize) {
					_frameSize = QSize(st::inlineThumbSize, (documentSize.height() * st::inlineThumbSize) / documentSize.width());
				}
			}
			if (!_frameSize.width()) {
				_frameSize.setWidth(1);
			}
			if (!_frameSize.height()) {
				_frameSize.setHeight(1);
			}
		}
	}
}

void Game::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (st::inlineThumbSize + st::inlineThumbSkip);
	TextParseOptions titleOpts = { 0, _maxw, 2 * st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldTextStyle, TextUtilities::SingleLine(_result->getLayoutTitle()), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(_maxw), 2 * st::semiboldFont->height);

	int32 descriptionLines = 2;
	QString description = _result->getLayoutDescription();
	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::defaultTextStyle, description, descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(_maxw), descriptionLines * st::normalFont->height);

	_minh = titleHeight + descriptionHeight;
	accumulate_max(_minh, st::inlineThumbSize);
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void Game::setPosition(int32 position) {
	ItemBase::setPosition(position);
	if (_position < 0) {
		_gif.reset();
	}
}

void Game::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int32 left = st::emojiPanHeaderLeft - st::inlineResultsLeft;

	left = st::inlineThumbSize + st::inlineThumbSkip;
	auto rthumb = rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width);

	// Gif thumb
	auto thumbDisplayed = false, radial = false;
	auto document = getResultDocument();
	auto animatedThumb = document && document->isAnimation();
	if (animatedThumb) {
		document->automaticLoad(fileOrigin(), nullptr);

		bool loaded = document->loaded(), loading = document->loading(), displayLoading = document->displayLoading();
		if (loaded && !_gif && !_gif.isBad()) {
			auto that = const_cast<Game*>(this);
			that->_gif = Media::Clip::MakeReader(document, FullMsgId(), [that](Media::Clip::Notification notification) {
				that->clipCallback(notification);
			});
			if (_gif) _gif->setAutoplay();
		}

		bool animating = (_gif && _gif->started());
		if (displayLoading) {
			if (!_radial) {
				_radial = std::make_unique<Ui::RadialAnimation>(animation(const_cast<Game*>(this), &Game::step_radial));
			}
			if (!_radial->animating()) {
				_radial->start(document->progress());
			}
		}
		radial = isRadialAnimation(context->ms);

		if (animating) {
			if (!_thumb.isNull()) _thumb = QPixmap();
			auto animationThumb = _gif->current(_frameSize.width(), _frameSize.height(), st::inlineThumbSize, st::inlineThumbSize, ImageRoundRadius::None, RectPart::None, context->paused ? 0 : context->ms);
			p.drawPixmapLeft(rthumb.topLeft(), _width, animationThumb);
			thumbDisplayed = true;
		}
	}

	if (!thumbDisplayed) {
		prepareThumb(st::inlineThumbSize, st::inlineThumbSize);
		if (_thumb.isNull()) {
			p.fillRect(rthumb, st::overviewPhotoBg);
		} else {
			p.drawPixmapLeft(rthumb.topLeft(), _width, _thumb);
		}
	}

	if (radial) {
		p.fillRect(rthumb, st::msgDateImgBg);
		QRect inner((st::inlineThumbSize - st::msgFileSize) / 2, (st::inlineThumbSize - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_radial->draw(p, rinner, st::msgFileRadialLine, st::historyFileThumbRadialFg);
		}
	}

	p.setPen(st::inlineTitleFg);
	_title.drawLeftElided(p, left, st::inlineRowMargin, _width - left, _width, 2);
	int32 titleHeight = qMin(_title.countHeight(_width - left), st::semiboldFont->height * 2);

	p.setPen(st::inlineDescriptionFg);
	int32 descriptionLines = 2;
	_description.drawLeftElided(p, left, st::inlineRowMargin + titleHeight, _width - left, _width, descriptionLines);

	if (!context->lastRow) {
		p.fillRect(rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

TextState Game::getState(
		QPoint point,
		StateRequest request) const {
	int left = st::inlineThumbSize + st::inlineThumbSkip;
	if (QRect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize).contains(point)) {
		return { nullptr, _send };
	}
	if (QRect(left, 0, _width - left, _height).contains(point)) {
		return { nullptr, _send };
	}
	return {};
}

void Game::prepareThumb(int width, int height) const {
	const auto thumb = [this] {
		if (auto photo = getResultPhoto()) {
			return photo->medium;
		} else if (auto document = getResultDocument()) {
			return document->thumb;
		}
		return ImagePtr();
	}();
	if (thumb->isNull()) {
		return;
	}

	const auto origin = fileOrigin();
	if (thumb->loaded()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			int w = qMax(ConvertScale(thumb->width()), 1), h = qMax(ConvertScale(thumb->height()), 1);
			auto resizeByHeight1 = (w * height > h * width) && (h >= height);
			auto resizeByHeight2 = (h * width >= w * height) && (w < width);
			if (resizeByHeight1 || resizeByHeight2) {
				if (h > height) {
					w = w * height / h;
					h = height;
				}
			} else {
				if (w > width) {
					h = h * width / w;
					w = width;
				}
			}
			_thumb = thumb->pixNoCache(origin, w * cIntRetinaFactor(), h * cIntRetinaFactor(), Images::Option::Smooth, width, height);
		}
	} else {
		thumb->load(origin);
	}
}

bool Game::isRadialAnimation(TimeMs ms) const {
	if (!_radial || !_radial->animating()) return false;

	_radial->step(ms);
	return _radial && _radial->animating();
}

void Game::step_radial(TimeMs ms, bool timer) {
	const auto document = getResultDocument();
	const auto updateRadial = [&] {
		return _radial->update(
			document->progress(),
			!document->loading() || document->loaded(),
			ms);
	};
	if (timer) {
		if (!anim::Disabled() || updateRadial()) {
			update();
		}
	} else {
		updateRadial();
		if (!_radial->animating() && document->loaded()) {
			_radial.reset();
		}
	}
}

void Game::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gif) {
			if (_gif->state() == State::Error) {
				_gif.setBad();
				getResultDocument()->forget();
			} else if (_gif->ready() && !_gif->started()) {
				_gif->start(_frameSize.width(), _frameSize.height(), st::inlineThumbSize, st::inlineThumbSize, ImageRoundRadius::None, RectPart::None);
			} else if (_gif->autoPausedGif() && !context()->inlineItemVisible(this)) {
				_gif.reset();
				getResultDocument()->forget();
			}
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gif && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

} // namespace internal
} // namespace Layout
} // namespace InlineBots
