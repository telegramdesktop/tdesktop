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
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/stickers/data_stickers.h"
#include "chat_helpers/gifs_list_widget.h" // ChatHelpers::AddGifAction
#include "chat_helpers/stickers_lottie.h"
#include "inline_bots/inline_bot_result.h"
#include "lottie/lottie_single_player.h"
#include "media/audio/media_audio.h"
#include "media/clip/media_clip_reader.h"
#include "media/player/media_player_instance.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_document.h" // DrawThumbnailAsSongCover
#include "ui/image/image.h"
#include "ui/text/format_values.h"
#include "ui/cached_round_corners.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "styles/style_overview.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"

namespace InlineBots {
namespace Layout {
namespace internal {

using TextState = HistoryView::TextState;

constexpr auto kMaxInlineArea = 1280 * 720;

[[nodiscard]] bool CanPlayInline(not_null<DocumentData*> document) {
	const auto dimensions = document->dimensions;
	return dimensions.width() * dimensions.height() <= kMaxInlineArea;
}

FileBase::FileBase(not_null<Context*> context, not_null<Result*> result)
: ItemBase(context, result) {
}

FileBase::FileBase(
	not_null<Context*> context,
	not_null<DocumentData*> document)
: ItemBase(context, document) {
}

DocumentData *FileBase::getShownDocument() const {
	if (const auto result = getDocument()) {
		return result;
	}
	return getResultDocument();
}

int FileBase::content_width() const {
	if (const auto document = getShownDocument()) {
		if (document->dimensions.width() > 0) {
			return document->dimensions.width();
		}
		return style::ConvertScale(document->thumbnailLocation().width());
	}
	return 0;
}

int FileBase::content_height() const {
	if (const auto document = getShownDocument()) {
		if (document->dimensions.height() > 0) {
			return document->dimensions.height();
		}
		return style::ConvertScale(document->thumbnailLocation().height());
	}
	return 0;
}

int FileBase::content_duration() const {
	if (const auto document = getShownDocument()) {
		if (document->getDuration() > 0) {
			return document->getDuration();
		}
	}
	return getResultDuration();
}

Gif::Gif(not_null<Context*> context, not_null<Result*> result)
: FileBase(context, result) {
	Expects(getResultDocument() != nullptr);
}

Gif::Gif(
	not_null<Context*> context,
	not_null<DocumentData*> document,
	bool hasDeleteButton)
: FileBase(context, document) {
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
	ChatHelpers::AddGifAction(
		[](QString, Fn<void()> &&done) { done(); },
		_data);
}

int Gif::resizeGetHeight(int width) {
	_width = width;
	_height = _minh;
	return _height;
}

void Gif::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	const auto document = getShownDocument();
	ensureDataMediaCreated(document);
	const auto preview = Data::VideoPreviewState(_dataMedia.get());
	preview.automaticLoad(fileOrigin());

	const auto displayLoading = !preview.usingThumbnail()
		&& document->displayLoading();
	const auto loaded = preview.loaded();
	const auto loading = preview.loading();
	if (loaded
		&& !_gif
		&& !_gif.isBad()
		&& CanPlayInline(document)) {
		auto that = const_cast<Gif*>(this);
		that->_gif = preview.makeAnimation([=](
				Media::Clip::Notification notification) {
			that->clipCallback(notification);
		});
	}

	const auto animating = (_gif && _gif->started());
	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_dataMedia->progress());
		}
	}
	const auto radial = isRadialAnimation();

	int32 height = st::inlineMediaHeight;
	QSize frame = countFrameSize();

	QRect r(0, 0, _width, height);
	if (animating) {
		const auto pixmap = _gif->current(frame.width(), frame.height(), _width, height, ImageRoundRadius::None, RectPart::None, context->paused ? 0 : context->ms);
		if (_thumb.isNull()) {
			_thumb = pixmap;
			_thumbGood = true;
		}
		p.drawPixmap(r.topLeft(), pixmap);
	} else {
		prepareThumbnail({ _width, height }, frame);
		if (_thumb.isNull()) {
			p.fillRect(r, st::overviewPhotoBg);
		} else {
			p.drawPixmap(r.topLeft(), _thumb);
		}
	}

	if (radial
		|| _gif.isBad()
		|| (!_gif && !loaded && !loading && !preview.usingThumbnail())) {
		auto radialOpacity = (radial && loaded) ? _animation->radial.opacity() : 1.;
		if (_animation && _animation->_a_over.animating()) {
			auto over = _animation->_a_over.value(1.);
			p.fillRect(r, anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = (_state & StateFlag::Over);
			p.fillRect(r, over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		p.setOpacity(radialOpacity);
		auto icon = [&] {
			if (radial || loading) {
				return &st::historyFileInCancel;
			} else if (loaded) {
				return &st::historyFileInPlay;
			}
			return &st::historyFileInDownload;
		}();
		const auto size = st::inlineRadialSize;
		QRect inner((_width - size) / 2, (height - size) / 2, size, size);
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
		if (_delete && style::rtlpoint(point, _width).x() >= _width - st::stickerPanDeleteIconBg.width() && point.y() < st::stickerPanDeleteIconBg.height()) {
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
			ensureDataMediaCreated(getShownDocument());
			const auto preview = Data::VideoPreviewState(_dataMedia.get());
			if (!preview.usingThumbnail() && !preview.loaded()) {
				ensureAnimation();
				auto from = active ? 0. : 1., to = active ? 1. : 0.;
				_animation->_a_over.start([=] { update(); }, from, to, st::stickersRowDuration);
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

void Gif::validateThumbnail(
		Image *image,
		QSize size,
		QSize frame,
		bool good) const {
	if (!image || (_thumbGood && !good)) {
		return;
	} else if ((_thumb.size() == size * cIntRetinaFactor())
		&& (_thumbGood || !good)) {
		return;
	}
	_thumbGood = good;
	_thumb = image->pixNoCache(
		frame.width() * cIntRetinaFactor(),
		frame.height() * cIntRetinaFactor(),
		(Images::Option::Smooth
			| (good ? Images::Option::None : Images::Option::Blurred)),
		size.width(),
		size.height());
}

void Gif::prepareThumbnail(QSize size, QSize frame) const {
	const auto document = getShownDocument();
	Assert(document != nullptr);

	ensureDataMediaCreated(document);
	validateThumbnail(_dataMedia->thumbnail(), size, frame, true);
	validateThumbnail(_dataMedia->thumbnailInline(), size, frame, false);
}

void Gif::ensureDataMediaCreated(not_null<DocumentData*> document) const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = document->createMediaView();
	_dataMedia->thumbnailWanted(fileOrigin());
	_dataMedia->videoThumbnailWanted(fileOrigin());
}

void Gif::ensureAnimation() const {
	if (!_animation) {
		_animation = std::make_unique<AnimationData>([=](crl::time now) {
			radialAnimationCallback(now);
		});
	}
}

bool Gif::isRadialAnimation() const {
	if (_animation) {
		if (_animation->radial.animating()) {
			return true;
		} else {
			ensureDataMediaCreated(getShownDocument());
			const auto preview = Data::VideoPreviewState(_dataMedia.get());
			if (preview.usingThumbnail() || preview.loaded()) {
				_animation = nullptr;
			}
		}
	}
	return false;
}

void Gif::radialAnimationCallback(crl::time now) const {
	const auto document = getShownDocument();
	ensureDataMediaCreated(document);
	const auto updated = [&] {
		return _animation->radial.update(
			_dataMedia->progress(),
			!document->loading() || _dataMedia->loaded(),
			now);
	}();
	if (!anim::Disabled() || updated) {
		update();
	}
	if (!_animation->radial.animating() && _dataMedia->loaded()) {
		_animation = nullptr;
	}
}

void Gif::unloadHeavyPart() {
	_gif.reset();
	_dataMedia = nullptr;
}

void Gif::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gif) {
			if (_gif->state() == State::Error) {
				_gif.setBad();
			} else if (_gif->ready() && !_gif->started()) {
				if (_gif->width() * _gif->height() > kMaxInlineArea) {
					getShownDocument()->dimensions = QSize(
						_gif->width(),
						_gif->height());
					_gif.reset();
				} else {
					auto height = st::inlineMediaHeight;
					auto frame = countFrameSize();
					_gif->start(frame.width(), frame.height(), _width, height, ImageRoundRadius::None, RectPart::None);
				}
			} else if (_gif->autoPausedGif() && !context()->inlineItemVisible(this)) {
				unloadHeavyPart();
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

Sticker::Sticker(not_null<Context*> context, not_null<Result*> result)
: FileBase(context, result) {
	Expects(getResultDocument() != nullptr);
}

Sticker::~Sticker() = default;

void Sticker::initDimensions() {
	_maxw = st::stickerPanSize.width();
	_minh = st::stickerPanSize.height();
}

void Sticker::preload() const {
	const auto document = getShownDocument();
	Assert(document != nullptr);

	ensureDataMediaCreated(document);
	_dataMedia->checkStickerSmall();
}

void Sticker::ensureDataMediaCreated(not_null<DocumentData*> document) const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = document->createMediaView();
}

void Sticker::unloadHeavyPart() {
	_dataMedia = nullptr;
	_lifetime.destroy();
	_lottie = nullptr;
}

void Sticker::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	ensureDataMediaCreated(getShownDocument());
	bool loaded = _dataMedia->loaded();

	auto over = _a_over.value(_active ? 1. : 0.);
	if (over > 0) {
		p.setOpacity(over);
		Ui::FillRoundRect(p, QRect(QPoint(0, 0), st::stickerPanSize), st::emojiPanHover, Ui::StickerHoverCorners);
		p.setOpacity(1);
	}

	prepareThumbnail();
	if (_lottie && _lottie->ready()) {
		const auto frame = _lottie->frame();
		_lottie->markFrameShown();
		const auto size = frame.size() / cIntRetinaFactor();
		const auto pos = QPoint(
			(st::stickerPanSize.width() - size.width()) / 2,
			(st::stickerPanSize.height() - size.height()) / 2);
		p.drawImage(
			QRect(pos, size),
			frame);
	} else if (!_thumb.isNull()) {
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
	float64 coefw = (st::stickerPanSize.width() - st::roundRadiusSmall * 2) / float64(width);
	float64 coefh = (st::stickerPanSize.height() - st::roundRadiusSmall * 2) / float64(height);
	float64 coef = qMin(qMin(coefw, coefh), 1.);
	int w = qRound(coef * content_width()), h = qRound(coef * content_height());
	return QSize(qMax(w, 1), qMax(h, 1));
}

void Sticker::setupLottie() const {
	Expects(_dataMedia != nullptr);

	_lottie = ChatHelpers::LottiePlayerFromDocument(
		_dataMedia.get(),
		ChatHelpers::StickerLottieSize::InlineResults,
		QSize(
			st::stickerPanSize.width() - st::roundRadiusSmall * 2,
			st::stickerPanSize.height() - st::roundRadiusSmall * 2
		) * cIntRetinaFactor());

	_lottie->updates(
	) | rpl::start_with_next([=] {
		update();
	}, _lifetime);
}

void Sticker::prepareThumbnail() const {
	const auto document = getShownDocument();
	Assert(document != nullptr);

	ensureDataMediaCreated(document);
	if (!_lottie
		&& document->sticker()
		&& document->sticker()->animated
		&& _dataMedia->loaded()) {
		setupLottie();
	}
	_dataMedia->checkStickerSmall();
	if (const auto sticker = _dataMedia->getStickerSmall()) {
		if (!_lottie && !_thumbLoaded) {
			const auto thumbSize = getThumbSize();
			_thumb = sticker->pix(
				thumbSize.width(),
				thumbSize.height());
			_thumbLoaded = true;
		}
	}
}

Photo::Photo(not_null<Context*> context, not_null<Result*> result)
: ItemBase(context, result) {
	Expects(getShownPhoto() != nullptr);
}

void Photo::initDimensions() {
	const auto photo = getShownPhoto();
	int32 w = photo->width(), h = photo->height();
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

	prepareThumbnail({ _width, height }, frame);
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

void Photo::unloadHeavyPart() {
	_photoMedia = nullptr;
}

PhotoData *Photo::getShownPhoto() const {
	if (const auto result = getPhoto()) {
		return result;
	}
	return getResultPhoto();
}

QSize Photo::countFrameSize() const {
	const auto photo = getShownPhoto();
	int32 framew = photo->width(), frameh = photo->height(), height = st::inlineMediaHeight;
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

void Photo::validateThumbnail(
		Image *image,
		QSize size,
		QSize frame,
		bool good) const {
	if (!image || (_thumbGood && !good)) {
		return;
	} else if ((_thumb.size() == size * cIntRetinaFactor())
		&& (_thumbGood || !good)) {
		return;
	}
	const auto origin = fileOrigin();
	_thumb = image->pixNoCache(
		frame.width() * cIntRetinaFactor(),
		frame.height() * cIntRetinaFactor(),
		Images::Option::Smooth | (good ? Images::Option(0) : Images::Option::Blurred),
		size.width(),
		size.height());
	_thumbGood = good;
}

void Photo::prepareThumbnail(QSize size, QSize frame) const {
	using PhotoSize = Data::PhotoSize;

	const auto photo = getShownPhoto();
	Assert(photo != nullptr);

	if (!_photoMedia) {
		_photoMedia = photo->createMediaView();
		_photoMedia->wanted(PhotoSize::Thumbnail, fileOrigin());
	}
	validateThumbnail(_photoMedia->image(PhotoSize::Thumbnail), size, frame, true);
	validateThumbnail(_photoMedia->image(PhotoSize::Small), size, frame, false);
	validateThumbnail(_photoMedia->thumbnailInline(), size, frame, false);
}

Video::Video(not_null<Context*> context, not_null<Result*> result)
: FileBase(context, result)
, _link(getResultPreviewHandler())
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
	if (int duration = content_duration()) {
		_duration = Ui::FormatDurationText(duration);
		_durationWidth = st::normalFont->width(_duration);
	}
}

bool Video::withThumbnail() const {
	if (const auto document = getShownDocument()) {
		if (document->hasThumbnail()) {
			return true;
		}
	}
	return hasResultThumb();
}

void Video::initDimensions() {
	const auto withThumb = withThumbnail();

	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	const auto textWidth = _maxw - (st::inlineThumbSize + st::inlineThumbSkip);
	TextParseOptions titleOpts = { 0, textWidth, 2 * st::semiboldFont->height, Qt::LayoutDirectionAuto };
	auto title = TextUtilities::SingleLine(_result->getLayoutTitle());
	if (title.isEmpty()) {
		title = tr::lng_media_video(tr::now);
	}
	_title.setText(st::semiboldTextStyle, title, titleOpts);
	int32 titleHeight = qMin(_title.countHeight(textWidth), 2 * st::semiboldFont->height);

	int32 descriptionLines = withThumb ? (titleHeight > st::semiboldFont->height ? 1 : 2) : 3;

	TextParseOptions descriptionOpts = { TextParseMultiline, textWidth, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	QString description = _result->getLayoutDescription();
	if (description.isEmpty()) {
		description = _duration;
	}
	_description.setText(st::defaultTextStyle, description, descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(textWidth), descriptionLines * st::normalFont->height);

	_minh = st::inlineThumbSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void Video::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int left = st::inlineThumbSize + st::inlineThumbSkip;

	const auto withThumb = withThumbnail();
	if (withThumb) {
		prepareThumbnail({ st::inlineThumbSize, st::inlineThumbSize });
		if (_thumb.isNull()) {
			p.fillRect(style::rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width), st::overviewPhotoBg);
		} else {
			p.drawPixmapLeft(0, st::inlineRowMargin, _width, _thumb);
		}
	} else {
		p.fillRect(style::rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width), st::overviewVideoBg);
	}

	if (!_duration.isEmpty()) {
		int durationTop = st::inlineRowMargin + st::inlineThumbSize - st::normalFont->height - st::inlineDurationMargin;
		int durationW = _durationWidth + 2 * st::msgDateImgPadding.x(), durationH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		int durationX = (st::inlineThumbSize - durationW) / 2, durationY = st::inlineRowMargin + st::inlineThumbSize - durationH;
		Ui::FillRoundRect(p, durationX, durationY - st::msgDateImgPadding.y(), durationW, durationH, st::msgDateImgBg, Ui::DateCorners);
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
		p.fillRect(style::rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

void Video::unloadHeavyPart() {
	_documentMedia = nullptr;
	ItemBase::unloadHeavyPart();
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

void Video::prepareThumbnail(QSize size) const {
	if (const auto document = getShownDocument()) {
		if (document->hasThumbnail()) {
			if (!_documentMedia) {
				_documentMedia = document->createMediaView();
				_documentMedia->thumbnailWanted(fileOrigin());
			}
			if (!_documentMedia->thumbnail()) {
				return;
			}
		}
	}
	const auto thumb = _documentMedia
		? _documentMedia->thumbnail()
		: getResultThumb(fileOrigin());
	if (!thumb) {
		return;
	}
	if (_thumb.size() != size * cIntRetinaFactor()) {
		const auto width = size.width();
		const auto height = size.height();
		auto w = qMax(style::ConvertScale(thumb->width()), 1);
		auto h = qMax(style::ConvertScale(thumb->height()), 1);
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
		_thumb = thumb->pixNoCache(w * cIntRetinaFactor(), h * cIntRetinaFactor(), Images::Option::Smooth, width, height);
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
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineFileSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineFileSize - st::inlineThumbSkip)
, _open(std::make_shared<OpenFileClickHandler>(result))
, _cancel(std::make_shared<CancelFileClickHandler>(result))
, _document(getShownDocument()) {
	Expects(getResultDocument() != nullptr);

	updateStatusText();

	// We have to save document, not read it from Result every time.
	// Because we first delete the Result and then delete this File.
	// So in destructor we have to remember _document, we can't read it.
	regDocumentItem(_document, this);
}

void File::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int textWidth = _maxw - (st::inlineFileSize + st::inlineThumbSkip);

	TextParseOptions titleOpts = { 0, _maxw, st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldTextStyle, TextUtilities::SingleLine(_result->getLayoutTitle()), titleOpts);

	TextParseOptions descriptionOpts = { TextParseMultiline, _maxw, st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::defaultTextStyle, _result->getLayoutDescription(), descriptionOpts);

	_minh = st::inlineFileSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void File::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	const auto left = st::inlineFileSize + st::inlineThumbSkip;

	ensureDataMediaCreated();
	const auto loaded = _documentMedia->loaded();
	const auto displayLoading = _document->displayLoading();
	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_documentMedia->progress());
		}
	}
	const auto showPause = updateStatusText();
	const auto radial = isRadialAnimation();

	auto inner = style::rtlrect(0, st::inlineRowMargin, st::inlineFileSize, st::inlineFileSize, _width);
	p.setPen(Qt::NoPen);

	const auto coverDrawn = _document->isSongWithCover()
		&& HistoryView::DrawThumbnailAsSongCover(p, _documentMedia, inner);
	if (!coverDrawn) {
		PainterHighQualityEnabler hq(p);
		if (isThumbAnimation()) {
			const auto over = _animation->a_thumbOver.value(1.);
			p.setBrush(
				anim::brush(st::msgFileInBg, st::msgFileInBgOver, over));
		} else {
			const auto over = ClickHandler::showAsActive(_document->loading()
				? _cancel
				: _open);
			p.setBrush(over ? st::msgFileInBgOver : st::msgFileInBg);
		}
		p.drawEllipse(inner);
	}

	if (radial) {
		auto radialCircle = inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine));
		_animation->radial.draw(p, radialCircle, st::msgFileRadialLine, st::historyFileInRadialFg);
	}

	const auto icon = [&] {
		if (radial || _document->loading()) {
			return &st::historyFileInCancel;
		} else if (showPause) {
			return &st::historyFileInPause;
		} else if (_document->isImage()) {
			return &st::historyFileInImage;
		} else if (_document->isSongWithCover()) {
			return &st::historyFileSongPlay;
		} else if (_document->isVoiceMessage()
			|| _document->isAudioFile()) {
			return &st::historyFileInPlay;
		}
		return &st::historyFileInDocument;
	}();
	icon->paintInCenter(p, inner);

	int titleTop = st::inlineRowMargin + st::inlineRowFileNameTop;
	int descriptionTop = st::inlineRowMargin + st::inlineRowFileDescriptionTop;

	p.setPen(st::inlineTitleFg);
	_title.drawLeftElided(p, left, titleTop, _width - left, _width);

	p.setPen(st::inlineDescriptionFg);
	bool drawStatusSize = true;
	if (_statusSize == Ui::FileStatusSizeReady
		|| _statusSize == Ui::FileStatusSizeLoaded
		|| _statusSize == Ui::FileStatusSizeFailed) {
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
		p.fillRect(style::rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

TextState File::getState(
		QPoint point,
		StateRequest request) const {
	if (QRect(0, st::inlineRowMargin, st::inlineFileSize, st::inlineFileSize).contains(point)) {
		return { nullptr, _document->loading() ? _cancel : _open };
	} else {
		auto left = st::inlineFileSize + st::inlineThumbSkip;
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

void File::unloadHeavyPart() {
	_documentMedia = nullptr;
}

File::~File() {
	unregDocumentItem(_document, this);
}

void File::thumbAnimationCallback() {
	update();
}

void File::radialAnimationCallback(crl::time now) const {
	ensureDataMediaCreated();
	const auto updated = [&] {
		return _animation->radial.update(
			_documentMedia->progress(),
			!_document->loading() || _documentMedia->loaded(),
			now);
	}();
	if (!anim::Disabled() || updated) {
		update();
	}
	if (!_animation->radial.animating()) {
		checkAnimationFinished();
	}
}

void File::ensureAnimation() const {
	if (!_animation) {
		_animation = std::make_unique<AnimationData>([=](crl::time now) {
			return radialAnimationCallback(now);
		});
	}
}

void File::ensureDataMediaCreated() const {
	if (_documentMedia) {
		return;
	}
	_documentMedia = _document->createMediaView();
}

void File::checkAnimationFinished() const {
	if (_animation
		&& !_animation->a_thumbOver.animating()
		&& !_animation->radial.animating()) {
		ensureDataMediaCreated();
		if (_documentMedia->loaded()) {
			_animation.reset();
		}
	}
}

bool File::updateStatusText() const {
	ensureDataMediaCreated();
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_document->status == FileDownloadFailed || _document->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (_document->uploading()) {
		statusSize = _document->uploadingData->offset;
	} else if (_document->loading()) {
		statusSize = _document->loadOffset();
	} else if (_documentMedia->loaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}

	if (_document->isVoiceMessage() || _document->isAudioFile()) {
		const auto type = _document->isVoiceMessage() ? AudioMsgId::Type::Voice : AudioMsgId::Type::Song;
		const auto state = Media::Player::instance()->getState(type);
		if (state.id == AudioMsgId(_document, FullMsgId(), state.id.externalPlayId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = Media::Player::ShowPauseIcon(state.state);
		}
		if (!showPause && (state.id == AudioMsgId(_document, FullMsgId(), state.id.externalPlayId())) && Media::Player::instance()->isSeeking(AudioMsgId::Type::Song)) {
			showPause = true;
		}
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
	if (_statusSize == Ui::FileStatusSizeReady) {
		_statusText = (duration >= 0) ? Ui::FormatDurationAndSizeText(duration, fullSize) : (duration < -1 ? Ui::FormatGifAndSizeText(fullSize) : Ui::FormatSizeText(fullSize));
	} else if (_statusSize == Ui::FileStatusSizeLoaded) {
		_statusText = (duration >= 0) ? Ui::FormatDurationText(duration) : (duration < -1 ? qsl("GIF") : Ui::FormatSizeText(fullSize));
	} else if (_statusSize == Ui::FileStatusSizeFailed) {
		_statusText = tr::lng_attach_failed(tr::now);
	} else if (_statusSize >= 0) {
		_statusText = Ui::FormatDownloadText(_statusSize, fullSize);
	} else {
		_statusText = Ui::FormatPlayedText(-_statusSize - 1, realDuration);
	}
}

Contact::Contact(not_null<Context*> context, not_null<Result*> result)
: ItemBase(context, result)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
}

void Contact::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (st::inlineThumbSize + st::inlineThumbSkip);
	TextParseOptions titleOpts = { 0, textWidth, st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldTextStyle, TextUtilities::SingleLine(_result->getLayoutTitle()), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(textWidth), st::semiboldFont->height);

	TextParseOptions descriptionOpts = { TextParseMultiline, textWidth, st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::defaultTextStyle, _result->getLayoutDescription(), descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(textWidth), st::normalFont->height);

	_minh = st::inlineFileSize;
	_minh += st::inlineRowMargin * 2 + st::inlineRowBorder;
}

void Contact::paint(Painter &p, const QRect &clip, const PaintContext *context) const {
	int32 left = st::emojiPanHeaderLeft - st::inlineResultsLeft;

	left = st::inlineFileSize + st::inlineThumbSkip;
	prepareThumbnail(st::inlineFileSize, st::inlineFileSize);
	QRect rthumb(style::rtlrect(0, st::inlineRowMargin, st::inlineFileSize, st::inlineFileSize, _width));
	p.drawPixmapLeft(rthumb.topLeft(), _width, _thumb);

	int titleTop = st::inlineRowMargin + st::inlineRowFileNameTop;
	int descriptionTop = st::inlineRowMargin + st::inlineRowFileDescriptionTop;

	p.setPen(st::inlineTitleFg);
	_title.drawLeftElided(p, left, titleTop, _width - left, _width);

	p.setPen(st::inlineDescriptionFg);
	_description.drawLeftElided(p, left, descriptionTop, _width - left, _width);

	if (!context->lastRow) {
		p.fillRect(style::rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
	}
}

TextState Contact::getState(
		QPoint point,
		StateRequest request) const {
	if (!QRect(0, st::inlineRowMargin, st::inlineFileSize, st::inlineThumbSize).contains(point)) {
		auto left = (st::inlineFileSize + st::inlineThumbSkip);
		if (QRect(left, 0, _width - left, _height).contains(point)) {
			return { nullptr, _send };
		}
	}
	return {};
}

void Contact::prepareThumbnail(int width, int height) const {
	if (!hasResultThumb()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			_thumb = getResultContactAvatar(width, height);
		}
		return;
	}

	const auto origin = fileOrigin();
	const auto thumb = getResultThumb(origin);
	if (!thumb
		|| ((_thumb.width() == width * cIntRetinaFactor())
			&& (_thumb.height() == height * cIntRetinaFactor()))) {
		return;
	}
	auto w = qMax(style::ConvertScale(thumb->width()), 1);
	auto h = qMax(style::ConvertScale(thumb->height()), 1);
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
	_thumb = thumb->pixNoCache(w * cIntRetinaFactor(), h * cIntRetinaFactor(), Images::Option::Smooth, width, height);
}

Article::Article(
	not_null<Context*> context,
	not_null<Result*> result,
	bool withThumb)
: ItemBase(context, result)
, _url(getResultUrlHandler())
, _link(getResultPreviewHandler())
, _withThumb(withThumb)
, _title(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip)
, _description(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft - st::inlineThumbSize - st::inlineThumbSkip) {
	if (!_link) {
		if (const auto point = result->getLocationPoint()) {
			_link = std::make_shared<LocationClickHandler>(*point);
		}
	}
	_thumbLetter = getResultThumbLetter();
}

void Article::initDimensions() {
	_maxw = st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft;
	int32 textWidth = _maxw - (_withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : (st::emojiPanHeaderLeft - st::inlineResultsLeft));
	TextParseOptions titleOpts = { 0, textWidth, 2 * st::semiboldFont->height, Qt::LayoutDirectionAuto };
	_title.setText(st::semiboldTextStyle, TextUtilities::SingleLine(_result->getLayoutTitle()), titleOpts);
	int32 titleHeight = qMin(_title.countHeight(textWidth), 2 * st::semiboldFont->height);

	int32 descriptionLines = (_withThumb || _url) ? 2 : 3;
	QString description = _result->getLayoutDescription();
	TextParseOptions descriptionOpts = { TextParseMultiline, textWidth, descriptionLines * st::normalFont->height, Qt::LayoutDirectionAuto };
	_description.setText(st::defaultTextStyle, description, descriptionOpts);
	int32 descriptionHeight = qMin(_description.countHeight(textWidth), descriptionLines * st::normalFont->height);

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
		int32 textWidth = _width - (_withThumb ? (st::inlineThumbSize + st::inlineThumbSkip) : (st::emojiPanHeaderLeft - st::inlineResultsLeft));
		if (_urlWidth > textWidth) {
			_urlText = st::normalFont->elided(_urlText, textWidth);
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
		prepareThumbnail(st::inlineThumbSize, st::inlineThumbSize);
		QRect rthumb(style::rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width));
		if (_thumb.isNull()) {
			if (!hasResultThumb() && !_thumbLetter.isEmpty()) {
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
		p.fillRect(style::rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
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
			if (style::rtlrect(left, st::inlineRowMargin + titleHeight + descriptionHeight, _urlWidth, st::normalFont->height, _width).contains(point)) {
				return { nullptr, _url };
			}
		}
		return { nullptr, _send };
	}
	return {};
}

void Article::prepareThumbnail(int width, int height) const {
	if (!hasResultThumb()) {
		if (_thumb.width() != width * cIntRetinaFactor() || _thumb.height() != height * cIntRetinaFactor()) {
			_thumb = getResultContactAvatar(width, height);
		}
		return;
	}

	const auto origin = fileOrigin();
	const auto thumb = getResultThumb(origin);
	if (!thumb
		|| ((_thumb.width() == width * cIntRetinaFactor())
			&& (_thumb.height() == height * cIntRetinaFactor()))) {
		return;
	}
	auto w = qMax(style::ConvertScale(thumb->width()), 1);
	auto h = qMax(style::ConvertScale(thumb->height()), 1);
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
	_thumb = thumb->pixNoCache(w * cIntRetinaFactor(), h * cIntRetinaFactor(), Images::Option::Smooth, width, height);
}

Game::Game(not_null<Context*> context, not_null<Result*> result)
: ItemBase(context, result)
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
	auto rthumb = style::rtlrect(0, st::inlineRowMargin, st::inlineThumbSize, st::inlineThumbSize, _width);

	// Gif thumb
	auto thumbDisplayed = false, radial = false;
	const auto photo = getResultPhoto();
	const auto document = getResultDocument();
	if (document) {
		ensureDataMediaCreated(document);
	} else if (photo) {
		ensureDataMediaCreated(photo);
	}
	auto animatedThumb = document && document->isAnimation();
	if (animatedThumb) {
		_documentMedia->automaticLoad(fileOrigin(), nullptr);

		bool loaded = _documentMedia->loaded(), loading = document->loading(), displayLoading = document->displayLoading();
		if (loaded && !_gif && !_gif.isBad()) {
			auto that = const_cast<Game*>(this);
			that->_gif = Media::Clip::MakeReader(
				_documentMedia->owner()->location(),
				_documentMedia->bytes(),
				[=](Media::Clip::Notification notification) { that->clipCallback(notification); });
		}

		bool animating = (_gif && _gif->started());
		if (displayLoading) {
			if (!_radial) {
				_radial = std::make_unique<Ui::RadialAnimation>([=](crl::time now) {
					return radialAnimationCallback(now);
				});
			}
			if (!_radial->animating()) {
				_radial->start(_documentMedia->progress());
			}
		}
		radial = isRadialAnimation();

		if (animating) {
			const auto pixmap = _gif->current(_frameSize.width(), _frameSize.height(), st::inlineThumbSize, st::inlineThumbSize, ImageRoundRadius::None, RectPart::None, context->paused ? 0 : context->ms);
			if (_thumb.isNull()) {
				_thumb = pixmap;
				_thumbGood = true;
			}
			p.drawPixmapLeft(rthumb.topLeft(), _width, pixmap);
			thumbDisplayed = true;
		}
	}

	if (!thumbDisplayed) {
		prepareThumbnail({ st::inlineThumbSize, st::inlineThumbSize });
		if (_thumb.isNull()) {
			p.fillRect(rthumb, st::overviewPhotoBg);
		} else {
			p.drawPixmapLeft(rthumb.topLeft(), _width, _thumb);
		}
	}

	if (radial) {
		p.fillRect(rthumb, st::msgDateImgBg);
		QRect inner((st::inlineThumbSize - st::inlineRadialSize) / 2, (st::inlineThumbSize - st::inlineRadialSize) / 2, st::inlineRadialSize, st::inlineRadialSize);
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
		p.fillRect(style::rtlrect(left, _height - st::inlineRowBorder, _width - left, st::inlineRowBorder, _width), st::inlineRowBorderFg);
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

void Game::prepareThumbnail(QSize size) const {
	if (const auto document = getResultDocument()) {
		Assert(_documentMedia != nullptr);
		validateThumbnail(_documentMedia->thumbnail(), size, true);
		validateThumbnail(_documentMedia->thumbnailInline(), size, false);
	} else if (const auto photo = getResultPhoto()) {
		using Data::PhotoSize;
		Assert(_photoMedia != nullptr);
		validateThumbnail(_photoMedia->image(PhotoSize::Thumbnail), size, true);
		validateThumbnail(_photoMedia->image(PhotoSize::Small), size, false);
		validateThumbnail(_photoMedia->thumbnailInline(), size, false);
	}
}

void Game::ensureDataMediaCreated(not_null<DocumentData*> document) const {
	if (_documentMedia) {
		return;
	}
	_documentMedia = document->createMediaView();
	_documentMedia->thumbnailWanted(fileOrigin());
}

void Game::ensureDataMediaCreated(not_null<PhotoData*> photo) const {
	if (_photoMedia) {
		return;
	}
	_photoMedia = photo->createMediaView();
	_photoMedia->wanted(Data::PhotoSize::Thumbnail, fileOrigin());
}

void Game::validateThumbnail(Image *image, QSize size, bool good) const {
	if (!image || (_thumbGood && !good)) {
		return;
	} else if ((_thumb.size() == size * cIntRetinaFactor())
		&& (_thumbGood || !good)) {
		return;
	}
	const auto width = size.width();
	const auto height = size.height();
	auto w = qMax(style::ConvertScale(image->width()), 1);
	auto h = qMax(style::ConvertScale(image->height()), 1);
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
	_thumbGood = good;
	_thumb = image->pixNoCache(
		w * cIntRetinaFactor(),
		h * cIntRetinaFactor(),
		(Images::Option::Smooth
			| (good ? Images::Option::None : Images::Option::Blurred)),
		size.width(),
		size.height());
}

bool Game::isRadialAnimation() const {
	if (_radial) {
		if (_radial->animating()) {
			return true;
		} else {
			ensureDataMediaCreated(getResultDocument());
			if (_documentMedia->loaded()) {
				_radial = nullptr;
			}
		}
	}
	return false;
}

void Game::radialAnimationCallback(crl::time now) const {
	const auto document = getResultDocument();
	ensureDataMediaCreated(document);
	const auto updated = [&] {
		return _radial->update(
			_documentMedia->progress(),
			!document->loading() || _documentMedia->loaded(),
			now);
	}();
	if (!anim::Disabled() || updated) {
		update();
	}
	if (!_radial->animating() && _documentMedia->loaded()) {
		_radial = nullptr;
	}
}

void Game::unloadHeavyPart() {
	_gif.reset();
	_documentMedia = nullptr;
	_photoMedia = nullptr;
}

void Game::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gif) {
			if (_gif->state() == State::Error) {
				_gif.setBad();
			} else if (_gif->ready() && !_gif->started()) {
				if (_gif->width() * _gif->height() > kMaxInlineArea) {
					getResultDocument()->dimensions = QSize(
						_gif->width(),
						_gif->height());
					_gif.reset();
				} else {
					_gif->start(
						_frameSize.width(),
						_frameSize.height(),
						st::inlineThumbSize,
						st::inlineThumbSize,
						ImageRoundRadius::None,
						RectPart::None);
				}
			} else if (_gif->autoPausedGif() && !context()->inlineItemVisible(this)) {
				unloadHeavyPart();
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
