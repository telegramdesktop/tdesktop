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
#include "media/player/media_player_instance.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_utility.h"
#include "media/view/media_view_playback_progress.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_transcribe_button.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_media_spoiler.h"
#include "window/window_session_controller.h"
#include "core/application.h" // Application::showDocument.
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/image/image.h"
#include "ui/text/format_values.h"
#include "ui/grouped_layout.h"
#include "ui/cached_round_corners.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/spoiler_mess.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "data/data_document.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_document_media.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMaxGifForwardedBarLines = 4;
constexpr auto kUseNonBlurredThreshold = 240;
constexpr auto kMaxInlineArea = 1920 * 1080;

int gifMaxStatusWidth(DocumentData *document) {
	auto result = st::normalFont->width(Ui::FormatDownloadText(document->size, document->size));
	accumulate_max(result, st::normalFont->width(Ui::FormatGifAndSizeText(document->size)));
	return result;
}

} // namespace

struct Gif::Streamed {
	Streamed(
		std::shared_ptr<::Media::Streaming::Document> shared,
		Fn<void()> waitingCallback);
	::Media::Streaming::Instance instance;
	::Media::Streaming::FrameRequest frozenRequest;
	QImage frozenFrame;
	QString frozenStatusText;
};

Gif::Streamed::Streamed(
	std::shared_ptr<::Media::Streaming::Document> shared,
	Fn<void()> waitingCallback)
: instance(std::move(shared), std::move(waitingCallback)) {
}

Gif::Gif(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<DocumentData*> document,
	bool spoiler)
: File(parent, realParent)
, _data(document)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right())
, _spoiler(spoiler ? std::make_unique<MediaSpoiler>() : nullptr)
, _downloadSize(Ui::FormatSizeText(_data->size)) {
	setDocumentLinks(_data, realParent);
	setStatusSize(Ui::FileStatusSizeReady);

	if (_spoiler) {
		createSpoilerLink(_spoiler.get());
	}

	refreshCaption();
	if ((_dataMedia = _data->activeMediaView())) {
		dataMediaCreated();
	} else {
		_data->loadThumbnail(realParent->fullId());
		if (!autoplayEnabled()) {
			_data->loadVideoThumbnail(realParent->fullId());
		}
	}
	ensureTranscribeButton();
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
}

bool Gif::CanPlayInline(not_null<DocumentData*> document) {
	const auto dimensions = document->dimensions;
	return dimensions.width() * dimensions.height() <= kMaxInlineArea;
}

QSize Gif::sizeForAspectRatio() const {
	// We use size only for aspect ratio and we want to have it
	// as close to the thumbnail as possible.
	//if (!_data->dimensions.isEmpty()) {
	//	return _data->dimensions;
	//}
	if (_data->hasThumbnail()) {
		const auto &location = _data->thumbnailLocation();
		return { location.width(), location.height() };
	}
	return { 1, 1 };
}

QSize Gif::countThumbSize(int &inOutWidthMax) const {
	const auto maxSize = _data->isVideoFile()
		? st::maxMediaSize
		: _data->isVideoMessage()
		? st::maxVideoMessageSize
		: st::maxGifSize;
	const auto size = style::ConvertScale(videoSize());
	accumulate_min(inOutWidthMax, maxSize);
	return DownscaledSize(size, { inOutWidthMax, maxSize });
}

QSize Gif::countOptimalSize() {
	if (_parent->media() != this) {
		_caption = Ui::Text::String();
	} else if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}
	if (_data->isVideoMessage() && _transcribe) {
		const auto &entry = _data->session().api().transcribes().entry(
			_realParent);
		_transcribe->setLoading(
			entry.shown && (entry.requestId || entry.pending),
			[=] { repaint(); });
	}

	const auto minWidth = std::clamp(
		_parent->minWidthForMedia(),
		(_parent->hasBubble()
			? st::historyPhotoBubbleMinWidth
			: st::minPhotoSize),
		st::maxMediaSize);
	auto thumbMaxWidth = st::msgMaxWidth;
	const auto scaled = countThumbSize(thumbMaxWidth);
	auto maxWidth = std::min(
		std::max(scaled.width(), minWidth),
		thumbMaxWidth);
	auto minHeight = qMax(scaled.height(), st::minPhotoSize);
	if (!activeCurrentStreamed()) {
		accumulate_max(maxWidth, gifMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (_parent->hasBubble()) {
		if (!_caption.isEmpty()) {
			maxWidth = qMax(maxWidth, st::msgPadding.left()
				+ _caption.maxWidth()
				+ st::msgPadding.right());
			minHeight = adjustHeightForLessCrop(
				scaled,
				{ maxWidth, minHeight });
			minHeight += st::mediaCaptionSkip + _caption.minHeight();
			if (isBubbleBottom()) {
				minHeight += st::msgPadding.bottom();
			}
		}
	} else if (isUnwrapped()) {
		const auto item = _parent->data();
		auto via = item->Get<HistoryMessageVia>();
		auto reply = _parent->displayedReply();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		if (forwarded) {
			forwarded->create(via);
		}
		maxWidth += additionalWidth(via, reply, forwarded);
		accumulate_max(maxWidth, _parent->reactionsOptimalWidth());
	}
	return { maxWidth, minHeight };
}

QSize Gif::countCurrentSize(int newWidth) {
	auto availableWidth = newWidth;

	auto thumbMaxWidth = newWidth;
	const auto scaled = countThumbSize(thumbMaxWidth);
	const auto minWidthByInfo = _parent->infoWidth()
		+ 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x());
	newWidth = std::clamp(
		std::max(scaled.width(), minWidthByInfo),
		st::minPhotoSize,
		thumbMaxWidth);
	auto newHeight = qMax(scaled.height(), st::minPhotoSize);
	if (!activeCurrentStreamed()) {
		accumulate_max(newWidth, gifMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (_parent->hasBubble()) {
		accumulate_max(newWidth, _parent->minWidthForMedia());
		if (!_caption.isEmpty()) {
			const auto maxWithCaption = qMin(
				st::msgMaxWidth,
				(st::msgPadding.left()
					+ _caption.maxWidth()
					+ st::msgPadding.right()));
			newWidth = qMin(qMax(newWidth, maxWithCaption), thumbMaxWidth);
			newHeight = adjustHeightForLessCrop(
				scaled,
				{ newWidth, newHeight });
			const auto captionw = newWidth
				- st::msgPadding.left()
				- st::msgPadding.right();
			newHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				newHeight += st::msgPadding.bottom();
			}
		}
	} else if (isUnwrapped()) {
		accumulate_max(newWidth, _parent->reactionsOptimalWidth());

		const auto item = _parent->data();
		auto via = item->Get<HistoryMessageVia>();
		auto reply = _parent->displayedReply();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		if (via || reply || forwarded) {
			auto additional = additionalWidth(via, reply, forwarded);
			newWidth += additional;
			accumulate_min(newWidth, availableWidth);
			auto usew = maxWidth() - additional;
			auto availw = newWidth - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.left();
			if (!forwarded && via) {
				via->resize(availw);
			}
			if (reply) {
				reply->resize(availw);
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
		&& _realParent->allowsForward()
		&& _data->canBeStreamed(_realParent)
		&& !_data->inappPlaybackFailed();
}

bool Gif::autoplayEnabled() const {
	return Data::AutoDownload::ShouldAutoPlay(
		_data->session().settings().autoDownload(),
		_realParent->history()->peer,
		_data);
}

void Gif::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	ensureDataMediaCreated();
	const auto item = _parent->data();
	const auto loaded = dataLoaded();
	const auto displayLoading = (item->isSending() || _data->displayLoading());
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();
	const auto cornerDownload = downloadInCorner();
	const auto canBePlayed = _dataMedia->canBePlayed(_realParent);
	const auto autoplay = autoplayEnabled()
		&& canBePlayed
		&& CanPlayInline(_data);
	const auto activeRoundPlaying = activeRoundStreamed();

	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();
	const bool bubble = _parent->hasBubble();
	const auto outbg = context.outbg;
	const auto inWebPage = (_parent->media() != this);
	const auto isRound = _data->isVideoMessage();

	const auto rounding = inWebPage
		? std::optional<Ui::BubbleRounding>()
		: adjustedBubbleRoundingWithCaption(_caption);
	if (bubble) {
		if (!_caption.isEmpty()) {
			painth -= st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				painth -= st::msgPadding.bottom();
			}
		}
	}

	auto usex = 0, usew = paintw;
	const auto unwrapped = isUnwrapped();
	const auto via = unwrapped ? item->Get<HistoryMessageVia>() : nullptr;
	const auto reply = unwrapped ? _parent->displayedReply() : nullptr;
	const auto forwarded = unwrapped ? item->Get<HistoryMessageForwarded>() : nullptr;
	const auto rightAligned = unwrapped
		&& outbg
		&& !_parent->delegate()->elementIsChatWide();
	if (via || reply || forwarded) {
		usew = maxWidth() - additionalWidth(via, reply, forwarded);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (isRound) {
		accumulate_min(usew, painth);
	}
	if (rtl()) usex = width() - usex - usew;

	QRect rthumb(style::rtlrect(usex + paintx, painty, usew, painth, width()));

	const auto revealed = (!isRound && _spoiler)
		? _spoiler->revealAnimation.value(_spoiler->revealed ? 1. : 0.)
		: 1.;
	const auto fullHiddenBySpoiler = (revealed == 0.);
	if (revealed < 1.) {
		validateSpoilerImageCache(rthumb.size(), rounding);
	}

	const auto startPlay = autoplay
		&& !_streamed
		&& !activeRoundPlaying
		&& !fullHiddenBySpoiler;
	if (startPlay) {
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

	if (!bubble && !unwrapped) {
		Assert(rounding.has_value());
		fillImageShadow(p, rthumb, *rounding, context);
	}

	const auto skipDrawingContent = context.skipDrawingParts
		== PaintContext::SkipDrawingParts::Content;
	if (streamed && !skipDrawingContent && !fullHiddenBySpoiler) {
		auto paused = context.paused;
		auto request = ::Media::Streaming::FrameRequest{
			.outer = QSize(usew, painth) * cIntRetinaFactor(),
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
			if (!paused) {
				streamed->markFrameShown();
			}
		}

		if (const auto playback = videoPlayback()) {
			const auto value = playback->value();
			if (value > 0.) {
				auto pen = st->historyVideoMessageProgressFg()->p;
				auto was = p.pen();
				pen.setWidth(st::radialLine);
				pen.setCapStyle(Qt::RoundCap);
				p.setPen(pen);
				p.setOpacity(st::historyVideoMessageProgressOpacity);

				auto from = QuarterArcLength;
				auto len = -qRound(FullArcLength * value);
				auto stepInside = st::radialLine / 2;
				{
					PainterHighQualityEnabler hq(p);
					p.drawArc(rthumb.marginsRemoved(QMargins(stepInside, stepInside, stepInside, stepInside)), from, len);
				}

				p.setPen(was);
				p.setOpacity(1.);
			}
		}
	} else if (!skipDrawingContent && !fullHiddenBySpoiler) {
		ensureDataMediaCreated();
		validateThumbCache({ usew, painth }, isRound, rounding);
		p.drawImage(rthumb, _thumbCache);
	}

	if (!isRound && revealed < 1.) {
		p.setOpacity(1. - revealed);
		p.drawImage(rthumb.topLeft(), _spoiler->background);
		fillImageSpoiler(p, _spoiler.get(), rthumb, context);
		p.setOpacity(1.);
	}
	if (context.selected()) {
		if (isRound) {
			Ui::FillComplexEllipse(p, st, rthumb);
		} else {
			fillImageOverlay(p, rthumb, rounding, context);
		}
	}

	if (radial
		|| (!streamingMode
			&& ((!loaded && !_data->loading()) || !autoplay))) {
		const auto opacity = (item->isSending() || _data->uploading())
			? 1.
			: streamedForWaiting
			? streamedForWaiting->waitingOpacity()
			: (radial && loaded)
			? _animation->radial.opacity()
			: 1.;
		const auto radialOpacity = opacity * revealed;
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
			if (streamingMode && !_data->uploading()) {
				return nullptr;
			} else if ((loaded || canBePlayed) && (!radial || cornerDownload)) {
				return &sti->historyFileThumbPlay;
			} else if (radial || _data->loading()) {
				if (!item->isSending() || _data->uploading()) {
					return &sti->historyFileThumbCancel;
				}
				return nullptr;
			}
			return &sti->historyFileThumbDownload;
		}();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		p.setOpacity(revealed);
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

	if (!unwrapped && !skipDrawingSurrounding) {
		drawCornerStatus(p, context, QPoint());
	} else if (!skipDrawingSurrounding) {
		if (isRound) {
			const auto mediaUnread = item->hasUnreadMediaFlag();
			auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
			auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			auto statusX = usex + paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
			auto statusY = painty + painth - st::msgDateImgDelta - statusH + st::msgDateImgPadding.y();
			if (mediaUnread) {
				statusW += st::mediaUnreadSkip + st::mediaUnreadSize;
			}
			Ui::FillRoundRect(p, style::rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), sti->msgServiceBg, sti->msgServiceBgCornersSmall);
			p.setFont(st::normalFont);
			p.setPen(st->msgServiceFg());
			p.drawTextLeft(statusX, statusY, width(), _statusText, statusW - 2 * st::msgDateImgPadding.x());
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
		if (via || reply || forwarded) {
			auto rectw = width() - usew - st::msgReplyPadding.left();
			auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
			auto recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
			auto forwardedHeightReal = forwarded ? forwarded->text.countHeight(innerw) : 0;
			auto forwardedHeight = qMin(forwardedHeightReal, kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
			if (forwarded) {
				recth += forwardedHeight;
			} else if (via) {
				recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			}
			if (reply) {
				recth += st::msgReplyBarSize.height();
			}
			int rectx = rightAligned ? 0 : (usew + st::msgReplyPadding.left());
			int recty = painty;
			if (rtl()) rectx = width() - rectx - rectw;

			Ui::FillRoundRect(p, rectx, recty, rectw, recth, sti->msgServiceBg, sti->msgServiceBgCornersSmall);
			p.setPen(st->msgServiceFg());
			rectx += st::msgReplyPadding.left();
			rectw = innerw;
			if (forwarded) {
				p.setTextPalette(st->serviceTextPalette());
				auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
				forwarded->text.drawElided(p, rectx, recty + st::msgReplyPadding.top(), rectw, kMaxGifForwardedBarLines, style::al_left, 0, -1, 0, breakEverywhere);
				p.restoreTextPalette();

				const auto skip = std::min(
					forwarded->text.countHeight(rectw),
					kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
				recty += skip;
			} else if (via) {
				p.setFont(st::msgServiceNameFont);
				p.drawTextLeft(rectx, recty + st::msgReplyPadding.top(), 2 * rectx + rectw, via->text);
				int skip = st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
				recty += skip;
			}
			if (reply) {
				reply->paint(p, _parent, context, rectx, recty, rectw, false);
			}
		}
	}
	if (!unwrapped && !_caption.isEmpty()) {
		p.setPen(stm->historyTextFg);
		_parent->prepareCustomEmojiPaint(p, context, _caption);
		_caption.draw(p, {
			.position = QPoint(
				st::msgPadding.left(),
				painty + painth + st::mediaCaptionSkip),
			.availableWidth = captionw,
			.palette = &stm->textPalette,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.paused = context.paused,
			.selection = context.selection,
		});
	} else if (!inWebPage && !skipDrawingSurrounding) {
		auto fullRight = paintx + usex + usew;
		auto fullBottom = painty + painth;
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		if (unwrapped && !rightAligned) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		if (isRound || needInfoDisplay()) {
			_parent->drawInfo(
				p,
				context,
				fullRight,
				fullBottom,
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
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = fullBottom
				- st::historyFastShareBottom
				- (size ? size->height() : 0);
			if (fastShareLeft + rightActionWidth > maxRight) {
				fastShareLeft = fullRight
					- rightActionWidth
					- st::msgDateImgDelta;
				fastShareTop -= st::msgDateImgDelta
					+ st::msgDateImgPadding.y()
					+ st::msgDateFont->height
					+ st::msgDateImgPadding.y();
			}
			if (size) {
				_parent->drawRightAction(p, context, fastShareLeft, fastShareTop, 2 * paintx + paintw);
			}
			if (_transcribe) {
				paintTranscribe(p, fastShareLeft, fastShareTop, true, context);
			}
		}
		if (rightAligned && _transcribe) {
			paintTranscribe(p, usex, fullBottom, false, context);
		}
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

void Gif::validateVideoThumbnail() const {
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
	const auto good = _dataMedia->goodThumbnail();
	const auto normal = good ? good : _dataMedia->thumbnail();
	if (!normal) {
		_data->loadThumbnail(_realParent->fullId());
		validateVideoThumbnail();
	}
	const auto videothumb = normal ? nullptr : _videoThumbnailFrame.get();
	const auto blurred = normal
		? (!good
			&& (normal->width() < kUseNonBlurredThreshold)
			&& (normal->height() < kUseNonBlurredThreshold))
		: !videothumb;
	const auto ratio = style::DevicePixelRatio();
	if (_thumbCache.size() == (outer * ratio)
		&& _thumbCacheRounding == rounding
		&& _thumbCacheBlurred == blurred
		&& _thumbIsEllipse == isEllipse) {
		return;
	}
	auto cache = prepareThumbCache(outer);
	_thumbCache = isEllipse
		? Images::Circle(std::move(cache))
		: Images::Round(std::move(cache), MediaRoundingMask(rounding));
	_thumbCacheRounding = rounding;
	_thumbCacheBlurred = blurred;
}

QImage Gif::prepareThumbCache(QSize outer) const {
	const auto good = _dataMedia->goodThumbnail();
	const auto normal = good ? good : _dataMedia->thumbnail();
	const auto videothumb = normal ? nullptr : _videoThumbnailFrame.get();
	auto blurred = (!good
		&& normal
		&& (normal->width() < kUseNonBlurredThreshold)
		&& (normal->height() < kUseNonBlurredThreshold))
		? normal
		: nullptr;
	const auto blurFromLarge = good || (normal && !blurred);
	const auto large = blurFromLarge ? normal : videothumb;
	if (videothumb) {
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
	if (_spoiler->background.size() == (outer * ratio)
		&& _spoiler->backgroundRounding == rounding) {
		return;
	}
	const auto normal = _dataMedia->thumbnail();
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
	const auto embedded = _dataMedia->thumbnailInline();
	const auto blurred = embedded ? embedded : downscale(normal);
	_spoiler->background = Images::Round(
		PrepareWithBlurredBackground(
			outer,
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
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto text = (own && !own->frozenStatusText.isEmpty())
		? own->frozenStatusText
		: _statusText;
	const auto padding = st::msgDateImgPadding;
	const auto radial = _animation && _animation->radial.animating();
	const auto cornerDownload = downloadInCorner() && !dataLoaded() && !_data->loadedInMediaCache();
	const auto cornerMute = _streamed && _data->isVideoFile() && !cornerDownload;
	const auto addLeft = cornerDownload ? (st::historyVideoDownloadSize + 2 * padding.y()) : 0;
	const auto addRight = cornerMute ? st::historyVideoMuteSize : 0;
	const auto downloadWidth = cornerDownload ? st::normalFont->width(_downloadSize) : 0;
	const auto statusW = std::max(downloadWidth, st::normalFont->width(text)) + 2 * padding.x() + addLeft + addRight;
	const auto statusH = cornerDownload ? (st::historyVideoDownloadSize + 2 * padding.y()) : (st::normalFont->height + 2 * padding.y());
	const auto statusX = position.x() + st::msgDateImgDelta + padding.x();
	const auto statusY = position.y() + st::msgDateImgDelta + padding.y();
	const auto around = style::rtlrect(statusX - padding.x(), statusY - padding.y(), statusW, statusH, width());
	const auto statusTextTop = statusY + (cornerDownload ? (((statusH - 2 * st::normalFont->height) / 3)  - padding.y()) : 0);
	Ui::FillRoundRect(p, around, sti->msgDateImgBg, sti->msgDateImgBgCorners);
	p.setFont(st::normalFont);
	p.setPen(st->msgDateImgFg());
	p.drawTextLeft(statusX + addLeft, statusTextTop, width(), text, statusW - 2 * padding.x());
	if (cornerDownload) {
		const auto downloadTextTop = statusY + st::normalFont->height + (2 * (statusH - 2 * st::normalFont->height) / 3)  - padding.y();
		p.drawTextLeft(statusX + addLeft, downloadTextTop, width(), _downloadSize, statusW - 2 * padding.x());
		const auto inner = QRect(statusX + padding.y() - padding.x(), statusY, st::historyVideoDownloadSize, st::historyVideoDownloadSize);
		const auto &icon = _data->loading()
			? sti->historyVideoCancel
			: sti->historyVideoDownload;
		icon.paintInCenter(p, inner);
		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::historyVideoRadialLine, st::historyVideoRadialLine, st::historyVideoRadialLine, st::historyVideoRadialLine)));
			_animation->radial.draw(p, rinner, st::historyVideoRadialLine, sti->historyFileThumbRadialFg);
		}
	} else if (cornerMute) {
		sti->historyVideoMessageMute.paint(p, statusX - padding.x() - padding.y() + statusW - addRight, statusY - padding.y() + (statusH - st::historyVideoMessageMute.height()) / 2, width());
	}
}

TextState Gif::cornerStatusTextState(
		QPoint point,
		StateRequest request,
		QPoint position) const {
	auto result = TextState(_parent);
	if (!needCornerStatusDisplay() || !downloadInCorner() || dataLoaded()) {
		return result;
	}
	const auto padding = st::msgDateImgPadding;
	const auto statusX = position.x() + st::msgDateImgDelta + padding.x();
	const auto statusY = position.y() + st::msgDateImgDelta + padding.y();
	const auto inner = QRect(statusX + padding.y() - padding.x(), statusY, st::historyVideoDownloadSize, st::historyVideoDownloadSize);
	if (inner.contains(point)) {
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

	if (bubble && !_caption.isEmpty()) {
		auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();
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
		painth -= st::mediaCaptionSkip;
	}
	const auto outbg = _parent->hasOutLayout();
	const auto inWebPage = (_parent->media() != this);
	const auto isRound = _data->isVideoMessage();
	const auto unwrapped = isUnwrapped();
	const auto item = _parent->data();
	auto usew = paintw, usex = 0;
	const auto via = unwrapped ? item->Get<HistoryMessageVia>() : nullptr;
	const auto reply = unwrapped ? _parent->displayedReply() : nullptr;
	const auto forwarded = unwrapped ? item->Get<HistoryMessageForwarded>() : nullptr;
	const auto rightAligned = unwrapped
		&& outbg
		&& !_parent->delegate()->elementIsChatWide();
	if (via || reply || forwarded) {
		usew = maxWidth() - additionalWidth(via, reply, forwarded);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (isRound) {
		accumulate_min(usew, painth);
	}
	if (rtl()) usex = width() - usex - usew;

	if (via || reply || forwarded) {
		auto rectw = paintw - usew - st::msgReplyPadding.left();
		auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
		auto recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
		auto forwardedHeightReal = forwarded ? forwarded->text.countHeight(innerw) : 0;
		auto forwardedHeight = qMin(forwardedHeightReal, kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
		if (forwarded) {
			recth += forwardedHeight;
		} else if (via) {
			recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			recth += st::msgReplyBarSize.height();
		}
		auto rectx = rightAligned ? 0 : (usew + st::msgReplyPadding.left());
		auto recty = painty;
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
			if (QRect(rectx, recty, rectw, recth).contains(point)) {
				result.link = reply->replyToLink();
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
		result.link = (_spoiler && !_spoiler->revealed)
			? _spoiler->link
			: _data->uploading()
			? _cancell
			: _realParent->isSending()
			? nullptr
			: (dataLoaded() || _dataMedia->canBePlayed(_realParent))
			? _openl
			: _data->loading()
			? _cancell
			: _savel;
	}
	if (unwrapped || _caption.isEmpty()) {
		auto fullRight = usex + paintx + usew;
		auto fullBottom = painty + painth;
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		if (unwrapped && !rightAligned) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		if (!inWebPage) {
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
		}
		if (const auto size = bubble ? std::nullopt : _parent->rightActionSize()) {
			const auto rightActionWidth = size->width();
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = fullBottom
				- st::historyFastShareBottom
				- size->height();
			if (fastShareLeft + rightActionWidth > maxRight) {
				fastShareLeft = fullRight
					- rightActionWidth
					- st::msgDateImgDelta;
				fastShareTop -= st::msgDateImgDelta
					+ st::msgDateImgPadding.y()
					+ st::msgDateFont->height
					+ st::msgDateImgPadding.y();
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

TextForMimeData Gif::selectedText(TextSelection selection) const {
	return _caption.toTextForMimeData(selection);
}

bool Gif::fullFeaturedGrouped(RectParts sides) const {
	return (sides & RectPart::Left) && (sides & RectPart::Right);
}

QSize Gif::sizeForGroupingOptimal(int maxWidth) const {
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
	const auto fullFeatured = fullFeaturedGrouped(sides);
	const auto cornerDownload = fullFeatured && downloadInCorner();
	const auto canBePlayed = _dataMedia->canBePlayed(_realParent);

	const auto revealed = _spoiler
		? _spoiler->revealAnimation.value(_spoiler->revealed ? 1. : 0.)
		: 1.;
	const auto fullHiddenBySpoiler = (revealed == 0.);
	if (revealed < 1.) {
		validateSpoilerImageCache(geometry.size(), rounding);
	}

	const auto autoplay = fullFeatured
		&& autoplayEnabled()
		&& canBePlayed
		&& CanPlayInline(_data);
	const auto startPlay = autoplay && !_streamed;
	if (startPlay) {
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
		const auto pixSize = Ui::GetImageScaleSizeForGeometry(
			{ originalWidth, originalHeight },
			{ geometry.width(), geometry.height() });
		auto request = ::Media::Streaming::FrameRequest{
			.resize = pixSize * cIntRetinaFactor(),
			.outer = geometry.size() * cIntRetinaFactor(),
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
			if (!context.paused) {
				streamed->markFrameShown();
			}
		}
	} else if (!fullHiddenBySpoiler) {
		validateGroupedCache(geometry, rounding, cacheKey, cache);
		p.drawPixmap(geometry, *cache);
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

	if (radial
		|| (!streamingMode
			&& ((!loaded && !_data->loading()) || !autoplay))) {
		const auto opacity = (item->isSending() || _data->uploading())
			? 1.
			: streamedForWaiting
			? streamedForWaiting->waitingOpacity()
			: (radial && loaded)
			? _animation->radial.opacity()
			: 1.;
		const auto radialOpacity = opacity * revealed;
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
			} else if (streamingMode && !_data->uploading()) {
				return nullptr;
			} else if ((loaded || canBePlayed) && (!radial || cornerDownload)) {
				return &sti->historyFileThumbPlay;
			} else if (radial || _data->loading()) {
				if (!item->isSending() || _data->uploading()) {
					return &sti->historyFileThumbCancel;
				}
				return nullptr;
			}
			return &sti->historyFileThumbDownload;
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
		p.setOpacity(revealed);
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
	if (fullFeatured) {
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
	if (fullFeaturedGrouped(sides)) {
		if (const auto state = cornerStatusTextState(point, request, geometry.topLeft()); state.link) {
			return state;
		}
	}
	ensureDataMediaCreated();
	return TextState(_parent, (_spoiler && !_spoiler->revealed)
		? _spoiler->link
		: _data->uploading()
		? _cancell
		: _realParent->isSending()
		? nullptr
		: (dataLoaded() || _dataMedia->canBePlayed(_realParent))
		? _openl
		: _data->loading()
		? _cancell
		: _savel);
}

void Gif::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	dataMediaCreated();
}

void Gif::dataMediaCreated() const {
	Expects(_dataMedia != nullptr);

	_dataMedia->goodThumbnailWanted();
	_dataMedia->thumbnailWanted(_realParent->fullId());
	if (!autoplayEnabled()) {
		_dataMedia->videoThumbnailWanted(_realParent->fullId());
	}
	history()->owner().registerHeavyViewPart(_parent);
}

bool Gif::uploading() const {
	return _data->uploading();
}

void Gif::hideSpoilers() {
	_caption.setSpoilerRevealed(false, anim::type::instant);
	if (_spoiler) {
		_spoiler->revealed = false;
	}
}

bool Gif::needsBubble() const {
	if (_data->isVideoMessage()) {
		return false;
	} else if (!_caption.isEmpty()) {
		return true;
	}
	const auto item = _parent->data();
	return item->repliesAreComments()
		|| item->externalReply()
		|| item->viaBot()
		|| _parent->displayedReply()
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
	const auto outbg = _parent->hasOutLayout();
	const auto rightAligned = outbg
		&& !_parent->delegate()->elementIsChatWide();
	const auto item = _parent->data();
	const auto via = item->Get<HistoryMessageVia>();
	const auto reply = _parent->displayedReply();
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	if (via || reply || forwarded) {
		usew = maxWidth() - additionalWidth(via, reply, forwarded);
	}
	accumulate_max(usew, _parent->reactionsOptimalWidth());
	if (rightAligned) {
		usex = width() - usew;
	}
	if (rtl()) usex = width() - usex - usew;
	return style::rtlrect(usex + paintx, painty, usew, painth, width());
}

std::optional<int> Gif::reactionButtonCenterOverride() const {
	if (!isUnwrapped()) {
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
	const auto unwrapped = isUnwrapped();
	if (unwrapped) {
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		const auto infoWidth = _parent->infoWidth();
		const auto outbg = _parent->hasOutLayout();
		const auto rightAligned = outbg
			&& !_parent->delegate()->elementIsChatWide();
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
		item->Get<HistoryMessageVia>(),
		item->Get<HistoryMessageReply>(),
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

	const auto good = _dataMedia->goodThumbnail();
	const auto thumb = _dataMedia->thumbnail();
	const auto image = good
		? good
		: thumb
		? thumb
		: _dataMedia->thumbnailInline();
	const auto blur = !good
		&& (!thumb
			|| (thumb->width() < kUseNonBlurredThreshold
				&& thumb->height() < kUseNonBlurredThreshold));

	const auto loadLevel = good ? 3 : thumb ? 2 : image ? 1 : 0;
	const auto width = geometry.width();
	const auto height = geometry.height();
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
	auto scaled = Images::Prepare(
		(image ? image : Image::BlankMedia().get())->original(),
		pixSize * ratio,
		{ .options = options, .outer = { width, height } });
	auto rounded = Images::Round(
		std::move(scaled),
		MediaRoundingMask(rounding));
	*cache = Ui::PixmapFromImage(std::move(rounded));
}

void Gif::setStatusSize(int64 newSize) const {
	if (newSize < 0) {
		_statusSize = newSize;
		_statusText = Ui::FormatDurationText(-newSize - 1);
	} else if (_data->isVideoMessage()) {
		_statusSize = newSize;
		_statusText = Ui::FormatDurationText(_data->getDuration());
	} else {
		File::setStatusSize(
			newSize,
			_data->size,
			_data->isVideoFile() ? _data->getDuration() : -2,
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
	} else if (dataLoaded() || _dataMedia->canBePlayed(_realParent)) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}
	const auto round = activeRoundStreamed();
	const auto own = activeOwnStreamed();
	if (round || (own && own->frozenFrame.isNull() && _data->isVideoFile())) {
		const auto streamed = round ? round : &own->instance;
		const auto state = streamed->player().prepareLegacyState();
		if (state.length) {
			auto position = int64(0);
			if (::Media::Player::IsStoppedAtEnd(state.state)) {
				position = state.length;
			} else if (!::Media::Player::IsStoppedOrStopping(state.state)) {
				position = state.position;
			}
			statusSize = -1 - int((state.length - position) / state.frequency + 1);
		} else {
			statusSize = -1 - _data->getDuration();
		}
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
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

void Gif::parentTextUpdated() {
	if (_parent->media() == this) {
		refreshCaption();
		history()->owner().requestViewResize(_parent);
	}
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
	_videoThumbnailFrame = nullptr;
	_caption.unloadPersistentAnimation();
}

void Gif::refreshParentId(not_null<HistoryItem*> realParent) {
	File::refreshParentId(realParent);
	if (_parent->media() == this) {
		refreshCaption();
	}
}

void Gif::refreshCaption() {
	_caption = createCaption(_parent->data());
}

int Gif::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply, const HistoryMessageForwarded *forwarded) const {
	int result = 0;
	if (forwarded) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + forwarded->text.maxWidth() + st::msgReplyPadding.right());
	} else if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

::Media::Streaming::Instance *Gif::activeRoundStreamed() const {
	return ::Media::Player::instance()->roundVideoStreamed(_parent->data());
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
	} else if (_dataMedia->canBePlayed(_realParent)) {
		if (!autoplayEnabled()) {
			history()->owner().checkPlayingAnimations();
		}
		createStreamedPlayer();
	}
}

void Gif::createStreamedPlayer() {
	auto shared = _data->owner().streaming().sharedDocument(
		_data,
		_realParent->fullId());
	if (!shared) {
		return;
	}
	setStreamed(std::make_unique<Streamed>(
		std::move(shared),
		[=] { repaintStreamedContent(); }));

	_streamed->instance.player().updates(
	) | rpl::start_with_next_error([=](::Media::Streaming::Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](::Media::Streaming::Error &&error) {
		handleStreamingError(std::move(error));
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
	//}
	_streamed->instance.play(options);
}

void Gif::checkStreamedIsStarted() const {
	if (!_streamed || _streamed->instance.playerLocked()) {
		return;
	} else if (_streamed->instance.paused()) {
		_streamed->instance.resume();
	}
	if (!_streamed->instance.active() && !_streamed->instance.failed()) {
		startStreamedPlayer();
	}
}

void Gif::setStreamed(std::unique_ptr<Streamed> value) {
	const auto removed = (_streamed && !value);
	const auto set = (!_streamed && value);
	_streamed = std::move(value);
	if (set) {
		history()->owner().registerHeavyViewPart(_parent);
	} else if (removed) {
		_parent->checkHeavyPart();
	}
}

void Gif::handleStreamingUpdate(::Media::Streaming::Update &&update) {
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
	if (info.video.size.width() * info.video.size.height()
		> kMaxInlineArea) {
		_data->dimensions = info.video.size;
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
	return _parent->data()->isSending()
		|| _data->uploading()
		|| _parent->isUnderCursor()
		// Don't show the GIF badge if this message has text.
		|| (!_parent->hasBubble() && _parent->isLastAndSelfMessage());
}

bool Gif::needCornerStatusDisplay() const {
	return _data->isVideoFile()
		|| needInfoDisplay();
}

void Gif::ensureTranscribeButton() const {
	if (_data->isVideoMessage() && _data->session().premium()) {
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
