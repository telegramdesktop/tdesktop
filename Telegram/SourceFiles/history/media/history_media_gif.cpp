/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_gif.h"

#include "lang/lang_keys.h"
#include "layout.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "media/media_audio.h"
#include "media/media_clip_reader.h"
#include "media/player/media_player_round_controller.h"
#include "media/view/media_clip_playback.h"
#include "boxes/confirm_box.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "window/window_controller.h"
#include "ui/image/image.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "styles/style_history.h"

namespace {

using TextState = HistoryView::TextState;

} // namespace

namespace {

constexpr auto kMaxGifForwardedBarLines = 4;

int gifMaxStatusWidth(DocumentData *document) {
	auto result = st::normalFont->width(formatDownloadText(document->size, document->size));
	accumulate_max(result, st::normalFont->width(formatGifAndSizeText(document->size)));
	return result;
}

} // namespace

HistoryGif::HistoryGif(
	not_null<Element*> parent,
	not_null<DocumentData*> document)
: HistoryFileMedia(parent, parent->data())
, _data(document)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	const auto item = parent->data();
	setDocumentLinks(_data, item, true);

	setStatusSize(FileStatusSizeReady);

	_caption = createCaption(item);
	_data->thumb->load(item->fullId());
}

QSize HistoryGif::countOptimalSize() {
	if (_parent->media() != this) {
		_caption = Text();
	} else if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}
	if (!_openInMediaviewLink) {
		_openInMediaviewLink = std::make_shared<DocumentOpenClickHandler>(
			_data,
			_parent->data()->fullId());
	}

	auto tw = 0;
	auto th = 0;
	if (_gif && _gif->state() == Media::Clip::State::Error) {
		if (!_gif->autoplay()) {
			Ui::show(Box<InformBox>(lang(lng_gif_error)));
		}
		setClipReader(Media::Clip::ReaderPointer::Bad());
	}

	const auto reader = currentReader();
	if (reader) {
		tw = ConvertScale(reader->width());
		th = ConvertScale(reader->height());
	} else {
		tw = ConvertScale(_data->dimensions.width()), th = ConvertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = ConvertScale(_data->thumb->width());
			th = ConvertScale(_data->thumb->height());
		}
	}
	const auto maxSize = _data->isVideoMessage()
		? st::maxVideoMessageSize
		: st::maxGifSize;
	if (tw > maxSize) {
		th = (maxSize * th) / tw;
		tw = maxSize;
	}
	if (th > maxSize) {
		tw = (maxSize * tw) / th;
		th = maxSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}
	_thumbw = tw;
	_thumbh = th;
	auto maxWidth = qMax(tw, st::minPhotoSize);
	auto minHeight = qMax(th, st::minPhotoSize);
	accumulate_max(maxWidth, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	if (!reader) {
		accumulate_max(maxWidth, gifMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (_parent->hasBubble()) {
		if (!_caption.isEmpty()) {
			auto captionw = maxWidth - st::msgPadding.left() - st::msgPadding.right();
			minHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				minHeight += st::msgPadding.bottom();
			}
		}
	} else if (isSeparateRoundVideo()) {
		const auto item = _parent->data();
		auto via = item->Get<HistoryMessageVia>();
		auto reply = item->Get<HistoryMessageReply>();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		if (forwarded) {
			forwarded->create(via);
		}
		maxWidth += additionalWidth(via, reply, forwarded);
	}
	return { maxWidth, minHeight };
}

QSize HistoryGif::countCurrentSize(int newWidth) {
	auto availableWidth = newWidth;

	int tw = 0, th = 0;
	const auto reader = currentReader();
	if (reader) {
		tw = ConvertScale(reader->width());
		th = ConvertScale(reader->height());
	} else {
		tw = ConvertScale(_data->dimensions.width()), th = ConvertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = ConvertScale(_data->thumb->width());
			th = ConvertScale(_data->thumb->height());
		}
	}
	const auto maxSize = _data->isVideoMessage()
		? st::maxVideoMessageSize
		: st::maxGifSize;
	if (tw > maxSize) {
		th = (maxSize * th) / tw;
		tw = maxSize;
	}
	if (th > maxSize) {
		tw = (maxSize * tw) / th;
		th = maxSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}

	if (newWidth < tw) {
		th = qRound((newWidth / float64(tw)) * th);
		tw = newWidth;
	}
	_thumbw = tw;
	_thumbh = th;

	newWidth = qMax(tw, st::minPhotoSize);
	auto newHeight = qMax(th, st::minPhotoSize);
	accumulate_max(newWidth, _parent->infoWidth() + 2 * st::msgDateImgDelta + st::msgDateImgPadding.x());
	if (reader) {
		const auto own = (reader->mode() == Media::Clip::Reader::Mode::Gif);
		if (own && !reader->started()) {
			auto isRound = _data->isVideoMessage();
			auto inWebPage = (_parent->media() != this);
			auto roundRadius = isRound
				? ImageRoundRadius::Ellipse
				: inWebPage
				? ImageRoundRadius::Small
				: ImageRoundRadius::Large;
			auto roundCorners = (isRound || inWebPage)
				? RectPart::AllCorners
				: ((isBubbleTop()
					? (RectPart::TopLeft | RectPart::TopRight)
					: RectPart::None)
				| ((isBubbleBottom() && _caption.isEmpty())
					? (RectPart::BottomLeft | RectPart::BottomRight)
					: RectPart::None));
			reader->start(
				_thumbw,
				_thumbh,
				newWidth,
				newHeight,
				roundRadius,
				roundCorners);
		}
	} else {
		accumulate_max(newWidth, gifMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (_parent->hasBubble()) {
		if (!_caption.isEmpty()) {
			auto captionw = newWidth - st::msgPadding.left() - st::msgPadding.right();
			newHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				newHeight += st::msgPadding.bottom();
			}
		}
	} else if (isSeparateRoundVideo()) {
		const auto item = _parent->data();
		auto via = item->Get<HistoryMessageVia>();
		auto reply = item->Get<HistoryMessageReply>();
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

void HistoryGif::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	const auto item = _parent->data();
	_data->automaticLoad(_realParent->fullId(), item);
	auto loaded = _data->loaded();
	auto displayLoading = (item->id < 0) || _data->displayLoading();
	auto selected = (selection == FullSelection);

	if (loaded
		&& cAutoPlayGif()
		&& !_gif
		&& !_gif.isBad()
		&& !activeRoundVideo()) {
		_parent->delegate()->elementAnimationAutoplayAsync(_parent);
	}

	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();
	auto outbg = _parent->hasOutLayout();
	auto inWebPage = (_parent->media() != this);

	auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();

	auto isRound = _data->isVideoMessage();
	auto displayMute = false;
	const auto reader = currentReader();
	const auto playingVideo = reader
		? (reader->mode() == Media::Clip::Reader::Mode::Video)
		: false;
	const auto animating = reader && reader->started();

	if ((!animating || item->id < 0) && displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(dataProgress());
		}
	}
	updateStatusText();
	auto radial = isRadialAnimation(ms);

	if (bubble) {
		if (!_caption.isEmpty()) {
			painth -= st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				painth -= st::msgPadding.bottom();
			}
		}
	} else if (!isRound) {
		App::roundShadow(p, 0, 0, paintw, height(), selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	auto usex = 0, usew = paintw;
	auto separateRoundVideo = isSeparateRoundVideo();
	auto via = separateRoundVideo ? item->Get<HistoryMessageVia>() : nullptr;
	auto reply = separateRoundVideo ? item->Get<HistoryMessageReply>() : nullptr;
	auto forwarded = separateRoundVideo ? item->Get<HistoryMessageForwarded>() : nullptr;
	if (via || reply || forwarded) {
		usew = maxWidth() - additionalWidth(via, reply, forwarded);
		if (outbg) {
			usex = width() - usew;
		}
	}
	if (rtl()) usex = width() - usex - usew;

	QRect rthumb(rtlrect(usex + paintx, painty, usew, painth, width()));

	auto roundRadius = isRound ? ImageRoundRadius::Ellipse : inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
	auto roundCorners = (isRound || inWebPage) ? RectPart::AllCorners : ((isBubbleTop() ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
		| ((isBubbleBottom() && _caption.isEmpty()) ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None));
	if (animating) {
		auto paused = App::wnd()->controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
		if (isRound) {
			if (playingVideo) {
				paused = false;
			} else {
				displayMute = true;
			}
		}
		p.drawPixmap(rthumb.topLeft(), reader->current(_thumbw, _thumbh, usew, painth, roundRadius, roundCorners, paused ? 0 : ms));

		if (const auto playback = videoPlayback()) {
			const auto value = playback->value(ms);
			if (value > 0.) {
				auto pen = st::historyVideoMessageProgressFg->p;
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
	} else {
		const auto good = _data->goodThumbnail();
		if (good && good->loaded()) {
			p.drawPixmap(rthumb.topLeft(), good->pixSingle({}, _thumbw, _thumbh, usew, painth, roundRadius, roundCorners));
		} else {
			if (good) {
				good->load({});
			}
			p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_realParent->fullId(), _thumbw, _thumbh, usew, painth, roundRadius, roundCorners));
		}
	}

	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (radial || (!reader && (_gif.isBad() || (!loaded && !_data->loading()) || !cAutoPlayGif()))) {
		auto radialOpacity = (radial && loaded && item->id > 0) ? _animation->radial.opacity() : 1.;
		auto inner = QRect(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			auto over = _animation->a_thumbOver.current();
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		auto icon = [&]() -> const style::icon * {
			if (_data->loaded() && !radial) {
				return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
			} else if (radial || _data->loading()) {
				if (item->id > 0 || _data->uploading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		}();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
		}

		if (!isRound && (!animating || item->id < 0)) {
			auto statusX = paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
			auto statusY = painty + st::msgDateImgDelta + st::msgDateImgPadding.y();
			auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
			auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
			p.setFont(st::normalFont);
			p.setPen(st::msgDateImgFg);
			p.drawTextLeft(statusX, statusY, width(), _statusText, statusW - 2 * st::msgDateImgPadding.x());
		}
	}
	if (displayMute) {
		auto muteRect = rtlrect(rthumb.x() + (rthumb.width() - st::historyVideoMessageMuteSize) / 2, rthumb.y() + st::msgDateImgDelta, st::historyVideoMessageMuteSize, st::historyVideoMessageMuteSize, width());
		p.setPen(Qt::NoPen);
		p.setBrush(selected ? st::msgDateImgBgSelected : st::msgDateImgBg);
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(muteRect);
		(selected ? st::historyVideoMessageMuteSelected : st::historyVideoMessageMute).paintInCenter(p, muteRect);
	}

	if (!inWebPage && isRound) {
		auto mediaUnread = item->isMediaUnread();
		auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
		auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		auto statusX = usex + paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
		auto statusY = painty + painth - st::msgDateImgDelta - statusH + st::msgDateImgPadding.y();
		if (item->isMediaUnread()) {
			statusW += st::mediaUnreadSkip + st::mediaUnreadSize;
		}
		App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
		p.setFont(st::normalFont);
		p.setPen(st::msgServiceFg);
		p.drawTextLeft(statusX, statusY, width(), _statusText, statusW - 2 * st::msgDateImgPadding.x());
		if (mediaUnread) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgServiceFg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(rtlrect(statusX - st::msgDateImgPadding.x() + statusW - st::msgDateImgPadding.x() - st::mediaUnreadSize, statusY + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width()));
			}
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
			int rectx = outbg ? 0 : (usew + st::msgReplyPadding.left());
			int recty = painty;
			if (rtl()) rectx = width() - rectx - rectw;

			App::roundRect(p, rectx, recty, rectw, recth, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
			p.setPen(st::msgServiceFg);
			rectx += st::msgReplyPadding.left();
			rectw = innerw;
			if (forwarded) {
				p.setTextPalette(st::serviceTextPalette);
				auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
				forwarded->text.drawElided(p, rectx, recty + st::msgReplyPadding.top(), rectw, kMaxGifForwardedBarLines, style::al_left, 0, -1, 0, breakEverywhere);
				p.restoreTextPalette();
			} else if (via) {
				p.setFont(st::msgDateFont);
				p.drawTextLeft(rectx, recty + st::msgReplyPadding.top(), 2 * rectx + rectw, via->text);
				int skip = st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
				recty += skip;
			}
			if (reply) {
				HistoryMessageReply::PaintFlags flags = 0;
				if (selected) {
					flags |= HistoryMessageReply::PaintFlag::Selected;
				}
				reply->paint(p, _parent, rectx, recty, rectw, flags);
			}
		}
	}
	if (!isRound && !_caption.isEmpty()) {
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), painty + painth + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	} else if (!inWebPage) {
		auto fullRight = paintx + usex + usew;
		auto fullBottom = painty + painth;
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		if (isRound && !outbg) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		if (isRound || needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, 2 * paintx + paintw, selected, isRound ? InfoDisplayType::Background : InfoDisplayType::Image);
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (fastShareLeft + st::historyFastShareSize > maxRight) {
				fastShareLeft = (fullRight - st::historyFastShareSize - st::msgDateImgDelta);
				fastShareTop -= (st::msgDateImgDelta + st::msgDateImgPadding.y() + st::msgDateFont->height + st::msgDateImgPadding.y());
			}
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

TextState HistoryGif::textState(QPoint point, StateRequest request) const {
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
	auto outbg = _parent->hasOutLayout();
	auto inWebPage = (_parent->media() != this);
	auto isRound = _data->isVideoMessage();
	auto usew = paintw, usex = 0;
	auto separateRoundVideo = isSeparateRoundVideo();
	const auto item = _parent->data();
	auto via = separateRoundVideo ? item->Get<HistoryMessageVia>() : nullptr;
	auto reply = separateRoundVideo ? item->Get<HistoryMessageReply>() : nullptr;
	auto forwarded = separateRoundVideo ? item->Get<HistoryMessageForwarded>() : nullptr;
	if (via || reply || forwarded) {
		usew = maxWidth() - additionalWidth(via, reply, forwarded);
		if (outbg) {
			usex = width() - usew;
		}
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
		auto rectx = outbg ? 0 : (usew + st::msgReplyPadding.left());
		auto recty = painty;
		if (rtl()) rectx = width() - rectx - rectw;

		if (forwarded) {
			if (QRect(rectx, recty, rectw, st::msgReplyPadding.top() + forwardedHeight).contains(point)) {
				auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
				auto textRequest = request.forText();
				if (breakEverywhere) {
					textRequest.flags |= Text::StateRequest::Flag::BreakEverywhere;
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
	if (QRect(usex + paintx, painty, usew, painth).contains(point)) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else if (!_gif || !cAutoPlayGif() || _data->isVideoMessage()) {
			result.link = _data->loaded() ? _openl : (_data->loading() ? _cancell : _savel);
		} else {
			result.link = _openInMediaviewLink;
		}
	}
	if (isRound || _caption.isEmpty()) {
		auto fullRight = usex + paintx + usew;
		auto fullBottom = painty + painth;
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		if (isRound && !outbg) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		if (!inWebPage) {
			if (_parent->pointInTime(fullRight, fullBottom, point, isRound ? InfoDisplayType::Background : InfoDisplayType::Image)) {
				result.cursor = CursorState::Date;
			}
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (fastShareLeft + st::historyFastShareSize > maxRight) {
				fastShareLeft = (fullRight - st::historyFastShareSize - st::msgDateImgDelta);
				fastShareTop -= st::msgDateImgDelta + st::msgDateImgPadding.y() + st::msgDateFont->height + st::msgDateImgPadding.y();
			}
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}
	return result;
}

TextWithEntities HistoryGif::selectedText(TextSelection selection) const {
	return _caption.originalTextWithEntities(selection, ExpandLinksAll);
}

bool HistoryGif::uploading() const {
	return _data->uploading();
}

bool HistoryGif::needsBubble() const {
	if (_data->isVideoMessage()) {
		return false;
	}
	if (!_caption.isEmpty()) {
		return true;
	}
	const auto item = _parent->data();
	return item->viaBot()
		|| item->Has<HistoryMessageReply>()
		|| _parent->displayForwardedFrom()
		|| _parent->displayFromName();
	return false;
}

int HistoryGif::additionalWidth() const {
	const auto item = _parent->data();
	return additionalWidth(
		item->Get<HistoryMessageVia>(),
		item->Get<HistoryMessageReply>(),
		item->Get<HistoryMessageForwarded>());
}

QString HistoryGif::mediaTypeString() const {
	return _data->isVideoMessage()
		? lang(lng_in_dlg_video_message)
		: qsl("GIF");
}

bool HistoryGif::isSeparateRoundVideo() const {
	return _data->isVideoMessage()
		&& (_parent->media() == this)
		&& !_parent->hasBubble();
}

void HistoryGif::setStatusSize(int newSize) const {
	if (_data->isVideoMessage()) {
		_statusSize = newSize;
		if (newSize < 0) {
			_statusText = formatDurationText(-newSize - 1);
		} else {
			_statusText = formatDurationText(_data->duration());
		}
	} else {
		HistoryFileMedia::setStatusSize(newSize, _data->size, -2, 0);
	}
}

void HistoryGif::updateStatusText() const {
	auto showPause = false;
	auto statusSize = 0;
	auto realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
		if (const auto video = activeRoundPlayer()) {
			statusSize = -1 - _data->duration();

			const auto state = Media::Player::mixer()->currentState(
				AudioMsgId::Type::Voice);
			if (state.id == video->audioMsgId() && state.length) {
				auto position = int64(0);
				if (Media::Player::IsStoppedAtEnd(state.state)) {
					position = state.length;
				} else if (!Media::Player::IsStoppedOrStopping(state.state)) {
					position = state.position;
				}
				accumulate_max(statusSize, -1 - int((state.length - position) / state.frequency + 1));
			}
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

void HistoryGif::refreshParentId(not_null<HistoryItem*> realParent) {
	HistoryFileMedia::refreshParentId(realParent);

	const auto fullId = realParent->fullId();
	if (_openInMediaviewLink) {
		_openInMediaviewLink->setMessageId(fullId);
	}
}

QString HistoryGif::additionalInfoString() const {
	if (_data->isVideoMessage()) {
		updateStatusText();
		return _statusText;
	}
	return QString();
}

bool HistoryGif::isReadyForOpen() const {
	return _data->loaded();
}

void HistoryGif::parentTextUpdated() {
	_caption = (_parent->media() == this)
		? createCaption(_parent->data())
		: Text();
	Auth().data().requestViewResize(_parent);
}

int HistoryGif::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply, const HistoryMessageForwarded *forwarded) const {
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

Media::Player::RoundController *HistoryGif::activeRoundVideo() const {
	return App::wnd()->controller()->roundVideo(_parent->data());
}

Media::Clip::Reader *HistoryGif::activeRoundPlayer() const {
	if (const auto video = activeRoundVideo()) {
		if (const auto result = video->reader()) {
			if (result->ready()) {
				return result;
			}
		}
	}
	return nullptr;
}

Media::Clip::Reader *HistoryGif::currentReader() const {
	if (const auto result = activeRoundPlayer()) {
		return result;
	}
	return (_gif && _gif->ready()) ? _gif.get() : nullptr;
}

Media::Clip::Playback *HistoryGif::videoPlayback() const {
	if (const auto video = activeRoundVideo()) {
		return video->playback();
	}
	return nullptr;
}

void HistoryGif::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;

	const auto reader = _gif.get();
	if (!reader) {
		return;
	}
	switch (notification) {
	case NotificationReinit: {
		auto stopped = false;
		if (reader->autoPausedGif()) {
			auto amVisible = false;
			Auth().data().queryItemVisibility().notify(
				{ _parent->data(), &amVisible },
				true);
			if (!amVisible) { // Stop animation if it is not visible.
				stopAnimation();
				stopped = true;
			}
		}
		if (!stopped) {
			Auth().data().requestViewResize(_parent);
		}
	} break;

	case NotificationRepaint: {
		if (!reader->currentDisplayed()) {
			Auth().data().requestViewRepaint(_parent);
		}
	} break;
	}
}

void HistoryGif::playAnimation(bool autoplay) {
	if (_data->isVideoMessage() && !autoplay) {
		return;
	} else if (_gif && autoplay) {
		return;
	}
	using Mode = Media::Clip::Reader::Mode;
	if (_gif) {
		stopAnimation();
	} else if (_data->loaded(DocumentData::FilePathResolveChecked)) {
		if (!cAutoPlayGif()) {
			Auth().data().stopAutoplayAnimations();
		}
		setClipReader(Media::Clip::MakeReader(
			_data,
			_parent->data()->fullId(),
			[=](auto notification) { clipCallback(notification); },
			Mode::Gif));
		if (_gif && autoplay) {
			_gif->setAutoplay();
		}
	}
}

void HistoryGif::stopAnimation() {
	if (_gif) {
		clearClipReader();
		Auth().data().requestViewResize(_parent);
		_data->unload();
	}
}

void HistoryGif::setClipReader(Media::Clip::ReaderPointer gif) {
	if (_gif) {
		Auth().data().unregisterAutoplayAnimation(_gif.get());
	}
	_gif = std::move(gif);
	if (_gif) {
		Auth().data().registerAutoplayAnimation(_gif.get(), _parent);
	}
}

HistoryGif::~HistoryGif() {
	clearClipReader();
}

float64 HistoryGif::dataProgress() const {
	return (_data->uploading() || _parent->data()->id > 0)
		? _data->progress()
		: 0;
}

bool HistoryGif::dataFinished() const {
	return (_parent->data()->id > 0)
		? (!_data->loading() && !_data->uploading())
		: false;
}

bool HistoryGif::dataLoaded() const {
	return (_parent->data()->id > 0) ? _data->loaded() : false;
}

bool HistoryGif::needInfoDisplay() const {
	return (_parent->data()->id < 0 || _parent->isUnderCursor());
}
