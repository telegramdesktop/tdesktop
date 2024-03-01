/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_photo.h"

#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_media_spoiler.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_utility.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/image/image.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/chat/chat_style.h"
#include "ui/grouped_layout.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_streaming.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_auto_download.h"
#include "data/data_web_page.h"
#include "core/application.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kStoryWidth = 720;
constexpr auto kStoryHeight = 1280;

using Data::PhotoSize;

} // namespace

struct Photo::Streamed {
	explicit Streamed(std::shared_ptr<::Media::Streaming::Document> shared);
	::Media::Streaming::Instance instance;
	::Media::Streaming::FrameRequest frozenRequest;
	QImage frozenFrame;
	std::array<QImage, 4> roundingCorners;
	QImage roundingMask;
};

Photo::Streamed::Streamed(
	std::shared_ptr<::Media::Streaming::Document> shared)
: instance(std::move(shared), nullptr) {
}

Photo::Photo(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<PhotoData*> photo,
	bool spoiler)
: File(parent, realParent)
, _data(photo)
, _storyId(realParent->media()
	? realParent->media()->storyId()
	: FullStoryId())
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right())
, _spoiler(spoiler ? std::make_unique<MediaSpoiler>() : nullptr) {
	_caption = createCaption(realParent);
	create(realParent->fullId());
}

Photo::Photo(
	not_null<Element*> parent,
	not_null<PeerData*> chat,
	not_null<PhotoData*> photo,
	int width)
: File(parent, parent->data())
, _data(photo)
, _serviceWidth(width) {
	create(parent->data()->fullId(), chat);
}

Photo::~Photo() {
	if (_streamed || _dataMedia) {
		if (_streamed) {
			_data->owner().streaming().keepAlive(_data);
			stopAnimation();
		}
		if (_dataMedia) {
			_data->owner().keepAlive(base::take(_dataMedia));
			_parent->checkHeavyPart();
		}
	}
	togglePollingStory(false);
}

void Photo::create(FullMsgId contextId, PeerData *chat) {
	setLinks(
		std::make_shared<PhotoOpenClickHandler>(
			_data,
			crl::guard(this, [=](FullMsgId id) { showPhoto(id); }),
			contextId),
		std::make_shared<PhotoSaveClickHandler>(_data, contextId, chat),
		std::make_shared<PhotoCancelClickHandler>(
			_data,
			crl::guard(this, [=](FullMsgId id) {
				_parent->delegate()->elementCancelUpload(id);
			}),
			contextId));
	if ((_dataMedia = _data->activeMediaView())) {
		dataMediaCreated();
	} else if (_data->inlineThumbnailBytes().isEmpty()
		&& (_data->hasExact(PhotoSize::Small)
			|| _data->hasExact(PhotoSize::Thumbnail))) {
		_data->load(PhotoSize::Small, contextId);
	}
	if (_spoiler) {
		createSpoilerLink(_spoiler.get());
	}
}

void Photo::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	dataMediaCreated();
}

void Photo::dataMediaCreated() const {
	Expects(_dataMedia != nullptr);

	if (_data->inlineThumbnailBytes().isEmpty()
		&& !_dataMedia->image(PhotoSize::Large)
		&& !_dataMedia->image(PhotoSize::Thumbnail)) {
		_dataMedia->wanted(PhotoSize::Small, _realParent->fullId());
	}
	history()->owner().registerHeavyViewPart(_parent);
	togglePollingStory(true);
}

bool Photo::hasHeavyPart() const {
	return (_spoiler && _spoiler->animation) || _streamed || _dataMedia;
}

void Photo::unloadHeavyPart() {
	stopAnimation();
	_dataMedia = nullptr;
	if (_spoiler) {
		_spoiler->background = _spoiler->cornerCache = QImage();
		_spoiler->animation = nullptr;
	}
	_imageCache = QImage();
	_caption.unloadPersistentAnimation();
	togglePollingStory(false);
}

void Photo::togglePollingStory(bool enabled) const {
	const auto pollingStory = (enabled ? 1 : 0);
	if (!_storyId || _pollingStory == pollingStory) {
		return;
	}
	const auto polling = Data::Stories::Polling::Chat;
	if (!enabled) {
		_data->owner().stories().unregisterPolling(_storyId, polling);
	} else if (
			!_data->owner().stories().registerPolling(_storyId, polling)) {
		return;
	}
	_pollingStory = pollingStory;
}

QSize Photo::countOptimalSize() {
	if (_serviceWidth > 0) {
		return { int(_serviceWidth), int(_serviceWidth) };
	}

	if (_parent->media() != this) {
		_caption = Ui::Text::String();
	} else if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}

	const auto dimensions = photoSize();
	const auto scaled = CountDesiredMediaSize(dimensions);
	const auto minWidth = std::clamp(
		_parent->minWidthForMedia(),
		(_parent->hasBubble()
			? st::historyPhotoBubbleMinWidth
			: st::minPhotoSize),
		st::maxMediaSize);
	const auto maxActualWidth = qMax(scaled.width(), minWidth);
	auto maxWidth = qMax(maxActualWidth, scaled.height());
	auto minHeight = qMax(scaled.height(), st::minPhotoSize);
	if (_parent->hasBubble() && !_caption.isEmpty()) {
		maxWidth = qMax(
			maxWidth,
			(st::msgPadding.left()
				+ _caption.maxWidth()
				+ st::msgPadding.right()));
		minHeight = adjustHeightForLessCrop(
			dimensions,
			{ maxWidth, minHeight });
		if (const auto botTop = _parent->Get<FakeBotAboutTop>()) {
			accumulate_max(maxWidth, botTop->maxWidth);
			minHeight += botTop->height;
		}
		minHeight += st::mediaCaptionSkip + _caption.minHeight();
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize Photo::countCurrentSize(int newWidth) {
	if (_serviceWidth) {
		return { int(_serviceWidth), int(_serviceWidth) };
	}
	const auto thumbMaxWidth = qMin(newWidth, st::maxMediaSize);
	const auto minWidth = std::clamp(
		_parent->minWidthForMedia(),
		qMin(thumbMaxWidth, _parent->hasBubble()
			? st::historyPhotoBubbleMinWidth
			: st::minPhotoSize),
		thumbMaxWidth);
	const auto dimensions = photoSize();
	auto pix = CountPhotoMediaSize(
		CountDesiredMediaSize(dimensions),
		newWidth,
		maxWidth());
	newWidth = qMax(pix.width(), minWidth);
	auto newHeight = qMax(pix.height(), st::minPhotoSize);
	auto imageHeight = newHeight;
	if (_parent->hasBubble() && !_caption.isEmpty()) {
		auto captionMaxWidth = st::msgPadding.left()
			+ _caption.maxWidth()
			+ st::msgPadding.right();
		const auto botTop = _parent->Get<FakeBotAboutTop>();
		if (botTop) {
			accumulate_max(captionMaxWidth, botTop->maxWidth);
		}
		const auto maxWithCaption = qMin(st::msgMaxWidth, captionMaxWidth);
		newWidth = qMin(qMax(newWidth, maxWithCaption), thumbMaxWidth);
		imageHeight = newHeight = adjustHeightForLessCrop(
			dimensions,
			{ newWidth, newHeight });
		const auto captionw = newWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		if (botTop) {
			newHeight += botTop->height;
		}
		newHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			newHeight += st::msgPadding.bottom();
		}
	}
	const auto enlargeInner = st::historyPageEnlargeSize;
	const auto enlargeOuter = 2 * st::historyPageEnlargeSkip + enlargeInner;
	const auto showEnlarge = (_parent->media() != this)
		&& _parent->data()->media()
		&& _parent->data()->media()->webpage()
		&& _parent->data()->media()->webpage()->suggestEnlargePhoto()
		&& (newWidth >= enlargeOuter)
		&& (imageHeight >= enlargeOuter);
	_showEnlarge = showEnlarge ? 1 : 0;
	return { newWidth, newHeight };
}

int Photo::adjustHeightForLessCrop(QSize dimensions, QSize current) const {
	if (dimensions.isEmpty()
		|| !::Media::Streaming::FrameResizeMayExpand(current, dimensions)) {
		return current.height();
	}
	return qMax(
		current.height(),
		current.width() * dimensions.height() / dimensions.width());
}

void Photo::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	} else if (_storyId && _data->isNull()) {
		return;
	}

	ensureDataMediaCreated();
	_dataMedia->automaticLoad(_realParent->fullId(), _parent->data());
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();
	auto loaded = _dataMedia->loaded();
	auto displayLoading = _data->displayLoading();

	auto inWebPage = (_parent->media() != this);
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	auto bubble = _parent->hasBubble();

	auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_dataMedia->progress());
		}
	}
	const auto radial = isRadialAnimation();
	const auto botTop = _parent->Get<FakeBotAboutTop>();

	auto rthumb = style::rtlrect(paintx, painty, paintw, painth, width());
	if (_serviceWidth > 0) {
		paintUserpicFrame(p, context, rthumb.topLeft());
	} else {
		const auto rounding = inWebPage
			? std::optional<Ui::BubbleRounding>()
			: adjustedBubbleRoundingWithCaption(_caption);
		if (bubble) {
			if (!_caption.isEmpty()) {
				painth -= st::mediaCaptionSkip + _caption.countHeight(captionw);
				if (botTop) {
					painth -= botTop->height;
				}
				if (isBubbleBottom()) {
					painth -= st::msgPadding.bottom();
				}
				rthumb = style::rtlrect(paintx, painty, paintw, painth, width());
			}
		} else {
			Assert(rounding.has_value());
			fillImageShadow(p, rthumb, *rounding, context);
		}
		const auto revealed = _spoiler
			? _spoiler->revealAnimation.value(_spoiler->revealed ? 1. : 0.)
			: 1.;
		if (revealed < 1.) {
			validateSpoilerImageCache(rthumb.size(), rounding);
		}
		if (revealed > 0.) {
			validateImageCache(rthumb.size(), rounding);
			p.drawImage(rthumb.topLeft(), _imageCache);
		}
		if (revealed < 1.) {
			p.setOpacity(1. - revealed);
			p.drawImage(rthumb.topLeft(), _spoiler->background);
			fillImageSpoiler(p, _spoiler.get(), rthumb, context);
			p.setOpacity(1.);
		}
		if (context.selected()) {
			fillImageOverlay(p, rthumb, rounding, context);
		}
	}

	const auto showEnlarge = loaded && _showEnlarge;
	const auto paintInCenter = (radial || (!loaded && !_data->loading()));
	if (paintInCenter || showEnlarge) {
		p.setPen(Qt::NoPen);
		if (context.selected()) {
			p.setBrush(st->msgDateImgBgSelected());
		} else if (showEnlarge) {
			const auto over = ClickHandler::showAsActive(_openl);
			p.setBrush(over ? st->msgDateImgBgOver() : st->msgDateImgBg());
		} else if (isThumbAnimation()) {
			const auto over = _animation->a_thumbOver.value(1.);
			p.setBrush(anim::brush(st->msgDateImgBg(), st->msgDateImgBgOver(), over));
		} else {
			const auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st->msgDateImgBgOver() : st->msgDateImgBg());
		}
	}
	if (paintInCenter) {
		const auto radialOpacity = (radial && loaded && !_data->uploading())
			? _animation->radial.opacity() :
			1.;
		const auto innerSize = st::msgFileLayout.thumbSize;
		QRect inner(rthumb.x() + (rthumb.width() - innerSize) / 2, rthumb.y() + (rthumb.height() - innerSize) / 2, innerSize, innerSize);

		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		const auto &icon = (radial || _data->loading())
			? sti->historyFileThumbCancel
			: sti->historyFileThumbDownload;
		icon.paintInCenter(p, inner);
		p.setOpacity(1);
		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, sti->historyFileThumbRadialFg);
		}
	}
	if (showEnlarge) {
		auto hq = PainterHighQualityEnabler(p);
		const auto rect = enlargeRect();
		const auto radius = st::historyPageEnlargeRadius;
		p.drawRoundedRect(rect, radius, radius);
		sti->historyPageEnlarge.paintInCenter(p, rect);
	}

	// date
	if (!_caption.isEmpty()) {
		p.setPen(stm->historyTextFg);
		_parent->prepareCustomEmojiPaint(p, context, _caption);
		auto top = painty + painth + st::mediaCaptionSkip;
		if (botTop) {
			botTop->text.drawLeftElided(
				p,
				st::msgPadding.left(),
				top,
				captionw,
				_parent->width());
			top += botTop->height;
		}
		auto highlightRequest = context.computeHighlightCache();
		_caption.draw(p, {
			.position = QPoint(st::msgPadding.left(), top),
			.availableWidth = captionw,
			.palette = &stm->textPalette,
			.pre = stm->preCache.get(),
			.blockquote = context.quoteCache(parent()->contentColorIndex()),
			.colors = context.st->highlightColors(),
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.selection = context.selection,
			.highlight = highlightRequest ? &*highlightRequest : nullptr,
		});
	} else if (!inWebPage) {
		auto fullRight = paintx + paintw;
		auto fullBottom = painty + painth;
		if (needInfoDisplay()) {
			_parent->drawInfo(
				p,
				context,
				fullRight,
				fullBottom,
				2 * paintx + paintw,
				InfoDisplayType::Image);
		}
		if (const auto size = bubble ? std::nullopt : _parent->rightActionSize()) {
			auto fastShareLeft = _parent->hasRightLayout()
				? (paintx - size->width() - st::historyFastShareLeft)
				: (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - size->height());
			_parent->drawRightAction(p, context, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

void Photo::validateUserpicImageCache(QSize size, bool forum) const {
	const auto forumValue = forum ? 1 : 0;
	const auto large = _dataMedia->image(PhotoSize::Large);
	const auto ratio = style::DevicePixelRatio();
	const auto blurredValue = large ? 0 : 1;
	if (_imageCache.size() == (size * ratio)
		&& _imageCacheForum == forumValue
		&& _imageCacheBlurred == blurredValue) {
		return;
	}
	auto original = [&] {
		if (large) {
			return large->original();
		} else if (const auto thumbnail = _dataMedia->image(
				PhotoSize::Thumbnail)) {
			return thumbnail->original();
		} else if (const auto small = _dataMedia->image(
				PhotoSize::Small)) {
			return small->original();
		} else if (const auto blurred = _dataMedia->thumbnailInline()) {
			return blurred->original();
		} else {
			return Image::Empty()->original();
		}
	}();
	auto args = Images::PrepareArgs();
	if (blurredValue) {
		args = args.blurred();
	}
	original = Images::Prepare(std::move(original), size * ratio, args);
	if (forumValue) {
		original = Images::Round(
			std::move(original),
			Images::CornersMask(std::min(size.width(), size.height())
				* Ui::ForumUserpicRadiusMultiplier()));
	} else {
		original = Images::Circle(std::move(original));
	}
	_imageCache = std::move(original);
	_imageCacheForum = forumValue;
	_imageCacheBlurred = blurredValue;
}

void Photo::validateImageCache(
		QSize outer,
		std::optional<Ui::BubbleRounding> rounding) const {
	const auto large = _dataMedia->image(PhotoSize::Large);
	const auto ratio = style::DevicePixelRatio();
	const auto blurredValue = large ? 0 : 1;
	if (_imageCache.size() == (outer * ratio)
		&& _imageCacheRounding == rounding
		&& _imageCacheBlurred == blurredValue) {
		return;
	}
	_imageCache = Images::Round(
		prepareImageCache(outer),
		MediaRoundingMask(rounding));
	_imageCacheRounding = rounding;
	_imageCacheBlurred = blurredValue;
}

void Photo::validateSpoilerImageCache(
		QSize outer,
		std::optional<Ui::BubbleRounding> rounding) const {
	Expects(_spoiler != nullptr);

	const auto ratio = style::DevicePixelRatio();
	if (_spoiler->background.size() == (outer * ratio)
		&& _spoiler->backgroundRounding == rounding) {
		return;
	}
	_spoiler->background = Images::Round(
		prepareImageCacheWithLarge(outer, nullptr),
		MediaRoundingMask(rounding));
	_spoiler->backgroundRounding = rounding;
}

QImage Photo::prepareImageCache(QSize outer) const {
	return prepareImageCacheWithLarge(
		outer,
		_dataMedia->image(PhotoSize::Large));
}

QImage Photo::prepareImageCacheWithLarge(QSize outer, Image *large) const {
	using Size = PhotoSize;
	auto blurred = (Image*)nullptr;
	if (const auto embedded = _dataMedia->thumbnailInline()) {
		blurred = embedded;
	} else if (const auto thumbnail = _dataMedia->image(Size::Thumbnail)) {
		blurred = thumbnail;
	} else if (const auto small = _dataMedia->image(Size::Small)) {
		blurred = small;
	} else {
		blurred = large;
	}
	const auto resize = large
		? ::Media::Streaming::DecideFrameResize(outer, large->size())
		: ::Media::Streaming::ExpandDecision();
	return PrepareWithBlurredBackground(outer, resize, large, blurred);
}

void Photo::paintUserpicFrame(
		Painter &p,
		QPoint photoPosition,
		bool markFrameShown) const {
	const auto autoplay = _data->videoCanBePlayed()
		&& videoAutoplayEnabled();
	const auto startPlay = autoplay && !_streamed;
	if (startPlay) {
		const_cast<Photo*>(this)->playAnimation(true);
	} else {
		checkStreamedIsStarted();
	}

	const auto size = QSize(width(), height());
	const auto rect = QRect(photoPosition, size);
	const auto forum = _parent->data()->history()->isForum();

	if (_streamed
		&& _streamed->instance.player().ready()
		&& !_streamed->instance.player().videoSize().isEmpty()) {
		const auto ratio = style::DevicePixelRatio();
		auto request = ::Media::Streaming::FrameRequest();
		request.outer = request.resize = size * ratio;
		if (forum) {
			const auto radius = int(std::min(size.width(), size.height())
				* Ui::ForumUserpicRadiusMultiplier());
			if (_streamed->roundingCorners[0].width() != radius * ratio) {
				_streamed->roundingCorners = Images::CornersMask(radius);
			}
			request.rounding = Images::CornersMaskRef(
				_streamed->roundingCorners);
		} else {
			if (_streamed->roundingMask.size() != request.outer) {
				_streamed->roundingMask = Images::EllipseMask(size);
			}
			request.mask = _streamed->roundingMask;
		}
		if (_streamed->instance.playerLocked()) {
			if (_streamed->frozenFrame.isNull()
				|| _streamed->frozenRequest != request) {
				_streamed->frozenRequest = request;
				_streamed->frozenFrame = _streamed->instance.frame(request);
			}
			p.drawImage(rect, _streamed->frozenFrame);
		} else {
			_streamed->frozenFrame = QImage();
			p.drawImage(rect, _streamed->instance.frame(request));
			if (markFrameShown) {
				_streamed->instance.markFrameShown();
			}
		}
		return;
	}
	validateUserpicImageCache(size, forum);
	p.drawImage(rect, _imageCache);
}

void Photo::paintUserpicFrame(
		Painter &p,
		const PaintContext &context,
		QPoint photoPosition) const {
	paintUserpicFrame(p, photoPosition, !context.paused);

	if (_data->videoCanBePlayed() && !_streamed) {
		const auto st = context.st;
		const auto sti = context.imageStyle();
		const auto innerSize = st::msgFileLayout.thumbSize;
		auto inner = QRect(
			photoPosition.x() + (width() - innerSize) / 2,
			photoPosition.y() + (height() - innerSize) / 2,
			innerSize,
			innerSize);
		p.setPen(Qt::NoPen);
		if (context.selected()) {
			p.setBrush(st->msgDateImgBgSelected());
		} else {
			const auto over = ClickHandler::showAsActive(_openl);
			p.setBrush(over ? st->msgDateImgBgOver() : st->msgDateImgBg());
		}
		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}
		sti->historyFileThumbPlay.paintInCenter(p, inner);
	}
}

QSize Photo::photoSize() const {
	if (_storyId) {
		return { kStoryWidth, kStoryHeight };
	}
	return QSize(_data->width(), _data->height());
}

QRect Photo::enlargeRect() const {
	const auto skip = st::historyPageEnlargeSkip;
	const auto enlargeInner = st::historyPageEnlargeSize;
	const auto enlargeOuter = 2 * skip + enlargeInner;
	return {
		width() - enlargeOuter + skip,
		skip,
		enlargeInner,
		enlargeInner,
	};
}

TextState Photo::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	} else if (_storyId && _data->isNull()) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	auto bubble = _parent->hasBubble();

	if (bubble && !_caption.isEmpty()) {
		const auto captionw = paintw
			- st::msgPadding.left()
			- st::msgPadding.right();
		painth -= _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			painth -= st::msgPadding.bottom();
		}
		if (QRect(st::msgPadding.left(), painth, captionw, height() - painth).contains(point)) {
			result = TextState(_parent, _caption.getState(
				point - QPoint(st::msgPadding.left(), painth),
				captionw,
				request.forText()));
			return result;
		}
		if (const auto botTop = _parent->Get<FakeBotAboutTop>()) {
			painth -= botTop->height;
		}
		painth -= st::mediaCaptionSkip;
	}
	if (QRect(paintx, painty, paintw, painth).contains(point)) {
		ensureDataMediaCreated();
		result.link = (_spoiler && !_spoiler->revealed)
			? _spoiler->link
			: _data->uploading()
			? _cancell
			: _dataMedia->loaded()
			? _openl
			: _data->loading()
			? _cancell
			: _savel;
		if (_showEnlarge
			&& result.link == _openl
			&& enlargeRect().contains(point)) {
			result.cursor = CursorState::Enlarge;
		}
	}
	if (_caption.isEmpty() && _parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = painty + painth;
		const auto bottomInfoResult = _parent->bottomInfoTextState(
			fullRight,
			fullBottom,
			point,
			InfoDisplayType::Image);
		if (bottomInfoResult.link
			|| bottomInfoResult.cursor != CursorState::None
			|| bottomInfoResult.customTooltip) {
			return bottomInfoResult;
		}
		if (const auto size = bubble ? std::nullopt : _parent->rightActionSize()) {
			auto fastShareLeft = _parent->hasRightLayout()
				? (paintx - size->width() - st::historyFastShareLeft)
				: (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - size->height());
			if (QRect(fastShareLeft, fastShareTop, size->width(), size->height()).contains(point)) {
				result.link = _parent->rightActionLink(point
					- QPoint(fastShareLeft, fastShareTop));
			}
		}
	}
	return result;
}

QSize Photo::sizeForGroupingOptimal(int maxWidth) const {
	const auto size = photoSize();
	return { std::max(size.width(), 1), std::max(size.height(), 1)};
}

QSize Photo::sizeForGrouping(int width) const {
	return sizeForGroupingOptimal(width);
}

void Photo::drawGrouped(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry,
		RectParts sides,
		Ui::BubbleRounding rounding,
		float64 highlightOpacity,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	ensureDataMediaCreated();
	_dataMedia->automaticLoad(_realParent->fullId(), _parent->data());

	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto loaded = _dataMedia->loaded();
	const auto displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_dataMedia->progress());
		}
	}
	const auto radial = isRadialAnimation();

	const auto revealed = _spoiler
		? _spoiler->revealAnimation.value(_spoiler->revealed ? 1. : 0.)
		: 1.;
	if (revealed < 1.) {
		validateSpoilerImageCache(geometry.size(), rounding);
	}
	if (revealed > 0.) {
		validateGroupedCache(geometry, rounding, cacheKey, cache);
		p.drawPixmap(geometry.topLeft(), *cache);
	}
	if (revealed < 1.) {
		p.setOpacity(1. - revealed);
		p.drawImage(geometry.topLeft(), _spoiler->background);
		fillImageSpoiler(p, _spoiler.get(), geometry, context);
		p.setOpacity(1.);
	}

	const auto overlayOpacity = context.selected()
		? (1. - highlightOpacity)
		: highlightOpacity;
	if (overlayOpacity > 0.) {
		p.setOpacity(overlayOpacity);
		fillImageOverlay(p, geometry, rounding, context);
		if (!context.selected()) {
			fillImageOverlay(p, geometry, rounding, context);
		}
		p.setOpacity(1.);
	}

	const auto displayState = radial
		|| (!loaded && !_data->loading())
		|| _data->waitingForAlbum();
	if (displayState) {
		const auto radialOpacity = radial
			? _animation->radial.opacity()
			: 1.;
		const auto backOpacity = (loaded && !_data->uploading())
			? radialOpacity
			: 1.;
		const auto radialSize = st::historyGroupRadialSize;
		const auto inner = QRect(
			geometry.x() + (geometry.width() - radialSize) / 2,
			geometry.y() + (geometry.height() - radialSize) / 2,
			radialSize,
			radialSize);
		p.setPen(Qt::NoPen);
		if (context.selected()) {
			p.setBrush(st->msgDateImgBgSelected());
		} else if (isThumbAnimation()) {
			auto over = _animation->a_thumbOver.value(1.);
			p.setBrush(anim::brush(st->msgDateImgBg(), st->msgDateImgBgOver(), over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st->msgDateImgBgOver() : st->msgDateImgBg());
		}

		p.setOpacity(backOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		const auto &icon = _data->waitingForAlbum()
			? sti->historyFileThumbWaiting
			: (radial || _data->loading())
			? sti->historyFileThumbCancel
			: sti->historyFileThumbDownload;
		const auto previous = _data->waitingForAlbum()
			? &sti->historyFileThumbCancel
			: nullptr;
		p.setOpacity(backOpacity);
		if (previous && radialOpacity > 0. && radialOpacity < 1.) {
			PaintInterpolatedIcon(p, icon, *previous, radialOpacity, inner);
		} else {
			icon.paintInCenter(p, inner);
		}
		p.setOpacity(1);
		if (radial) {
			const auto line = st::historyGroupRadialLine;
			const auto rinner = inner.marginsRemoved({ line, line, line, line });
			_animation->radial.draw(p, rinner, line, sti->historyFileThumbRadialFg);
		}
	}
}

TextState Photo::getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const {
	if (!geometry.contains(point)) {
		return {};
	}
	ensureDataMediaCreated();
	return TextState(_parent, (_spoiler && !_spoiler->revealed)
		? _spoiler->link
		: _data->uploading()
		? _cancell
		: _dataMedia->loaded()
		? _openl
		: _data->loading()
		? _cancell
		: _savel);
}

float64 Photo::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Photo::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool Photo::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool Photo::needInfoDisplay() const {
	if (_parent->data()->isFakeAboutView()) {
		return false;
	}
	return _parent->data()->isSending()
		|| _parent->data()->hasFailed()
		|| _parent->isUnderCursor()
		|| _parent->isLastAndSelfMessage();
}

void Photo::validateGroupedCache(
		const QRect &geometry,
		Ui::BubbleRounding rounding,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	using Option = Images::Option;

	ensureDataMediaCreated();

	const auto loaded = _dataMedia->loaded();
	const auto loadLevel = loaded
		? 2
		: (_dataMedia->thumbnailInline()
			|| _dataMedia->image(PhotoSize::Small)
			|| _dataMedia->image(PhotoSize::Thumbnail))
		? 1
		: 0;
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto options = (loaded ? Option() : Option::Blur);
	const auto key = (uint64(width) << 48)
		| (uint64(height) << 32)
		| (uint64(options) << 16)
		| (uint64(rounding.key()) << 8)
		| (uint64(loadLevel));
	if (*cacheKey == key) {
		return;
	}

	const auto unscaled = photoSize();
	const auto originalWidth = style::ConvertScale(unscaled.width());
	const auto originalHeight = style::ConvertScale(unscaled.height());
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ originalWidth, originalHeight },
		{ width, height });
	const auto ratio = style::DevicePixelRatio();
	const auto image = _dataMedia->image(PhotoSize::Large)
		? _dataMedia->image(PhotoSize::Large)
		: _dataMedia->image(PhotoSize::Thumbnail)
		? _dataMedia->image(PhotoSize::Thumbnail)
		: _dataMedia->image(PhotoSize::Small)
		? _dataMedia->image(PhotoSize::Small)
		: _dataMedia->thumbnailInline()
		? _dataMedia->thumbnailInline()
		: Image::BlankMedia().get();

	*cacheKey = key;
	auto scaled = Images::Prepare(
		image->original(),
		pixSize * ratio,
		{ .options = options, .outer = { width, height } });
	auto rounded = Images::Round(
		std::move(scaled),
		MediaRoundingMask(rounding));
	*cache = Ui::PixmapFromImage(std::move(rounded));
}

bool Photo::createStreamingObjects() {
	using namespace ::Media::Streaming;

	setStreamed(std::make_unique<Streamed>(
		history()->owner().streaming().sharedDocument(
			_data,
			_realParent->fullId())));
	_streamed->instance.player().updates(
	) | rpl::start_with_next_error([=](Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](Error &&error) {
		handleStreamingError(std::move(error));
	}, _streamed->instance.lifetime());
	if (_streamed->instance.ready()) {
		streamingReady(base::duplicate(_streamed->instance.info()));
	}
	if (!_streamed->instance.valid()) {
		stopAnimation();
		return false;
	}
	checkStreamedIsStarted();
	return true;
}

void Photo::setStreamed(std::unique_ptr<Streamed> value) {
	const auto removed = (_streamed && !value);
	const auto set = (!_streamed && value);
	_streamed = std::move(value);
	if (set) {
		history()->owner().registerHeavyViewPart(_parent);
		togglePollingStory(true);
	} else if (removed) {
		_parent->checkHeavyPart();
	}
}

void Photo::handleStreamingUpdate(::Media::Streaming::Update &&update) {
	using namespace ::Media::Streaming;

	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [&](const PreloadedVideo &update) {
	}, [&](const UpdateVideo &update) {
		repaintStreamedContent();
	}, [&](const PreloadedAudio &update) {
	}, [&](const UpdateAudio &update) {
	}, [&](const WaitingForData &update) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
	});
}

void Photo::handleStreamingError(::Media::Streaming::Error &&error) {
	_data->setVideoPlaybackFailed();
	stopAnimation();
}

void Photo::repaintStreamedContent() {
	if (_streamed && !_streamed->frozenFrame.isNull()) {
		return;
	} else if (_parent->delegate()->elementAnimationsPaused()) {
		return;
	}
	repaint();
}

void Photo::streamingReady(::Media::Streaming::Information &&info) {
	repaint();
}

void Photo::checkAnimation() {
	if (_streamed && !videoAutoplayEnabled()) {
		stopAnimation();
	}
}

void Photo::stopAnimation() {
	setStreamed(nullptr);
}

void Photo::playAnimation(bool autoplay) {
	ensureDataMediaCreated();
	if (_streamed && autoplay) {
		return;
	} else if (_streamed && videoAutoplayEnabled()) {
		showPhoto(_parent->data()->fullId());
		return;
	}
	if (_streamed) {
		stopAnimation();
	} else if (_data->videoCanBePlayed()) {
		if (!videoAutoplayEnabled()) {
			history()->owner().checkPlayingAnimations();
		}
		if (!createStreamingObjects()) {
			_data->setVideoPlaybackFailed();
			return;
		}
	}
}

void Photo::checkStreamedIsStarted() const {
	if (!_streamed) {
		return;
	} else if (_streamed->instance.paused()) {
		_streamed->instance.resume();
	}
	if (_streamed
		&& !_streamed->instance.active()
		&& !_streamed->instance.failed()) {
		const auto position = _data->videoStartPosition();
		auto options = ::Media::Streaming::PlaybackOptions();
		options.position = position;
		options.mode = ::Media::Streaming::Mode::Video;
		options.loop = true;
		_streamed->instance.play(options);
	}
}

bool Photo::videoAutoplayEnabled() const {
	return Data::AutoDownload::ShouldAutoPlay(
		_data->session().settings().autoDownload(),
		_realParent->history()->peer,
		_data);
}

TextForMimeData Photo::selectedText(TextSelection selection) const {
	return _caption.toTextForMimeData(selection);
}

SelectedQuote Photo::selectedQuote(TextSelection selection) const {
	return Element::FindSelectedQuote(_caption, selection, _realParent);
}

TextSelection Photo::selectionFromQuote(const SelectedQuote &quote) const {
	return Element::FindSelectionFromQuote(_caption, quote);
}

void Photo::hideSpoilers() {
	_caption.setSpoilerRevealed(false, anim::type::instant);
	if (_spoiler) {
		_spoiler->revealed = false;
	}
}

bool Photo::needsBubble() const {
	if (_storyId || !_caption.isEmpty()) {
		return true;
	}
	const auto item = _parent->data();
	return !item->isService()
		&& (item->repliesAreComments()
			|| item->externalReply()
			|| item->viaBot()
			|| _parent->displayReply()
			|| _parent->displayForwardedFrom()
			|| _parent->displayFromName()
			|| _parent->displayedTopicButton());
}

QPoint Photo::resolveCustomInfoRightBottom() const {
	const auto skipx = (st::msgDateImgDelta + st::msgDateImgPadding.x());
	const auto skipy = (st::msgDateImgDelta + st::msgDateImgPadding.y());
	return QPoint(width() - skipx, height() - skipy);
}

bool Photo::isReadyForOpen() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

void Photo::parentTextUpdated() {
	_caption = (_parent->media() == this)
		? createCaption(_parent->data())
		: Ui::Text::String();
	history()->owner().requestViewResize(_parent);
}

void Photo::showPhoto(FullMsgId id) {
	_parent->delegate()->elementOpenPhoto(_data, id);
}

} // namespace HistoryView
