/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_gif.h"

#include "apiwrap.h"
#include "api/api_transcribes.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "media/audio/media_audio.h"
#include "media/clip/media_clip_reader.h"
#include "media/media_common.h"
#include "media/player/media_player_instance.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_utility.h"
#include "media/view/media_view_open_common.h"
#include "media/view/media_view_playback_progress.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_transcribe_button.h"
#include "history/view/media/history_view_document.h" // TTLVoiceStops
#include "history/view/media/history_view_ephemeral_plate.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_media_spoiler.h"
#include "history/view/media/history_view_video_message_seek.h"
#include "history/view/media/history_view_video_status.h"
#include "window/window_session_controller.h"
#include "core/application.h" // Application::showDocument.
#include "core/core_settings.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/image/image.h"
#include "ui/text/format_values.h"
#include "ui/grouped_layout.h"
#include "ui/cached_round_corners.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/spoiler_mess.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_streaming.h"
#include "data/data_document.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_document_media.h"
#include "data/data_web_page.h"
#include "storage/storage_account.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"

#include <QSvgRenderer>
#include <QtWidgets/QApplication>

namespace HistoryView {
namespace {

constexpr auto kMaxGifForwardedBarLines = 4;
constexpr auto kUseNonBlurredThreshold = 240;
constexpr auto kMaxInlineArea = 1920 * 1080;
constexpr auto kMaxInstantViewInlineArea = 1920 * 1920;
constexpr auto kSeekPreviewInterval = crl::time(100);

using ::Media::ValidFrameSize;

[[nodiscard]] bool IsHostedInstantViewMedia(not_null<const Element*> parent) {
	return parent->Get<InstantViewMediaRuntime>() != nullptr;
}

[[nodiscard]] double HostedInstantViewMediaPixelScale(
		not_null<const Element*> parent) {
	const auto runtime = parent->Get<InstantViewMediaRuntime>();
	return runtime ? runtime->mediaPixelScale : 1.;
}

[[nodiscard]] QSize ScaledInstantViewMediaSize(QSize size, double scale) {
	return (scale == 1.)
		? size
		: QSize(
			std::max(qRound(size.width() * scale), 1),
			std::max(qRound(size.height() * scale), 1));
}

[[nodiscard]] QSize HostedInstantViewForcedSize(
		not_null<const Element*> parent,
		not_null<const Media*> media) {
	const auto runtime = parent->Get<InstantViewMediaRuntime>();
	return (runtime && runtime->forcedFor == media)
		? runtime->forcedSize
		: QSize();
}

[[nodiscard]] int GifMaxStatusWidth(not_null<DocumentData*> document) {
	auto result = st::normalFont->width(
		Ui::FormatDownloadText(document->size, document->size));
	accumulate_max(
		result,
		st::normalFont->width(Ui::FormatGifAndSizeText(document->size)));
	return result;
}

[[nodiscard]] HistoryView::TtlRoundPaintCallback CreateTtlPaintCallback(
		Fn<void()> update) {
	const auto centerMargins = Margins(st::historyFileInPause.width() * 3);

	const auto renderer = std::make_shared<QSvgRenderer>(
		u":/gui/ttl/video_message_icon.svg"_q);

	return [=](QPainter &p, QRect r, const PaintContext &context) {
		const auto centerRect = r - centerMargins;
		const auto &icon = context.imageStyle()->historyVideoMessageTtlIcon;
		const auto iconRect = QRect(
			rect::right(centerRect) - icon.width() * 0.75,
			rect::bottom(centerRect) - icon.height() * 0.75,
			icon.width(),
			icon.height());
		{
			auto hq = PainterHighQualityEnabler(p);
			auto path = QPainterPath();
			path.setFillRule(Qt::WindingFill);
			path.addEllipse(centerRect);
			path.addEllipse(iconRect);
			p.fillPath(path, st::shadowFg);
			p.fillPath(path, st::shadowFg);
			p.fillPath(path, st::shadowFg);
		}

		renderer->render(&p, centerRect - Margins(centerRect.width() / 4));

		icon.paint(p, iconRect.topLeft(), centerRect.width());
	};
}

} // namespace

struct Gif::Streamed {
	Streamed(
		not_null<DocumentData*> chosen,
		std::shared_ptr<::Media::Streaming::Document> shared,
		Fn<void()> waitingCallback);
	const not_null<DocumentData*> chosen;
	::Media::Streaming::Instance instance;
	::Media::Streaming::FrameRequest frozenRequest;
	QImage frozenFrame;
	QString frozenStatusText;
};

Gif::Streamed::Streamed(
	not_null<DocumentData*> chosen,
	std::shared_ptr<::Media::Streaming::Document> shared,
	Fn<void()> waitingCallback)
: chosen(chosen)
, instance(std::move(shared), std::move(waitingCallback)) {
}

[[nodiscard]] bool IsHiddenRoundMessage(not_null<Element*> parent) {
	return parent->delegate()->elementContext() != Context::TTLViewer
		&& parent->data()->media()
		&& parent->data()->media()->ttlSeconds();
}

Gif::Gif(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<DocumentData*> document,
	bool spoiler)
: File(parent, realParent)
, _data(document)
, _videoCover(LookupVideoCover(document, realParent))
, _storyId(realParent->media()
	? realParent->media()->storyId()
	: FullStoryId())
, _spoiler((spoiler
	|| IsHiddenRoundMessage(_parent)
	|| realParent->isMediaSensitive())
	? std::make_unique<MediaSpoiler>()
	: nullptr)
, _downloadSize(Ui::FormatSizeText(_data->size))
, _videoTimestamp(::Media::View::ExtractVideoTimestamp(realParent))
, _sensitiveSpoiler(realParent->isMediaSensitive())
, _ttlCover(realParent->isTtlCoveredMedia())
, _hasVideoCover(realParent->media() && realParent->media()->videoCover()) {
	const auto media = _parent->data()->media();
	if (_data->isVideoMessage() && media && media->ttlSeconds()) {
		if (_spoiler) {
			_drawTtl = CreateTtlPaintCallback([=] { repaint(); });
		}
		const auto fullId = _realParent->fullId();
		const auto &data = &_parent->data()->history()->owner();
		const auto isOut = _parent->data()->out();
		_parent->data()->removeFromSharedMediaIndex();
		setDocumentLinks(_data, realParent, [=] {
			auto lifetime = std::make_shared<rpl::lifetime>();
			TTLVoiceStops(fullId) | rpl::on_next([=]() mutable {
				if (lifetime) {
					base::take(lifetime)->destroy();
				}
				if (!isOut) {
					if (const auto item = data->message(fullId)) {
						// Destroys this.
						item->clearMediaAsExpired();
					}
				}
			}, *lifetime);

			return false;
		});
	} else {
		setDocumentLinks(_data, realParent, [=] {
			if (!_data->createMediaView()->canBePlayed()
				|| !_data->isAnimation()
				|| _data->isVideoMessage()
				|| !canPlayInline()) {
				return false;
			}
			playAnimation(false);
			return true;
		});
	}

	setStatusSize(Ui::FileStatusSizeReady);

	if (_data->isVideoMessage()) {
		_roundSeek = std::make_unique<VideoMessageSeek>([=] { repaint(); });
		if (!media || !media->ttlSeconds()) {
			_seekl = std::make_shared<VoiceSeekClickHandler>(
				_data,
				[](FullMsgId) {});
		}
	}

	if (_spoiler) {
		createSpoilerLink(_spoiler.get());
	}

	if ((_dataMedia = _data->activeMediaView())) {
		dataMediaCreated();
	} else if (_videoCover) {
		if (_videoCover->inlineThumbnailBytes().isEmpty()
			&& (_videoCover->hasExact(Data::PhotoSize::Small)
				|| _videoCover->hasExact(Data::PhotoSize::Thumbnail))) {
			_videoCover->load(Data::PhotoSize::Small, realParent->fullId());
		}
	} else {
		_data->loadThumbnail(realParent->fullId());
		if (!autoplayEnabled()) {
			_data->loadVideoThumbnail(realParent->fullId());
		}
	}
	ensureTranscribeButton();

	_purchasedPriceTag = hasPurchasedTag();
}

Gif::~Gif() {
	if (_streamed || _dataMedia) {
		if (_streamed) {
			_data->owner().streaming().keepAlive(_data);
			setStreamed(nullptr);
		}
		if (_dataMedia) {
			_data->owner().keepAlive(base::take(_dataMedia));
			_parent->checkHeavyPart();
		}
	}
	togglePollingStory(false);
}

DocumentData *Gif::ChooseInlineQuality(
		not_null<DocumentData*> document,
		HistoryItem *context,
		int maxArea,
		::Media::VideoQuality request) {
	const auto fits = [&](not_null<DocumentData*> quality) {
		return ValidFrameSize(quality->dimensions, maxArea)
			&& (quality == document
				|| (quality->useStreamingLoader()
					&& quality->canBeStreamed()
					&& !quality->inappPlaybackFailed()));
	};
	const auto &list = document->resolveQualities(context);
	if (list.empty()) {
		return fits(document) ? document.get() : nullptr;
	}
	const auto chosen = document->chooseQuality(context, request);
	if (fits(chosen)) {
		return chosen;
	}

	// The requested rendition does not fit inline: either the settings ask
	// for the original, which they do as soon as the original file is on
	// disk, or the closest match by height is itself over the area cap.
	// Fall back to the largest rendition that does fit, measured by the
	// same area the cap is expressed in, because resolveVideoQuality()
	// reports the largest packed height for a self-packed original rather
	// than the original's own.
	auto result = (DocumentData*)nullptr;
	auto resultArea = int64(0);
	for (const auto &quality : list) {
		const auto area = int64(quality->dimensions.width())
			* quality->dimensions.height();
		if (area > resultArea && fits(quality)) {
			result = quality;
			resultArea = area;
		}
	}
	return result;
}

int Gif::maxInlineArea() const {
	return IsHostedInstantViewMedia(_parent)
		? kMaxInstantViewInlineArea
		: kMaxInlineArea;
}

bool Gif::canPlayInline() const {
	return ChooseInlineQuality(
		_data,
		_realParent,
		maxInlineArea(),
		Core::App().settings().videoQuality()) != nullptr;
}

QSize Gif::sizeForAspectRatio() const {
	// We use size only for aspect ratio and we want to have it
	// as close to the thumbnail as possible.
	//if (!_data->dimensions.isEmpty()) {
	//	return _data->dimensions;
	//}
	if (_data->hasThumbnail()) {
		const auto &location = _data->thumbnailLocation();
		return NonEmptySize({ location.width(), location.height() });
	}
	return { 1, 1 };
}

QSize Gif::countThumbSize(int &inOutWidthMax) const {
	const auto hostedInstantView = IsHostedInstantViewMedia(_parent);
	const auto maxSize = [&] {
		if (hostedInstantView) {
			return std::max(inOutWidthMax, 1);
		} else if (_data->isVideoFile()) {
			return st::maxMediaSize;
		} else if (_data->isVideoMessage()) {
			return st::maxVideoMessageSize;
		}
		return st::maxGifSize;
	}();
	const auto size = style::ConvertScale(videoSize());
	if (hostedInstantView) {
		inOutWidthMax = std::max(inOutWidthMax, 1);
	} else {
		accumulate_min(inOutWidthMax, maxSize);
	}
	return DownscaledSize(size, { inOutWidthMax, maxSize });
}

QSize Gif::countOptimalSize() {
	if (_data->isVideoMessage() && _transcribe) {
		const auto &entry = _data->session().api().transcribes().entry(
			_realParent);
		_transcribe->setLoading(
			entry.shown && (entry.requestId || entry.pending));
	}

	if (const auto forced = HostedInstantViewForcedSize(_parent, this)
		; !forced.isEmpty()) {
		return forced;
	}
	const auto hostedInstantView = IsHostedInstantViewMedia(_parent);
	const auto maxMediaWidth = hostedInstantView
		? std::max(st::msgMaxWidth, st::maxMediaSize)
		: st::maxMediaSize;
	const auto minWidth = std::clamp(
		_parent->minWidthForMedia(),
		(_parent->hasBubble()
			? st::historyPhotoBubbleMinWidth
			: st::minPhotoSize),
		maxMediaWidth);
	auto thumbMaxWidth = st::msgMaxWidth;
	const auto scaled = countThumbSize(thumbMaxWidth);
	auto maxWidth = std::min(
		std::max(scaled.width(), minWidth),
		thumbMaxWidth);
	auto minHeight = qMax(scaled.height(), st::minPhotoSize);
	if (!activeCurrentStreamed()) {
		accumulate_max(
			maxWidth,
			GifMaxStatusWidth(_data)
				+ 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (_parent->hasBubble()) {
		maxWidth = qMax(maxWidth, _parent->textualMaxWidth());
		minHeight = adjustHeightForLessCrop(
			scaled,
			{ maxWidth, minHeight });
	} else if (isUnwrapped()) {
		const auto item = _parent->data();
		auto via = item->Get<HistoryMessageVia>();
		auto reply = _parent->Get<Reply>();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		if (forwarded) {
			forwarded->create(via, item);
		}
		RefreshEphemeralPlate(_parent, _ephemeral.text);
		maxWidth += additionalWidth(reply, via, forwarded);
		accumulate_max(maxWidth, _parent->reactionsOptimalWidth());
	}
	return { maxWidth, minHeight };
}

QSize Gif::countCurrentSize(int newWidth) {
	if (const auto forced = HostedInstantViewForcedSize(_parent, this)
		; !forced.isEmpty()) {
		return forced;
	}
	auto availableWidth = newWidth;
	_ephemeral.onTop = false;
	_ephemeral.topAdded = 0;

	const auto hostedInstantView = IsHostedInstantViewMedia(_parent);
	auto thumbMaxWidth = newWidth;
	const auto scaled = countThumbSize(thumbMaxWidth);
	const auto minWidthByInfo = hostedInstantView
		? _parent->minWidthForMedia()
		: (_parent->hidesBottomInfo()
			? 0
			: (_parent->infoWidth()
				+ 2 * (st::msgDateImgDelta
					+ st::msgDateImgPadding.x())));
	const auto minPhotoWidth = std::min(st::minPhotoSize, thumbMaxWidth);
	newWidth = std::clamp(
		std::max(scaled.width(), minWidthByInfo),
		minPhotoWidth,
		thumbMaxWidth);
	auto newHeight = qMax(scaled.height(), st::minPhotoSize);
	if (!activeCurrentStreamed()) {
		accumulate_max(
			newWidth,
			GifMaxStatusWidth(_data)
				+ 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (_parent->hasBubble()) {
		accumulate_max(newWidth, _parent->minWidthForMedia());
		auto captionMaxWidth = _parent->textualMaxWidth();
		const auto botTop = _parent->Get<FakeBotAboutTop>();
		if (botTop) {
			accumulate_max(captionMaxWidth, botTop->maxWidth);
		}
		const auto maxWithCaption = qMin(st::msgMaxWidth, captionMaxWidth);
		newWidth = qMin(qMax(newWidth, maxWithCaption), thumbMaxWidth);
		newHeight = adjustHeightForLessCrop(
			scaled,
			{ newWidth, newHeight });
	} else if (isUnwrapped()) {
		accumulate_max(newWidth, _parent->reactionsOptimalWidth());

		const auto item = _parent->data();
		auto via = item->Get<HistoryMessageVia>();
		auto reply = _parent->Get<Reply>();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		RefreshEphemeralPlate(_parent, _ephemeral.text);
		if (via || reply || forwarded || !_ephemeral.text.isEmpty()) {
			auto additional = additionalWidth(reply, via, forwarded);
			newWidth += additional;
			accumulate_min(newWidth, availableWidth);
			const auto usew = maxWidth() - additional;
			if (!_ephemeral.text.isEmpty()) {
				const auto contentWidth = _data->isVideoMessage()
					? std::min(usew, newHeight)
					: usew;
				const auto sideRoom = newWidth
					- contentWidth
					- st::msgReplyPadding.left();
				_ephemeral.onTop = (sideRoom
					< EphemeralPlateMaxWidth(_ephemeral.text));
			}
			const auto rectw = _ephemeral.onTop
				? std::min(newWidth - st::msgReplyPadding.left(), additional)
				: (newWidth - usew - st::msgReplyPadding.left());
			const auto availw = rectw
				- st::msgReplyPadding.left()
				- st::msgReplyPadding.left();
			if (!forwarded && via) {
				via->resize(availw);
			}
			if (reply) {
				[[maybe_unused]] int height = reply->resizeToWidth(availw);
			}
			if (_ephemeral.onTop) {
				const auto plate = EphemeralPlateSize(
					_ephemeral.text,
					newWidth - st::msgReplyPadding.left());
				_ephemeral.topAdded = plate.height()
					+ st::msgReplyPadding.top()
					+ ((via || reply || forwarded)
						? surroundingHeight(reply, via, forwarded, rectw)
						: 0);
				newHeight += _ephemeral.topAdded;
			}
		}
	}

	return { newWidth, newHeight };
}

int Gif::adjustHeightForLessCrop(QSize dimensions, QSize current) const {
	if (dimensions.isEmpty()) {
		return current.height();
	}
	// Allow some more vertical space for less cropping,
	// but not more than 1.33 * existing height.
	return qMax(
		current.height(),
		qMin(
			current.width() * dimensions.height() / dimensions.width(),
			current.height() * 4 / 3));
}

QSize Gif::videoSize() const {
	if (const auto streamed = activeCurrentStreamed()) {
		return streamed->player().videoSize();
	} else if (!_data->dimensions.isEmpty()) {
		return _data->dimensions;
	} else if (_data->hasThumbnail()) {
		const auto &location = _data->thumbnailLocation();
		return QSize(location.width(), location.height());
	} else {
		return QSize(1, 1);
	}
}

void Gif::validateRoundingMask(QSize size) const {
	if (_roundingMask.size() != size) {
		const auto ratio = style::DevicePixelRatio();
		_roundingMask = Images::EllipseMask(size / ratio);
	}
}

bool Gif::downloadInCorner() const {
	return _data->isVideoFile()
		&& (_data->loading() || !autoplayEnabled())
		&& _realParent->allowsMediaDownloadControls()
		&& _data->canBeStreamed()
		&& !_data->inappPlaybackFailed();
}

bool Gif::autoplayUnderCursor() const {
	return (_videoTimestamp || _hasVideoCover);
}

bool Gif::underCursor(bool fullFeatured) const {
	return ClickHandler::getActive() == currentVideoLink(fullFeatured);
}

bool Gif::autoplayEnabled() const {
	if (_realParent->isSponsored()) {
		return true;
	}
	return Data::AutoDownload::ShouldAutoPlay(
		_data->session().settings().autoDownload(),
		_realParent->history()->peer,
		_data);
}

bool Gif::autoplayEligible(bool fullFeatured) const {
	ensureDataMediaCreated();
	return fullFeatured
		&& autoplayEnabled()
		&& _dataMedia->canBePlayed()
		&& canPlayInline()
		&& !(_data->uploading() && _data->uploadingData->preparing);
}

float64 Gif::revealedProgress() const {
	const auto item = _parent->data();
	const auto isRound = _data->isVideoMessage();
	const auto inTTLViewer = _parent->delegate()->elementContext()
		== Context::TTLViewer;
	return ((isRound || _ttlCover)
		&& item->media()
		&& item->media()->ttlSeconds()
		&& !inTTLViewer)
		? 0.
		: (!isRound && _spoiler)
		? _spoiler->revealAnimation.value(_spoiler->revealed ? 1. : 0.)
		: 1.;
}

bool Gif::hideMessageText() const {
	return _data->isVideoMessage();
}

void Gif::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_smallGroupPart = false;

	ensureDataMediaCreated();
	const auto item = _parent->data();
	const auto loaded = dataLoaded();
	const auto displayLoading = (item->isSending() || _data->displayLoading());
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto cornerDownload = downloadInCorner();
	const auto autoplay = autoplayEligible(true);
	const auto activeRoundPlaying = activeRoundStreamed();

	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	const bool bubble = _parent->hasBubble();
	const auto rightLayout = _parent->hasRightLayout();
	const auto inWebPage = (_parent->media() != this);
	const auto isRound = _data->isVideoMessage();
	const auto hostedInstantView = IsHostedInstantViewMedia(_parent);

	const auto inWebPageWithoutOwnRounding = inWebPage
		&& bubbleRounding() == Ui::BubbleRounding();
	const auto rounding = hostedInstantView
		? std::optional<Ui::BubbleRounding>(Ui::BubbleRounding())
		: inWebPageWithoutOwnRounding
		? std::optional<Ui::BubbleRounding>()
		: adjustedBubbleRounding();

	auto usex = 0, usew = paintw;
	const auto unwrapped = isUnwrapped();
	const auto via = unwrapped ? item->Get<HistoryMessageVia>() : nullptr;
	const auto reply = unwrapped ? _parent->Get<Reply>() : nullptr;
	const auto forwarded = unwrapped ? item->Get<HistoryMessageForwarded>() : nullptr;
	const auto rightAligned = unwrapped && rightLayout;
	if (via || reply || forwarded || !_ephemeral.text.isEmpty()) {
		usew = maxWidth() - additionalWidth(reply, via, forwarded);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (_ephemeral.onTop) {
		painty += _ephemeral.topAdded;
		painth -= _ephemeral.topAdded;
	}
	if (isRound) {
		accumulate_min(usew, painth);
	}
	if (rtl()) usex = width() - usex - usew;

	QRect rthumb(style::rtlrect(usex + paintx, painty, usew, painth, width()));

	const auto inTTLViewer = _parent->delegate()->elementContext()
		== Context::TTLViewer;
	const auto revealed = revealedProgress();
	const auto fullHiddenBySpoiler = (revealed == 0.);
	if (revealed < 1.) {
		validateSpoilerImageCache(rthumb.size(), rounding);
	}

	const auto canStartPlay = autoplay
		&& !_streamed
		&& !activeRoundPlaying
		&& !_seeking
		&& !fullHiddenBySpoiler;
	const auto shouldBePlaying = !autoplayUnderCursor() || underCursor(true);
	if (!shouldBePlaying && _videoTimestamp != 0) {
		const_cast<Gif*>(this)->stopAnimation();
	} else if (canStartPlay) {
		const_cast<Gif*>(this)->playAnimation(true);
	} else {
		checkStreamedIsStarted();
	}
	const auto streamingMode = _streamed || activeRoundPlaying || autoplay;
	const auto activeOwnPlaying = activeOwnStreamed();

	auto displayMute = false;
	const auto streamed = activeRoundPlaying
		? activeRoundPlaying
		: activeOwnPlaying
		? &activeOwnPlaying->instance
		: nullptr;
	const auto streamedForWaiting = activeRoundPlaying
		? activeRoundPlaying
		: _streamed
		? &_streamed->instance
		: nullptr;

	if (displayLoading
		&& (!streamedForWaiting
			|| item->isSending()
			|| _data->uploading()
			|| (cornerDownload && _data->loading()))) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(dataProgress());
		}
	}
	updateStatusText();
	const auto radial = isRadialAnimation()
		|| (streamedForWaiting && streamedForWaiting->waitingShown());

	if (!bubble && !unwrapped && !hostedInstantView) {
		Assert(rounding.has_value());
		fillImageShadow(p, rthumb, *rounding, context);
	}

	const auto skipDrawingContent = context.skipDrawingParts
		== PaintContext::SkipDrawingParts::Content;
	const auto drawStreamed = streamed
		&& (shouldBePlaying || !_videoCover)
		&& (activeRoundPlaying || !_seeking);
	if (drawStreamed && !skipDrawingContent && !fullHiddenBySpoiler) {
		if (!_seekLastFrame.isNull()) {
			_seekLastFrame = QImage();
		}
		auto paused = context.paused || !shouldBePlaying;
		auto request = ::Media::Streaming::FrameRequest{
			.outer = (ScaledInstantViewMediaSize(
				QSize(usew, painth),
				HostedInstantViewMediaPixelScale(_parent))
				* style::DevicePixelRatio()),
			.blurredBackground = true,
		};
		if (isRound) {
			if (activeRoundStreamed()) {
				paused = false;
			} else {
				displayMute = true;
			}
			validateRoundingMask(request.outer);
			request.mask = _roundingMask;
		} else {
			request.rounding = MediaRoundingMask(rounding);
		}
		if (!activeRoundPlaying && activeOwnPlaying->instance.playerLocked()) {
			if (activeOwnPlaying->frozenFrame.isNull()) {
				activeOwnPlaying->frozenRequest = request;
				activeOwnPlaying->frozenFrame = streamed->frame(request);
				activeOwnPlaying->frozenStatusText = _statusText;
			} else if (activeOwnPlaying->frozenRequest != request) {
				activeOwnPlaying->frozenRequest = request;
				activeOwnPlaying->frozenFrame = streamed->frame(request);
			}
			p.drawImage(rthumb, activeOwnPlaying->frozenFrame);
		} else {
			if (activeOwnPlaying
				&& !activeOwnPlaying->frozenFrame.isNull()) {
				activeOwnPlaying->frozenFrame = QImage();
				activeOwnPlaying->frozenStatusText = QString();
			}

			const auto frame = streamed->frameWithInfo(request);
			p.drawImage(rthumb, frame.image);
			if (_seeking) {
				_seekLastFrame = frame.image;
			}
			if (!paused) {
				streamed->markFrameShown();
			}
		}
	} else if (!_seekLastFrame.isNull()
			&& !skipDrawingContent
			&& !fullHiddenBySpoiler) {
		p.drawImage(rthumb, _seekLastFrame);
	} else if (!skipDrawingContent && !fullHiddenBySpoiler) {
		ensureDataMediaCreated();
		validateThumbCache({ usew, painth }, isRound, rounding);
		p.drawImage(rthumb, _thumbCache);
	}
	if (isRound) {
		paintRoundPlaybackProgress(p, context, rthumb, inTTLViewer);
	}
	if (!isRound) {
		paintTimestampMark(p, rthumb, rounding);
	}

	if (revealed < 1.) {
		p.setOpacity(1. - revealed);
		if (!isRound) {
			p.drawImage(rthumb, _spoiler->background);
			fillImageSpoiler(p, _spoiler.get(), rthumb, context);
		} else {
			auto frame = _spoiler->background;
			{
				auto q = QPainter(&frame);
				fillImageSpoiler(q, _spoiler.get(), rthumb, context);
			}
			p.drawImage(rthumb.topLeft(), Images::Circle(std::move(frame)));
		}
		p.setOpacity(1.);
	}
	if (context.selected()) {
		if (isRound) {
			Ui::FillComplexEllipse(p, st, rthumb);
		} else {
			fillImageOverlay(p, rthumb, rounding, context);
		}
	}

	const auto ttlCovered = _ttlCover && (revealed < 1.);
	const auto paintInCenter = !_sensitiveSpoiler
		&& (radial
			|| (!streamingMode
				&& ((!loaded && !_data->loading()) || !autoplay))
			|| ttlCovered);
	if (paintInCenter) {
		const auto radialRevealed = 1.;
		const auto opacity = (item->isSending() || _data->uploading())
			? 1.
			: streamedForWaiting
			? streamedForWaiting->waitingOpacity()
			: (radial && loaded)
			? _animation->radial.opacity()
			: 1.;
		const auto radialOpacity = opacity * radialRevealed;
		const auto innerSize = st::msgFileLayout.thumbSize;
		auto inner = QRect(rthumb.x() + (rthumb.width() - innerSize) / 2, rthumb.y() + (rthumb.height() - innerSize) / 2, innerSize, innerSize);
		p.setPen(Qt::NoPen);
		if (context.selected()) {
			p.setBrush(st->msgDateImgBgSelected());
		} else if (isThumbAnimation()) {
			auto over = _animation->a_thumbOver.value(1.);
			p.setBrush(anim::brush(st->msgDateImgBg(), st->msgDateImgBgOver(), over));
		} else {
			const auto over = ClickHandler::showAsActive(
				(_data->loading() || _data->uploading()) ? _cancell : _savel);
			p.setBrush(over ? st->msgDateImgBgOver() : st->msgDateImgBg());
		}
		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		const auto icon = [&]() -> const style::icon * {
			switch (currentAction(true)) {
			case Action::None:
			case Action::Streaming: return nullptr;
			case Action::Open: return &sti->historyFileThumbPlay;
			case Action::Cancel: return &sti->historyFileThumbCancel;
			case Action::Download: return &sti->historyFileThumbDownload;
			}
			Unexpected("Action in Gif::draw.");
		}();
		if (ttlCovered && !radial && !_data->loading()) {
			paintTtlFire(p, inner);
			paintTtlCountdown(
				p,
				inner,
				st::msgFileRadialLine,
				sti->historyFileThumbRadialFg,
				context.paused);
			PaintTtlSingleViewBadge(p, inner, _realParent, context);
		} else if (icon) {
			icon->paintInCenter(p, inner);
		}
		p.setOpacity(radialRevealed);
		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			if (streamedForWaiting && !_data->uploading()) {
				Ui::InfiniteRadialAnimation::Draw(
					p,
					streamedForWaiting->waitingState(),
					rinner.topLeft(),
					rinner.size(),
					width(),
					sti->historyFileThumbRadialFg,
					st::msgFileRadialLine);
			} else if (!cornerDownload) {
				_animation->radial.draw(
					p,
					rinner,
					st::msgFileRadialLine,
					sti->historyFileThumbRadialFg);
			}
		}
		p.setOpacity(1.);
	} else if (_sensitiveSpoiler) {
		drawSpoilerTag(p, rthumb, context, [&] {
			return spoilerTagBackground();
		});
	}
	if (displayMute) {
		auto muteRect = style::rtlrect(rthumb.x() + (rthumb.width() - st::historyVideoMessageMuteSize) / 2, rthumb.y() + st::msgDateImgDelta, st::historyVideoMessageMuteSize, st::historyVideoMessageMuteSize, width());
		p.setPen(Qt::NoPen);
		p.setBrush(sti->msgDateImgBg);
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(muteRect);
		sti->historyVideoMessageMute.paintInCenter(p, muteRect);
	}

	const auto skipDrawingSurrounding = context.skipDrawingParts
		== PaintContext::SkipDrawingParts::Surrounding;

	if (!skipDrawingSurrounding && _purchasedPriceTag) {
		drawPurchasedTag(p, rthumb, context);
	}

	if (!unwrapped && !skipDrawingSurrounding) {
		const auto sponsoredSkip = !_data->isVideoFile()
			&& _realParent->isSponsored();
		if ((!isRound || !inWebPage) && !sponsoredSkip) {
			if (ttlCovered) {
				PaintTtlLabel(p, QPoint(), width(), _realParent, context);
			} else {
				drawCornerStatus(p, context, QPoint());
			}
		}
	} else if (!skipDrawingSurrounding) {
		if (isRound) {
			const auto mediaUnread = item->hasUnreadMediaFlag();
			const auto statusText = _seeking
				? Ui::FormatDurationText(1 + int64(base::SafeRound(
					(1. - _roundSeek->progress())
						* _data->duration()
						/ 1000.)))
				: _statusText;
			auto statusW = st::normalFont->width(statusText) + 2 * st::msgDateImgPadding.x();
			auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			auto statusX = usex + paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
			auto statusY = painty + painth - st::msgDateImgDelta - statusH + st::msgDateImgPadding.y();
			if (mediaUnread) {
				statusW += st::mediaUnreadSkip + st::mediaUnreadSize;
			}
			Ui::FillRoundRect(p, style::rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), sti->msgServiceBg, sti->msgServiceBgCornersSmall);
			p.setFont(st::normalFont);
			p.setPen(st->msgServiceFg());
			p.drawTextLeft(statusX, statusY, width(), statusText, statusW - 2 * st::msgDateImgPadding.x());
			if (mediaUnread) {
				p.setPen(Qt::NoPen);
				p.setBrush(st->msgServiceFg());

				{
					PainterHighQualityEnabler hq(p);
					p.drawEllipse(style::rtlrect(statusX - st::msgDateImgPadding.x() + statusW - st::msgDateImgPadding.x() - st::mediaUnreadSize, statusY + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width()));
				}
			}
			ensureTranscribeButton();
		}
		const auto ephemeralPlate = _ephemeral.text.isEmpty()
			? QSize()
			: EphemeralPlateSize(
				_ephemeral.text,
				_ephemeral.onTop
					? (width() - st::msgReplyPadding.left())
					: (width() - usew - st::msgReplyPadding.left()));
		const auto platey = painty - _ephemeral.topAdded;
		const auto plateOffset = ephemeralPlate.isEmpty()
			? 0
			: (ephemeralPlate.height() + st::msgReplyPadding.top());
		if (!ephemeralPlate.isEmpty()) {
			auto platex = _ephemeral.onTop
				? (rightAligned ? (width() - ephemeralPlate.width()) : 0)
				: (rightAligned ? 0 : (usew + st::msgReplyPadding.left()));
			if (rtl()) {
				platex = width() - platex - ephemeralPlate.width();
			}
			PaintEphemeralPlate(
				p,
				context,
				_ephemeral.text,
				platex,
				platey,
				ephemeralPlate.width(),
				width());
		}
		if (via || reply || forwarded) {
			auto rectw = _ephemeral.onTop
				? std::min(
					width() - st::msgReplyPadding.left(),
					additionalWidth(reply, via, forwarded))
				: (width() - usew - st::msgReplyPadding.left());
			auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
			auto recth = 0;
			auto forwardedHeightReal = forwarded ? forwarded->text.countHeight(innerw) : 0;
			auto forwardedHeight = qMin(forwardedHeightReal, kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
			if (forwarded) {
				recth += st::msgReplyPadding.top() + forwardedHeight;
			} else if (via) {
				recth += st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			}
			if (reply) {
				const auto replyMargins = reply->margins();
				recth += reply->height()
					- ((forwarded || via) ? 0 : replyMargins.top())
					- replyMargins.bottom();
			} else {
				recth += st::msgReplyPadding.bottom();
			}
			int rectx = _ephemeral.onTop
				? (rightAligned ? (width() - rectw) : 0)
				: (rightAligned ? 0 : (usew + st::msgReplyPadding.left()));
			int recty = platey + plateOffset;
			if (rtl()) rectx = width() - rectx - rectw;

			Ui::FillRoundRect(p, rectx, recty, rectw, recth, sti->msgServiceBg, sti->msgServiceBgCornersSmall);
			p.setPen(st->msgServiceFg());
			const auto textx = rectx + st::msgReplyPadding.left();
			const auto textw = rectw - st::msgReplyPadding.left() - st::msgReplyPadding.right();
			if (forwarded) {
				p.setTextPalette(st->serviceTextPalette());
				auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
				forwarded->text.drawElided(p, textx, recty + st::msgReplyPadding.top(), textw, kMaxGifForwardedBarLines, style::al_left, 0, -1, 0, breakEverywhere);
				p.restoreTextPalette();

				const auto skip = std::min(
					forwarded->text.countHeight(textw),
					kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
				recty += skip;
			} else if (via) {
				p.setFont(st::msgServiceNameFont);
				p.drawTextLeft(textx, recty + st::msgReplyPadding.top(), 2 * textx + textw, via->text);
				int skip = st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
				recty += skip;
			}
			if (reply) {
				if (forwarded || via) {
					recty += st::msgReplyPadding.top();
					recth -= st::msgReplyPadding.top();
				} else {
					recty -= reply->margins().top();
				}
				reply->paint(p, _parent, context, rectx, recty, rectw, false);
			}
		}
	}
	if (!inWebPage && !skipDrawingSurrounding) {
		auto fullRight = paintx + usex + usew;
		auto fullBottom = painty + painth;
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		if (unwrapped
			&& !rightAligned
			&& !_parent->hidesBottomInfo()) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		if (isRound
			|| ((!bubble || isBubbleBottom()) && needInfoDisplay())) {
			_parent->drawInfo(
				p,
				context,
				fullRight,
				fullBottom - st::msgDateImgDelta,
				2 * paintx + paintw,
				(unwrapped
					? InfoDisplayType::Background
					: InfoDisplayType::Image));
		}
		if (const auto size = bubble ? std::nullopt : _parent->rightActionSize()
			; size || (_transcribe && !rightAligned)) {
			const auto rightActionWidth = size
				? size->width()
				: _transcribe->size().width();
			auto fastShareLeft = rightLayout
				? (paintx + usex - size->width() - st::historyFastShareLeft)
				: (fullRight + st::historyFastShareLeft);
			auto fastShareTop = fullBottom
				- st::historyFastShareBottom
				- (size ? size->height() : 0);
			if (fastShareLeft + rightActionWidth > maxRight) {
				const auto hidesBottomInfo = _parent->hidesBottomInfo();
				fastShareLeft = fullRight
					- rightActionWidth
					- (hidesBottomInfo ? 0 : st::msgDateImgDelta);
				if (!hidesBottomInfo) {
					fastShareTop -= st::msgDateImgDelta
						+ st::msgDateImgPadding.y()
						+ st::msgDateFont->height
						+ st::msgDateImgPadding.y();
				}
			}
			if (size) {
				_parent->drawRightAction(p, context, fastShareLeft, fastShareTop, 2 * paintx + paintw);
			}
			if (_transcribe) {
				paintTranscribe(p, fastShareLeft, fastShareTop, true, context);
			}
		} else if (rightAligned && _transcribe) {
			paintTranscribe(p, usex, fullBottom, false, context);
		}
	}
	if (_drawTtl) {
		_drawTtl(p, rthumb, context);
	}
}

void Gif::paintTranscribe(
		Painter &p,
		int x,
		int y,
		bool right,
		const PaintContext &context) const {
	if (!_transcribe) {
		return;
	}
	const auto s = _transcribe->size();
	_transcribe->paint(
		p,
		x - (right ? 0 : s.width()),
		y - s.height() - st::msgDateImgDelta,
		context);
}

void Gif::paintTimestampMark(
		Painter &p,
		QRect rthumb,
		std::optional<Ui::BubbleRounding> rounding) const {
	if (_videoTimestamp <= 0 && _videoPosition < crl::time(200)) {
		return;
	}
	PaintVideoTimestampMark(
		p,
		rthumb,
		rounding,
		((_videoPosition > 0)
			? _videoPosition
			: (_videoTimestamp * crl::time(1000))),
		_data->duration());
}

void Gif::paintRoundPlaybackProgress(
		Painter &p,
		const PaintContext &context,
		QRect rthumb,
		bool inTTLViewer) const {
	const auto playback = videoPlayback();
	_roundSeek->paint(
		p,
		context,
		rthumb,
		roundSeekShown(),
		_seeking,
		playback ? playback->value() : -1.,
		inTTLViewer);
}

void Gif::drawSpoilerTag(
		Painter &p,
		QRect rthumb,
		const PaintContext &context,
		Fn<QImage()> generateBackground) const {
	Media::drawSpoilerTag(
		p,
		_spoiler.get(),
		_spoilerTag,
		rthumb,
		context,
		std::move(generateBackground));
}

ClickHandlerPtr Gif::spoilerTagLink() const {
	return Media::spoilerTagLink(_spoiler.get(), _spoilerTag);
}

QImage Gif::spoilerTagBackground() const {
	return _spoiler ? _spoiler->background : QImage();
}

void Gif::validateVideoThumbnail() const {
	Expects(!_videoCover);

	const auto content = _dataMedia->videoThumbnailContent();
	if (_videoThumbnailFrame || content.isEmpty()) {
		return;
	}
	auto info = v::get<Ui::PreparedFileInformation::Video>(
		::Media::Clip::PrepareForSending(QString(), content).media);
	_videoThumbnailFrame = std::make_unique<Image>(info.thumbnail.isNull()
		? Image::BlankMedia()->original()
		: info.thumbnail);
}

void Gif::validateThumbCache(
		QSize outer,
		bool isEllipse,
		std::optional<Ui::BubbleRounding> rounding) const {
	const auto good = _videoCoverMedia
		? _videoCoverMedia->image(Data::PhotoSize::Large)
		: _dataMedia->goodThumbnail();
	const auto normal = good
		? good
		: _videoCoverMedia
		? nullptr
		: _dataMedia->thumbnail();
	if (!normal) {
		if (_videoCoverMedia) {
			_videoCover->load(Data::PhotoSize::Small, _realParent->fullId());
		} else {
			_data->loadThumbnail(_realParent->fullId());
			validateVideoThumbnail();
		}
	}
	const auto videothumb = (normal || _videoCoverMedia)
		? nullptr
		: _videoThumbnailFrame.get();
	const auto blurred = normal
		? (!good
			&& (normal->width() < kUseNonBlurredThreshold)
			&& (normal->height() < kUseNonBlurredThreshold))
		: !videothumb;
	const auto ratio = style::DevicePixelRatio();
	const auto scaled = ScaledInstantViewMediaSize(
		outer,
		HostedInstantViewMediaPixelScale(_parent));
	if (_thumbCache.size() == (scaled * ratio)
		&& _thumbCacheRounding == rounding
		&& _thumbCacheBlurred == blurred
		&& _thumbIsEllipse == isEllipse) {
		return;
	}
	auto cache = prepareThumbCache(scaled);
	_thumbCache = isEllipse
		? Images::Circle(std::move(cache))
		: Images::Round(std::move(cache), MediaRoundingMask(rounding));
	_thumbCacheRounding = rounding;
	_thumbCacheBlurred = blurred;
}

QImage Gif::prepareThumbCache(QSize outer) const {
	const auto good = _videoCoverMedia
		? _videoCoverMedia->image(Data::PhotoSize::Large)
		: _dataMedia->goodThumbnail();
	const auto normal = good
		? good
		: _videoCoverMedia
		? nullptr
		: _dataMedia->thumbnail();
	const auto videothumb = (normal || _videoCoverMedia)
		? nullptr
		: _videoThumbnailFrame.get();
	auto blurred = (!good
		&& normal
		&& (normal->width() < kUseNonBlurredThreshold)
		&& (normal->height() < kUseNonBlurredThreshold))
		? normal
		: nullptr;
	const auto blurFromLarge = good || (normal && !blurred);
	const auto large = blurFromLarge ? normal : videothumb;
	if (videothumb) {
	} else if (_videoCoverMedia) {
		if (const auto embedded = _videoCoverMedia->thumbnailInline()) {
			blurred = embedded;
		}
	} else if (const auto embedded = _dataMedia->thumbnailInline()) {
		blurred = embedded;
	}
	const auto resize = large
		? ::Media::Streaming::DecideVideoFrameResize(
			outer,
			good ? large->size() : _data->dimensions)
		: ::Media::Streaming::ExpandDecision();
	return PrepareWithBlurredBackground(
		outer,
		resize,
		large,
		blurFromLarge ? large : blurred);
}

void Gif::validateSpoilerImageCache(
		QSize outer,
		std::optional<Ui::BubbleRounding> rounding) const {
	Expects(_spoiler != nullptr);

	const auto ratio = style::DevicePixelRatio();
	const auto scaled = ScaledInstantViewMediaSize(
		outer,
		HostedInstantViewMediaPixelScale(_parent));
	if (_spoiler->background.size() == (scaled * ratio)
		&& _spoiler->backgroundRounding == rounding) {
		return;
	}
	const auto normal = _videoCoverMedia
		? _videoCoverMedia->image(Data::PhotoSize::Small)
		: _dataMedia->thumbnail();
	auto container = std::optional<Image>();
	const auto downscale = [&](Image *image) {
		if (!image || (image->width() <= 40 && image->height() <= 40)) {
			return image;
		}
		container.emplace(image->original().scaled(
			{ 40, 40 },
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation));
		return &*container;
	};
	const auto embedded = _videoCoverMedia
		? _videoCoverMedia->thumbnailInline()
		: _dataMedia->thumbnailInline();
	const auto blurred = embedded ? embedded : downscale(normal);
	_spoiler->background = Images::Round(
		PrepareWithBlurredBackground(
			scaled,
			::Media::Streaming::ExpandDecision(),
			nullptr,
			blurred),
		MediaRoundingMask(rounding));
	_spoiler->backgroundRounding = rounding;
}

void Gif::drawCornerStatus(
		Painter &p,
		const PaintContext &context,
		QPoint position) const {
	if (!needCornerStatusDisplay()) {
		return;
	}
	const auto own = activeOwnStreamed();
	const auto download = downloadInCorner()
		&& !dataLoaded()
		&& !_data->loadedInMediaCache();
	PaintVideoCornerStatus(p, context, {
		.text = ((own && !own->frozenStatusText.isEmpty())
			? own->frozenStatusText
			: _statusText),
		.downloadSize = _downloadSize,
		.position = position,
		.outerWidth = width(),
		.radial = ((_animation && _animation->radial.animating())
			? &_animation->radial
			: nullptr),
		.download = download,
		.loading = _data->loading(),
		.mute = (_streamed && _data->isVideoFile() && !download),
	});
}

TextState Gif::cornerStatusTextState(
		QPoint point,
		StateRequest request,
		QPoint position) const {
	auto result = TextState(_parent);
	if (!needCornerStatusDisplay() || !downloadInCorner() || dataLoaded()) {
		return result;
	}
	if (VideoCornerDownloadRect(position).contains(point)) {
		result.link = _data->loading() ? _cancell : _savel;
	}
	return result;
}

TextState Gif::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	auto bubble = _parent->hasBubble();

	const auto rightLayout = _parent->hasRightLayout();
	const auto inWebPage = (_parent->media() != this);
	const auto isRound = _data->isVideoMessage();
	const auto unwrapped = isUnwrapped();
	const auto item = _parent->data();
	auto usew = paintw, usex = 0;
	const auto via = unwrapped ? item->Get<HistoryMessageVia>() : nullptr;
	const auto reply = unwrapped ? _parent->Get<Reply>() : nullptr;
	const auto forwarded = unwrapped ? item->Get<HistoryMessageForwarded>() : nullptr;
	const auto rightAligned = unwrapped && rightLayout;
	if (via || reply || forwarded || !_ephemeral.text.isEmpty()) {
		usew = maxWidth() - additionalWidth(reply, via, forwarded);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (_ephemeral.onTop) {
		painty += _ephemeral.topAdded;
		painth -= _ephemeral.topAdded;
	}
	if (isRound) {
		accumulate_min(usew, painth);
	}
	if (rtl()) usex = width() - usex - usew;

	const auto ephemeralPlate = _ephemeral.text.isEmpty()
		? QSize()
		: EphemeralPlateSize(
			_ephemeral.text,
			_ephemeral.onTop
				? (paintw - st::msgReplyPadding.left())
				: (paintw - usew - st::msgReplyPadding.left()));
	const auto platey = painty - _ephemeral.topAdded;
	const auto plateOffset = ephemeralPlate.isEmpty()
		? 0
		: (ephemeralPlate.height() + st::msgReplyPadding.top());
	if (!ephemeralPlate.isEmpty()) {
		auto platex = _ephemeral.onTop
			? (rightAligned ? (width() - ephemeralPlate.width()) : 0)
			: (rightAligned ? 0 : (usew + st::msgReplyPadding.left()));
		if (rtl()) {
			platex = width() - platex - ephemeralPlate.width();
		}
		if (EphemeralPlateState(
				_parent,
				_ephemeral.text,
				point,
				platex,
				platey,
				ephemeralPlate.width(),
				ephemeralPlate.height(),
				request,
				result)) {
			return result;
		}
	}
	if (via || reply || forwarded) {
		auto rectw = _ephemeral.onTop
			? std::min(
				paintw - st::msgReplyPadding.left(),
				additionalWidth(reply, via, forwarded))
			: (paintw - usew - st::msgReplyPadding.left());
		auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
		auto recth = 0;
		auto forwardedHeightReal = forwarded ? forwarded->text.countHeight(innerw) : 0;
		auto forwardedHeight = qMin(forwardedHeightReal, kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
		if (forwarded) {
			recth += st::msgReplyPadding.top() + forwardedHeight;
		} else if (via) {
			recth += st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			const auto replyMargins = reply->margins();
			recth += reply->height()
				- ((forwarded || via) ? 0 : replyMargins.top())
				- replyMargins.bottom();
		} else {
			recth += st::msgReplyPadding.bottom();
		}
		auto rectx = _ephemeral.onTop
			? (rightAligned ? (width() - rectw) : 0)
			: (rightAligned ? 0 : (usew + st::msgReplyPadding.left()));
		auto recty = platey + plateOffset;
		if (rtl()) rectx = width() - rectx - rectw;

		if (forwarded) {
			if (QRect(rectx, recty, rectw, st::msgReplyPadding.top() + forwardedHeight).contains(point)) {
				auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
				auto textRequest = request.forText();
				if (breakEverywhere) {
					textRequest.flags |= Ui::Text::StateRequest::Flag::BreakEverywhere;
				}
				result = TextState(_parent, forwarded->text.getState(
					point - QPoint(rectx + st::msgReplyPadding.left(), recty + st::msgReplyPadding.top()),
					innerw,
					textRequest));
				result.symbol = 0;
				result.afterSymbol = false;
				if (breakEverywhere) {
					result.cursor = CursorState::Forwarded;
				} else {
					result.cursor = CursorState::None;
				}
				return result;
			}
			recty += forwardedHeight;
			recth -= forwardedHeight;
		} else if (via) {
			auto viah = st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? 0 : st::msgReplyPadding.bottom());
			if (QRect(rectx, recty, rectw, viah).contains(point)) {
				result.link = via->link;
				return result;
			}
			auto skip = st::msgServiceNameFont->height + (reply ? 2 * st::msgReplyPadding.top() : 0);
			recty += skip;
			recth -= skip;
		}
		if (reply) {
			if (forwarded || via) {
				recty += st::msgReplyPadding.top();
				recth -= st::msgReplyPadding.top() + reply->margins().top();
			} else {
				recty -= reply->margins().top();
			}
			const auto replyRect = QRect(rectx, recty, rectw, recth);
			if (replyRect.contains(point)) {
				result.link = reply->link();
				reply->saveRipplePoint(point - replyRect.topLeft());
				reply->createRippleAnimation(_parent, replyRect.size());
				return result;
			}
		}
	}
	if (!unwrapped) {
		if (const auto state = cornerStatusTextState(point, request, QPoint()); state.link) {
			return state;
		}
	}
	if (QRect(usex + paintx, painty, usew, painth).contains(point)) {
		ensureDataMediaCreated();
		if (_spoiler && !_spoiler->revealed) {
			const auto media = _parent->data()->media();
			result.link = _sensitiveSpoiler
				? spoilerTagLink()
				: (isRound && media && media->ttlSeconds())
				? _openl
				: _spoiler->link;
		} else if (_seekl && isRoundSeekable()) {
			_seekStatePoint = point;
			result.link = _seekl;
		} else {
			result.link = currentVideoLink(true);
		}
	}
	const auto checkBottomInfo = !inWebPage
		&& (unwrapped || !bubble || isBubbleBottom());
	if (checkBottomInfo) {
		auto fullRight = usex + paintx + usew;
		auto fullBottom = painty + painth;
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		if (unwrapped
			&& !rightAligned
			&& !_parent->hidesBottomInfo()) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		const auto bottomInfoResult = _parent->bottomInfoTextState(
			fullRight,
			fullBottom,
			point,
			(unwrapped
				? InfoDisplayType::Background
				: InfoDisplayType::Image));
		if (bottomInfoResult.link
			|| bottomInfoResult.cursor != CursorState::None
			|| bottomInfoResult.customTooltip) {
			return bottomInfoResult;
		}
		if (const auto size = bubble ? std::nullopt : _parent->rightActionSize()) {
			const auto rightActionWidth = size->width();
			auto fastShareLeft = _parent->hasRightLayout()
				? (paintx + usex - size->width() - st::historyFastShareLeft)
				: (fullRight + st::historyFastShareLeft);
			auto fastShareTop = fullBottom
				- st::historyFastShareBottom
				- size->height();
			if (fastShareLeft + rightActionWidth > maxRight) {
				const auto hidesBottomInfo = _parent->hidesBottomInfo();
				fastShareLeft = fullRight
					- rightActionWidth
					- (hidesBottomInfo ? 0 : st::msgDateImgDelta);
				if (!hidesBottomInfo) {
					fastShareTop -= st::msgDateImgDelta
						+ st::msgDateImgPadding.y()
						+ st::msgDateFont->height
						+ st::msgDateImgPadding.y();
				}
			}
			if (QRect(QPoint(fastShareLeft, fastShareTop), *size).contains(point)) {
				result.link = _parent->rightActionLink(point
					- QPoint(fastShareLeft, fastShareTop));
			}
		}
		if (_transcribe && _transcribe->contains(point)) {
			result.link = _transcribe->link();
		}
	}
	return result;
}

void Gif::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (_seekl && handler == _seekl) {
		if (pressed && !_seeking) {
			_seekPressPoint = QPoint(-1, -1);
			if (const auto playback = videoPlayback()) {
				_roundSeek->setProgress(playback->value());
			}
			const auto rthumb = roundThumbRect();
			if (roundSeekShown()
				&& _roundSeek->grabPoint(rthumb, _seekStatePoint)) {
				startRoundSeeking();
				updateRoundSeeking(rthumb, _seekStatePoint);
			}
		} else if (!pressed) {
			if (_seeking) {
				if (isRoundSeekable()) {
					::Media::Player::instance()->finishSeeking(
						AudioMsgId::Type::Voice,
						_roundSeek->progress());
				}
				_seeking = false;
				_roundSeek->setGrabbed(false);
				repaint();
			} else if (_seekPressPoint != QPoint()) {
				_seekPressPoint = QPoint();
				::Media::Player::instance()->playPauseCancelClicked(
					AudioMsgId::Type::Voice);
			}
		}
	}
	File::clickHandlerPressedChanged(handler, pressed);
	if (!handler) {
		return;
	} else if (_transcribe && (handler == _transcribe->link())) {
		if (pressed) {
			_transcribe->addRipple([=] { repaint(); });
		} else {
			_transcribe->stopRipple();
		}
	}
}

QRect Gif::roundThumbRect() const {
	const auto item = _parent->data();
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	const auto unwrapped = isUnwrapped();
	auto usew = paintw, usex = 0;
	const auto via = unwrapped ? item->Get<HistoryMessageVia>() : nullptr;
	const auto reply = unwrapped ? _parent->Get<Reply>() : nullptr;
	const auto forwarded = unwrapped
		? item->Get<HistoryMessageForwarded>()
		: nullptr;
	if (via || reply || forwarded || !_ephemeral.text.isEmpty()) {
		usew = maxWidth() - additionalWidth(reply, via, forwarded);
		if (unwrapped && _parent->hasRightLayout()) {
			usex = width() - usew;
		}
	}
	if (_ephemeral.onTop) {
		painty += _ephemeral.topAdded;
		painth -= _ephemeral.topAdded;
	}
	accumulate_min(usew, painth);
	if (rtl()) usex = width() - usex - usew;
	return style::rtlrect(usex + paintx, painty, usew, painth, width());
}

void Gif::captureRoundSeekFrame() const {
	// Restart would flash cover thumbnail in place of shown frame.
	const auto streamed = activeRoundStreamed();
	if (!streamed) {
		return;
	}
	const auto size = roundThumbRect().size();
	auto request = ::Media::Streaming::FrameRequest{
		.outer = (ScaledInstantViewMediaSize(
			size,
			HostedInstantViewMediaPixelScale(_parent))
			* style::DevicePixelRatio()),
		.blurredBackground = true,
	};
	validateRoundingMask(request.outer);
	request.mask = _roundingMask;
	_seekLastFrame = streamed->frame(request);
}

void Gif::startRoundSeeking() {
	captureRoundSeekFrame();
	_seeking = true;
	_seekPressPoint = QPoint();
	_seekPreviewTime = 0;
	::Media::Player::instance()->startSeeking(AudioMsgId::Type::Voice);
	_roundSeek->setGrabbed(true);
}

void Gif::updateRoundSeeking(QRect rthumb, QPoint point) {
	const auto center = rthumb.center();
	const auto dx = float64(point.x() - center.x());
	const auto dy = float64(point.y() - center.y());
	const auto angle = atan2(-dy, dx);
	const auto now = std::clamp(
		fmod((M_PI / 2. - angle) / (2. * M_PI) + 1., 1.),
		0.,
		1.);
	const auto changed = (now != _roundSeek->progress());
	_roundSeek->setDraggedProgress(now);
	// Piled up restarts would cancel each other before showing a frame.
	if (changed && activeRoundStreamed()) {
		const auto ms = crl::now();
		if (ms - _seekPreviewTime >= kSeekPreviewInterval) {
			_seekPreviewTime = ms;
			captureRoundSeekFrame();
			::Media::Player::instance()->updateSeeking(
				AudioMsgId::Type::Voice,
				now);
		}
	}
	repaint();
}

void Gif::updatePressed(QPoint point) {
	if (!_seeking && _seekPressPoint == QPoint()) {
		return;
	}
	const auto rthumb = roundThumbRect();
	if (!_seeking) {
		if (_seekPressPoint == QPoint(-1, -1)) {
			_seekPressPoint = point;
			return;
		} else if ((point - _seekPressPoint).manhattanLength()
				<= QApplication::startDragDistance()) {
			return;
		} else if (!_roundSeek->grabPoint(rthumb, _seekPressPoint)) {
			// Angle from near the center jumps on the smallest move.
			_seekPressPoint = QPoint();
			return;
		}
		startRoundSeeking();
	}
	updateRoundSeeking(rthumb, point);
}

bool Gif::fullFeaturedGrouped(RectParts sides) const {
	return (sides & RectPart::Left) && (sides & RectPart::Right);
}

QSize Gif::sizeForGroupingOptimal(int maxWidth, bool last) const {
	return sizeForAspectRatio();
}

QSize Gif::sizeForGrouping(int width) const {
	return sizeForAspectRatio();
}

void Gif::drawGrouped(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry,
		RectParts sides,
		Ui::BubbleRounding rounding,
		float64 highlightOpacity,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	ensureDataMediaCreated();
	const auto item = _parent->data();
	const auto loaded = dataLoaded();
	const auto displayLoading = item->isSending()
		|| item->hasFailed()
		|| _data->displayLoading();
	const auto st = context.st;
	const auto sti = context.imageStyle();
	_smallGroupPart = !fullFeaturedGrouped(sides);
	const auto cornerDownload = !_smallGroupPart && downloadInCorner();

	const auto revealed = revealedProgress();
	const auto fullHiddenBySpoiler = (revealed == 0.);
	if (revealed < 1.) {
		validateSpoilerImageCache(geometry.size(), rounding);
	}

	const auto autoplay = autoplayEligible(!_smallGroupPart);
	const auto canStartPlay = autoplay
		&& !_streamed
		&& !fullHiddenBySpoiler;
	const auto shouldBePlaying = !autoplayUnderCursor()
		|| underCursor(!_smallGroupPart);
	if (!shouldBePlaying && _videoTimestamp != 0) {
		const_cast<Gif*>(this)->stopAnimation();
	} else if (canStartPlay) {
		const_cast<Gif*>(this)->playAnimation(true);
	} else {
		checkStreamedIsStarted();
	}

	const auto streamingMode = _streamed || autoplay;
	const auto activeOwnPlaying = activeOwnStreamed();

	const auto streamed = activeOwnPlaying
		? &activeOwnPlaying->instance
		: nullptr;
	const auto streamedForWaiting = _streamed
		? &_streamed->instance
		: nullptr;

	if (displayLoading
		&& (!streamedForWaiting
			|| item->isSending()
			|| _data->uploading()
			|| (cornerDownload && _data->loading()))) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(dataProgress());
		}
	}
	updateStatusText();
	const auto radial = isRadialAnimation()
		|| (streamedForWaiting && streamedForWaiting->waitingShown());

	if (streamed && !fullHiddenBySpoiler) {
		const auto original = sizeForAspectRatio();
		const auto originalWidth = style::ConvertScale(original.width());
		const auto originalHeight = style::ConvertScale(original.height());
		const auto scaled = ScaledInstantViewMediaSize(
			geometry.size(),
			HostedInstantViewMediaPixelScale(_parent));
		const auto pixSize = Ui::GetImageScaleSizeForGeometry(
			{ originalWidth, originalHeight },
			{ scaled.width(), scaled.height() });
		const auto ratio = style::DevicePixelRatio();
		auto request = ::Media::Streaming::FrameRequest{
			.resize = pixSize * ratio,
			.outer = scaled * ratio,
			.rounding = MediaRoundingMask(rounding),
		};
		if (activeOwnPlaying->instance.playerLocked()) {
			if (activeOwnPlaying->frozenFrame.isNull()) {
				activeOwnPlaying->frozenRequest = request;
				activeOwnPlaying->frozenFrame = streamed->frame(request);
				activeOwnPlaying->frozenStatusText = _statusText;
			} else if (activeOwnPlaying->frozenRequest != request) {
				activeOwnPlaying->frozenRequest = request;
				activeOwnPlaying->frozenFrame = streamed->frame(request);
			}
			p.drawImage(geometry, activeOwnPlaying->frozenFrame);
		} else {
			if (activeOwnPlaying) {
				activeOwnPlaying->frozenFrame = QImage();
				activeOwnPlaying->frozenStatusText = QString();
			}
			p.drawImage(geometry, streamed->frame(request));
			const auto paused = context.paused
				|| (autoplayUnderCursor() && !underCursor(!_smallGroupPart));
			if (!paused) {
				streamed->markFrameShown();
			}
		}
	} else if (!fullHiddenBySpoiler) {
		validateGroupedCache(geometry, rounding, cacheKey, cache);
		p.drawPixmap(geometry, *cache);
	}

	if (revealed < 1.) {
		p.setOpacity(1. - revealed);
		p.drawImage(geometry, _spoiler->background);
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

	const auto paintInCenter = !_sensitiveSpoiler
		&& (radial
			|| (!streamingMode
				&& ((!loaded && !_data->loading()) || !autoplay)));
	if (paintInCenter) {
		const auto radialRevealed = 1.;
		const auto opacity = (item->isSending() || _data->uploading())
			? 1.
			: streamedForWaiting
			? streamedForWaiting->waitingOpacity()
			: (radial && loaded)
			? _animation->radial.opacity()
			: 1.;
		const auto radialOpacity = opacity * radialRevealed;
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
			auto over = ClickHandler::showAsActive(
				(_data->loading() || _data->uploading()) ? _cancell : _savel);
			p.setBrush(over ? st->msgDateImgBgOver() : st->msgDateImgBg());
		}
		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		const auto icon = [&]() -> const style::icon * {
			if (_data->waitingForAlbum()) {
				return &sti->historyFileThumbWaiting;
			}
			switch (currentAction(!_smallGroupPart)) {
			case Action::None:
			case Action::Streaming: return nullptr;
			case Action::Open: return &sti->historyFileThumbPlay;
			case Action::Cancel: return &sti->historyFileThumbCancel;
			case Action::Download: return &sti->historyFileThumbDownload;
			}
			Unexpected("Action in Gif::drawGrouped.");
		}();
		const auto previous = _data->waitingForAlbum()
			? &sti->historyFileThumbCancel
			: nullptr;
		if (icon) {
			if (previous && radialOpacity > 0. && radialOpacity < 1.) {
				PaintInterpolatedIcon(p, *icon, *previous, radialOpacity, inner);
			} else {
				icon->paintInCenter(p, inner);
			}
		}
		p.setOpacity(radialRevealed);
		if (radial) {
			const auto line = st::historyGroupRadialLine;
			const auto rinner = inner.marginsRemoved({ line, line, line, line });
			if (streamedForWaiting && !_data->uploading()) {
				Ui::InfiniteRadialAnimation::Draw(
					p,
					streamedForWaiting->waitingState(),
					rinner.topLeft(),
					rinner.size(),
					width(),
					sti->historyFileThumbRadialFg,
					st::msgFileRadialLine);
			} else if (!cornerDownload) {
				_animation->radial.draw(
					p,
					rinner,
					st::msgFileRadialLine,
					sti->historyFileThumbRadialFg);
			}
		}
		p.setOpacity(1.);
	}
	if (!_smallGroupPart) {
		drawCornerStatus(p, context, geometry.topLeft());
	}
}

TextState Gif::getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const {
	if (!geometry.contains(point)) {
		return {};
	}
	const auto fullFeatured = fullFeaturedGrouped(sides);
	if (fullFeatured) {
		const auto state = cornerStatusTextState(
			point,
			request,
			geometry.topLeft());
		if (state.link) {
			return state;
		}
	}
	ensureDataMediaCreated();

	auto link = (_spoiler && !_spoiler->revealed)
		? (_sensitiveSpoiler ? spoilerTagLink() : _spoiler->link)
		: currentVideoLink(fullFeatured);
	return TextState(_parent, std::move(link));
}

Gif::Action Gif::currentAction(bool fullFeatured) const {
	ensureDataMediaCreated();
	if (_data->waitingForAlbum()) {
		return Action::None;
	} else if (_data->uploading()) {
		return Action::Cancel;
	} else if (_realParent->isSending()) {
		return Action::None;
	} else if (_streamed
		|| activeRoundStreamed()
		|| autoplayEligible(fullFeatured)) {
		return Action::Streaming;
	}
	const auto cornerDownload = fullFeatured && downloadInCorner();
	if ((dataLoaded() || _dataMedia->canBePlayed())
		&& (!_data->displayLoading() || cornerDownload)) {
		return Action::Open;
	} else if (_data->loading()) {
		return Action::Cancel;
	}
	return Action::Download;
}

ClickHandlerPtr Gif::currentVideoLink(bool fullFeatured) const {
	switch (currentAction(fullFeatured)) {
	case Action::None: return nullptr;
	case Action::Open:
	case Action::Streaming: return _openl;
	case Action::Cancel: return _cancell;
	case Action::Download: return _savel;
	}
	Unexpected("Action in Gif::currentVideoLink.");
}

void Gif::ensureDataMediaCreated() const {
	if (_dataMedia && (!_videoCover || _videoCoverMedia)) {
		return;
	}
	_dataMedia = _data->createMediaView();
	_videoCoverMedia = _videoCover
		? _videoCover->createMediaView()
		: nullptr;
	dataMediaCreated();
}

void Gif::dataMediaCreated() const {
	Expects(_dataMedia != nullptr);

	if (_videoCoverMedia) {
		_videoCoverMedia->wanted(
			Data::PhotoSize::Large,
			_realParent->fullId());
	} else {
		_dataMedia->goodThumbnailWanted();
		_dataMedia->thumbnailWanted(_realParent->fullId());
		if (!autoplayEnabled()) {
			_dataMedia->videoThumbnailWanted(_realParent->fullId());
		}
	}
	history()->owner().registerHeavyViewPart(_parent);
	togglePollingStory(true);
}

void Gif::togglePollingStory(bool enabled) const {
	if (!_storyId || _pollingStory == enabled) {
		return;
	}
	const auto polling = Data::Stories::Polling::Chat;
	if (!enabled) {
		_data->owner().stories().unregisterPolling(_storyId, polling);
	} else if (
			!_data->owner().stories().registerPolling(_storyId, polling)) {
		return;
	}
	_pollingStory = enabled;
}

bool Gif::uploading() const {
	return _data->uploading();
}

void Gif::hideSpoilers() {
	if (_spoiler) {
		_spoiler->revealed = false;
	}
}

bool Gif::needsBubble() const {
	if (_storyId) {
		return true;
	} else if (_data->isVideoMessage()) {
		return false;
	}
	const auto item = _parent->data();
	return item->repliesAreComments()
		|| item->externalReply()
		|| item->viaBot()
		|| !item->emptyText()
		|| _parent->displayReply()
		|| _parent->displayForwardedFrom()
		|| _parent->displayFromName()
		|| _parent->displayedTopicButton();
	return false;
}

bool Gif::unwrapped() const {
	return isUnwrapped();
}

QRect Gif::contentRectForReactions() const {
	if (!isUnwrapped()) {
		return QRect(0, 0, width(), height());
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	auto usex = 0, usew = paintw;
	const auto rightAligned = _parent->hasRightLayout();
	const auto item = _parent->data();
	const auto via = item->Get<HistoryMessageVia>();
	const auto reply = _parent->Get<Reply>();
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	if (via || reply || forwarded || !_ephemeral.text.isEmpty()) {
		usew = maxWidth() - additionalWidth(reply, via, forwarded);
	}
	accumulate_max(usew, _parent->reactionsOptimalWidth());
	if (rightAligned) {
		usex = width() - usew;
	}
	if (rtl()) usex = width() - usex - usew;
	return style::rtlrect(usex + paintx, painty, usew, painth, width());
}

std::optional<int> Gif::reactionButtonCenterOverride() const {
	if (!isUnwrapped() || _parent->hidesBottomInfo()) {
		return std::nullopt;
	}
	const auto right = resolveCustomInfoRightBottom().x()
		- _parent->infoWidth()
		- 3 * st::msgDateImgPadding.x();
	return right - st::reactionCornerSize.width() / 2;
}

QPoint Gif::resolveCustomInfoRightBottom() const {
	const auto inner = contentRectForReactions();
	auto fullBottom = inner.y() + inner.height();
	auto fullRight = inner.x() + inner.width();
	if (_parent->hidesBottomInfo()) {
		return QPoint(fullRight, fullBottom);
	}
	const auto unwrapped = isUnwrapped();
	if (unwrapped) {
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		const auto infoWidth = _parent->infoWidth();
		const auto rightAligned = _parent->hasRightLayout();
		if (!rightAligned) {
			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
	}
	const auto skipx = unwrapped
		? st::msgDateImgPadding.x()
		: (st::msgDateImgDelta + st::msgDateImgPadding.x());
	const auto skipy = unwrapped
		? st::msgDateImgPadding.y()
		: (st::msgDateImgDelta + st::msgDateImgPadding.y());
	return QPoint(fullRight - skipx, fullBottom - skipy);
}

int Gif::additionalWidth() const {
	const auto item = _parent->data();
	return additionalWidth(
		_parent->Get<Reply>(),
		item->Get<HistoryMessageVia>(),
		item->Get<HistoryMessageForwarded>());
}

bool Gif::isUnwrapped() const {
	return _data->isVideoMessage() && (_parent->media() == this);
}

void Gif::validateGroupedCache(
		const QRect &geometry,
		Ui::BubbleRounding rounding,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	using Option = Images::Option;

	ensureDataMediaCreated();

	const auto good = _videoCoverMedia
		? _videoCoverMedia->image(Data::PhotoSize::Large)
		: _dataMedia->goodThumbnail();
	const auto thumb = _videoCoverMedia
		? nullptr
		: _dataMedia->thumbnail();
	const auto image = good
		? good
		: thumb
		? thumb
		: _videoCoverMedia
		? _videoCoverMedia->thumbnailInline()
		: _dataMedia->thumbnailInline();
	const auto blur = !good
		&& (!thumb
			|| (thumb->width() < kUseNonBlurredThreshold
				&& thumb->height() < kUseNonBlurredThreshold));

	const auto loadLevel = good ? 3 : thumb ? 2 : image ? 1 : 0;
	const auto scaled = ScaledInstantViewMediaSize(
		geometry.size(),
		HostedInstantViewMediaPixelScale(_parent));
	const auto width = scaled.width();
	const auto height = scaled.height();
	const auto options = (blur ? Option::Blur : Option(0));
	const auto key = (uint64(width) << 48)
		| (uint64(height) << 32)
		| (uint64(options) << 16)
		| (uint64(rounding.key()) << 8)
		| (uint64(loadLevel));
	if (*cacheKey == key) {
		return;
	}

	const auto original = sizeForAspectRatio();
	const auto originalWidth = style::ConvertScale(original.width());
	const auto originalHeight = style::ConvertScale(original.height());
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ originalWidth, originalHeight },
		{ width, height });
	const auto ratio = style::DevicePixelRatio();

	*cacheKey = key;
	auto prepared = Images::Prepare(
		(image ? image : Image::BlankMedia().get())->original(),
		pixSize * ratio,
		{ .options = options, .outer = { width, height } });
	auto rounded = Images::Round(
		std::move(prepared),
		MediaRoundingMask(rounding));
	*cache = Ui::PixmapFromImage(std::move(rounded));
}

void Gif::setStatusSize(int64 newSize) const {
	if (newSize < 0) {
		_statusSize = newSize;
		_statusText = Ui::FormatDurationText(-newSize - 1);
	} else if (_data->isVideoMessage()) {
		_statusSize = newSize;
		_statusText = Ui::FormatDurationText(_data->duration() / 1000);
	} else {
		File::setStatusSize(
			newSize,
			_data->size,
			_data->isVideoFile() ? (_data->duration() / 1000) : -2,
			0);
	}
}

void Gif::updateStatusText() const {
	ensureDataMediaCreated();
	auto statusSize = int64();
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (!downloadInCorner() && _data->loading()) {
		statusSize = _data->loadOffset();
	} else if (dataLoaded() || _dataMedia->canBePlayed()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}
	const auto round = activeRoundStreamed();
	const auto own = activeOwnStreamed();
	if (round || (own && _data->isVideoFile())) {
		const auto frozen = own && !own->frozenFrame.isNull();
		const auto streamed = round ? round : &own->instance;
		const auto state = streamed->player().prepareLegacyState();
		if (state.length) {
			auto position = int64(0);
			if (::Media::Player::IsStoppedAtEnd(state.state)) {
				position = state.length;
			} else if (!::Media::Player::IsStoppedOrStopping(state.state)) {
				position = state.position;
			}
			if (!frozen) {
				statusSize = -1 - int((state.length - position) / state.frequency + 1);
			}
			_videoPosition = std::max(
				crl::time(position * crl::time(1000) / state.frequency),
				crl::time(1));
		} else {
			if (!frozen) {
				statusSize = -1 - (_data->duration() / 1000);
			}
			_videoPosition = 0;
		}
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
	if (_data->uploading() && _data->uploadingData->preparing) {
		const auto percent = int(base::SafeRound(
			_data->uploadingData->prepareProgress * 100));
		_statusText = tr::lng_send_video_preparing(
			tr::now,
			lt_progress,
			QString::number(percent));
		_statusSize = Ui::FileStatusSizeReady;
	}
}

QString Gif::additionalInfoString() const {
	if (_data->isVideoMessage()) {
		updateStatusText();
		return _statusText;
	}
	return QString();
}

bool Gif::isReadyForOpen() const {
	return true;
}

bool Gif::hasHeavyPart() const {
	return (_spoiler && _spoiler->animation) || _streamed || _dataMedia;
}

void Gif::unloadHeavyPart() {
	stopAnimation();
	_dataMedia = nullptr;
	if (_spoiler) {
		_spoiler->background = _spoiler->cornerCache = QImage();
		_spoiler->animation = nullptr;
	}
	_thumbCache = QImage();
	_seekLastFrame = QImage();
	if (_roundSeek) {
		_roundSeek->unloadHeavyPart();
	}
	_videoThumbnailFrame = nullptr;
	togglePollingStory(false);
}

bool Gif::enforceBubbleWidth() const {
	return true;
}

int Gif::bubbleWidthLimit() const {
	if (_ephemeral.text.isEmpty()
		|| !_data->isVideoMessage()
		|| !isUnwrapped()) {
		return 0;
	}
	const auto item = _parent->data();
	const auto via = item->Get<HistoryMessageVia>();
	const auto reply = _parent->Get<Reply>();
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	const auto content = maxWidth() - additionalWidth(reply, via, forwarded);
	return content
		+ st::msgReplyPadding.left()
		+ EphemeralPlateMaxWidth(_ephemeral.text);
}

int Gif::additionalWidth(
		const Reply *reply,
		const HistoryMessageVia *via,
		const HistoryMessageForwarded *forwarded) const {
	int result = 0;
	if (forwarded) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + forwarded->text.maxWidth() + st::msgReplyPadding.right());
	} else if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->maxWidth());
	}
	if (!_ephemeral.text.isEmpty()) {
		accumulate_max(
			result,
			st::msgReplyPadding.left()
				+ EphemeralPlateMaxWidth(_ephemeral.text));
	}
	return result;
}

int Gif::surroundingHeight(
		const Reply *reply,
		const HistoryMessageVia *via,
		const HistoryMessageForwarded *forwarded,
		int rectw) const {
	const auto innerw = rectw
		- (st::msgReplyPadding.left() + st::msgReplyPadding.right());
	auto recth = 0;
	const auto forwardedHeightReal = forwarded
		? forwarded->text.countHeight(innerw)
		: 0;
	const auto forwardedHeight = qMin(
		forwardedHeightReal,
		kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
	if (forwarded) {
		recth += st::msgReplyPadding.top() + forwardedHeight;
	} else if (via) {
		recth += st::msgReplyPadding.top()
			+ st::msgServiceNameFont->height
			+ (reply ? st::msgReplyPadding.top() : 0);
	}
	if (reply) {
		const auto replyMargins = reply->margins();
		recth += reply->height()
			- ((forwarded || via) ? 0 : replyMargins.top())
			- replyMargins.bottom();
	} else {
		recth += st::msgReplyPadding.bottom();
	}
	return recth;
}

::Media::Streaming::Instance *Gif::activeRoundStreamed() const {
	return ::Media::Player::instance()->roundVideoStreamed(_parent->data());
}

bool Gif::roundSeekShown() const {
	if (!_seekl) {
		return false;
	} else if (_seeking) {
		return true;
	}
	const auto streamed = activeRoundStreamed();
	return streamed && streamed->paused();
}

bool Gif::isRoundSeekable() const {
	// Player goes not-ready while a seek is applied.
	if (!activeRoundStreamed() && !_seeking) {
		return false;
	}
	const auto state = ::Media::Player::instance()->getState(
		AudioMsgId::Type::Voice);
	return (state.id == AudioMsgId(
			_data,
			_realParent->fullId(),
			state.id.externalPlayId()))
		&& !::Media::Player::IsStoppedOrStopping(state.state);
}

Gif::Streamed *Gif::activeOwnStreamed() const {
	return (_streamed
		&& _streamed->instance.player().ready()
		&& !_streamed->instance.player().videoSize().isEmpty())
		? _streamed.get()
		: nullptr;
}

::Media::Streaming::Instance *Gif::activeCurrentStreamed() const {
	if (const auto streamed = activeRoundStreamed()) {
		return streamed;
	} else if (const auto owned = activeOwnStreamed()) {
		return &owned->instance;
	}
	return nullptr;
}

::Media::View::PlaybackProgress *Gif::videoPlayback() const {
	return ::Media::Player::instance()->roundVideoPlayback(_parent->data());
}

void Gif::playAnimation(bool autoplay) {
	ensureDataMediaCreated();
	if (_data->isVideoMessage() && !autoplay) {
		return;
	} else if (_streamed && autoplay) {
		return;
	} else if ((_streamed && autoplayEnabled())
		|| (!autoplay && _data->isVideoFile())) {
		_parent->delegate()->elementOpenDocument(
			_data,
			_parent->data()->fullId(),
			true);
		return;
	}
	if (_streamed) {
		stopAnimation();
	} else if (_dataMedia->canBePlayed()) {
		if (!autoplayEnabled()) {
			history()->owner().checkPlayingAnimations();
		}
		createStreamedPlayer();
	}
}

void Gif::createStreamedPlayer() {
	const auto quality = _data->initialPlaybackVideoQuality(
		Core::App().settings().videoQuality());
	const auto chosen = ChooseInlineQuality(
		_data,
		_realParent,
		maxInlineArea(),
		quality);
	if (!chosen || (_streamed && _streamed->chosen == chosen)) {
		return;
	}
	auto shared = _data->owner().streaming().sharedDocument(
		chosen,
		_data,
		_realParent,
		_realParent->fullId());
	if (!shared) {
		return;
	}
	setStreamed(std::make_unique<Streamed>(
		chosen,
		std::move(shared),
		[=] { repaintStreamedContent(); }));

	_streamed->instance.player().updates(
	) | rpl::on_next_error([=](::Media::Streaming::Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](::Media::Streaming::Error &&error) {
		handleStreamingError(std::move(error));
	}, _streamed->instance.lifetime());

	_streamed->instance.switchQualityRequests(
	) | rpl::on_next([=](int requested) {
		if (quality.manual) {
			return;
		}
		auto now = Core::App().settings().videoQuality();
		if (now.manual || now.height == requested) {
			return;
		}
		Core::App().settings().setVideoQuality({
			.manual = 0,
			.height = uint32(requested),
		});
		Core::App().saveSettingsDelayed();
		createStreamedPlayer();
	}, _streamed->instance.lifetime());

	if (_streamed->instance.ready()) {
		streamingReady(base::duplicate(_streamed->instance.info()));
	}
	checkStreamedIsStarted();
}

void Gif::startStreamedPlayer() const {
	Expects(_streamed != nullptr);

	auto options = ::Media::Streaming::PlaybackOptions();
	options.audioId = AudioMsgId(_data, _realParent->fullId());
	options.waitForMarkAsShown = true;
	//if (!_streamed->withSound) {
	options.mode = ::Media::Streaming::Mode::Video;
	options.loop = true;
	options.position = _videoTimestamp
		? (_videoTimestamp * crl::time(1000))
		: _parent->history()->session().local().mediaLastPlaybackPosition(
			_data->id);
	//}
	_streamed->instance.play(options);
}

void Gif::checkStreamedIsStarted() const {
	if (!_streamed || _streamed->instance.playerLocked()) {
		return;
	}
	if (_streamed->instance.active()) {
		if (_streamed->instance.paused()) {
			_streamed->instance.resume();
		}
	} else if (!_streamed->instance.failed()) {
		startStreamedPlayer();
	}
}

void Gif::setStreamed(std::unique_ptr<Streamed> value) {
	const auto removed = (_streamed && !value);
	const auto set = (!_streamed && value);
	_streamed = std::move(value);
	if (set) {
		history()->owner().registerHeavyViewPart(_parent);
		togglePollingStory(true);
	} else if (removed) {
		_videoPosition = 0;
		_parent->checkHeavyPart();
	}
}

void Gif::handleStreamingUpdate(::Media::Streaming::Update &&update) {
	using namespace ::Media::Streaming;

	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [](PreloadedVideo) {
	}, [&](UpdateVideo) {
		repaintStreamedContent();
	}, [](PreloadedAudio) {
	}, [](UpdateAudio) {
	}, [](WaitingForData) {
	}, [](SpeedEstimate) {
	}, [](MutedByOther) {
	}, [](Finished) {
	});
}

void Gif::handleStreamingError(::Media::Streaming::Error &&error) {
}

void Gif::repaintStreamedContent() {
	const auto own = activeOwnStreamed();
	if (own && !own->frozenFrame.isNull()) {
		return;
	} else if (_parent->delegate()->elementAnimationsPaused()
		&& !activeRoundStreamed()) {
		return;
	}
	repaint();
}

void Gif::streamingReady(::Media::Streaming::Information &&info) {
	Expects(_streamed != nullptr);

	if (!ValidFrameSize(info.video.size, maxInlineArea())) {
		if (!info.video.size.isEmpty()) {
			_streamed->chosen->dimensions = info.video.size;
		}
		stopAnimation();
	} else {
		history()->owner().requestViewResize(_parent);
	}
}

void Gif::stopAnimation() {
	if (_streamed) {
		setStreamed(nullptr);
		history()->owner().requestViewResize(_parent);
	}
}

void Gif::checkAnimation() {
	if (_streamed && !autoplayEnabled()) {
		stopAnimation();
	}
}

float64 Gif::dataProgress() const {
	ensureDataMediaCreated();
	return (_data->uploading()
		|| (!_parent->data()->isSending() && !_parent->data()->hasFailed()))
		? _dataMedia->progress()
		: 0;
}

bool Gif::dataFinished() const {
	return (!_parent->data()->isSending() && !_parent->data()->hasFailed())
		? (!_data->loading() && !_data->uploading())
		: false;
}

bool Gif::dataLoaded() const {
	ensureDataMediaCreated();
	return !_parent->data()->isSending()
		&& !_parent->data()->hasFailed()
		&& _dataMedia->loaded();
}

bool Gif::needInfoDisplay() const {
	const auto item = _parent->data();
	if (item->isFakeAboutView()) {
		return false;
	}
	return item->isSending()
		|| item->awaitingVideoProcessing()
		|| _data->uploading()
		|| _parent->isUnderCursor()
		|| (_parent->delegate()->elementContext() == Context::ChatPreview)
		// Don't show the GIF badge if this message has text.
		|| (!_parent->hasBubble() && _parent->isLastAndSelfMessage());
}

bool Gif::needCornerStatusDisplay() const {
	return _data->isVideoFile()
		|| needInfoDisplay();
}

void Gif::ensureTranscribeButton() const {
	const auto media = _parent->data()->media();
	if (_data->isVideoMessage()
		&& (!media || !media->ttlSeconds())
		&& !_parent->data()->isScheduled()
		&& !_parent->data()->isAdminLogEntry()
		&& (_data->session().premium()
			|| _data->session().api().transcribes().trialsSupport())) {
		if (!_transcribe) {
			_transcribe = std::make_unique<TranscribeButton>(
				_realParent,
				true);
		}
	} else {
		_transcribe = nullptr;
	}
}

} // namespace HistoryView
