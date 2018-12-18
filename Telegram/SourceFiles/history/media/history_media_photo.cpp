/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_photo.h"

#include "layout.h"
#include "auth_session.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/media/history_media_common.h"
#include "ui/image/image.h"
#include "ui/grouped_layout.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "styles/style_history.h"

namespace {

using TextState = HistoryView::TextState;

} // namespace

HistoryPhoto::HistoryPhoto(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<PhotoData*> photo)
: HistoryFileMedia(parent, realParent)
, _data(photo)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	const auto fullId = realParent->fullId();
	setLinks(
		std::make_shared<PhotoOpenClickHandler>(_data, fullId),
		std::make_shared<PhotoSaveClickHandler>(_data, fullId),
		std::make_shared<PhotoCancelClickHandler>(_data, fullId));
	_caption = createCaption(realParent);
	create(realParent->fullId());
}

HistoryPhoto::HistoryPhoto(
	not_null<Element*> parent,
	not_null<PeerData*> chat,
	not_null<PhotoData*> photo,
	int width)
: HistoryFileMedia(parent, parent->data())
, _data(photo)
, _serviceWidth(width) {
	create(parent->data()->fullId(), chat);
}

void HistoryPhoto::create(FullMsgId contextId, PeerData *chat) {
	setLinks(
		std::make_shared<PhotoOpenClickHandler>(_data, contextId, chat),
		std::make_shared<PhotoSaveClickHandler>(_data, contextId, chat),
		std::make_shared<PhotoCancelClickHandler>(_data, contextId, chat));
	_data->thumb->load(contextId);
}

QSize HistoryPhoto::countOptimalSize() {
	if (_parent->media() != this) {
		_caption = Text();
	} else if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}

	auto maxWidth = 0;
	auto minHeight = 0;

	auto tw = ConvertScale(_data->full->width());
	auto th = ConvertScale(_data->full->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	if (th > st::maxMediaSize) {
		tw = (st::maxMediaSize * tw) / th;
		th = st::maxMediaSize;
	}

	if (_serviceWidth > 0) {
		return { _serviceWidth, _serviceWidth };
	}
	const auto minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	const auto maxActualWidth = qMax(tw, minWidth);
	maxWidth = qMax(maxActualWidth, th);
	minHeight = qMax(th, st::minPhotoSize);
	if (_parent->hasBubble() && !_caption.isEmpty()) {
		auto captionw = maxActualWidth - st::msgPadding.left() - st::msgPadding.right();
		minHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize HistoryPhoto::countCurrentSize(int newWidth) {
	int tw = ConvertScale(_data->full->width()), th = ConvertScale(_data->full->height());
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	if (th > st::maxMediaSize) {
		tw = (st::maxMediaSize * tw) / th;
		th = st::maxMediaSize;
	}

	_pixw = qMin(newWidth, maxWidth());
	_pixh = th;
	if (tw > _pixw) {
		_pixh = (_pixw * _pixh / tw);
	} else {
		_pixw = tw;
	}
	if (_pixh > newWidth) {
		_pixw = (_pixw * newWidth) / _pixh;
		_pixh = newWidth;
	}
	if (_pixw < 1) _pixw = 1;
	if (_pixh < 1) _pixh = 1;

	auto minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	newWidth = qMax(_pixw, minWidth);
	auto newHeight = qMax(_pixh, st::minPhotoSize);
	if (_parent->hasBubble() && !_caption.isEmpty()) {
		const auto captionw = newWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		newHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			newHeight += st::msgPadding.bottom();
		}
	}
	return { newWidth, newHeight };
}

void HistoryPhoto::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_realParent->fullId(), _parent->data());
	auto selected = (selection == FullSelection);
	auto loaded = _data->loaded();
	auto displayLoading = _data->displayLoading();

	auto inWebPage = (_parent->media() != this);
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	auto bubble = _parent->hasBubble();

	auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool radial = isRadialAnimation(ms);

	auto rthumb = rtlrect(paintx, painty, paintw, painth, width());
	if (_serviceWidth > 0) {
		const auto pix = loaded
			? _data->full->pixCircled(_realParent->fullId(), _pixw, _pixh)
			: _data->thumb->pixBlurredCircled(_realParent->fullId(), _pixw, _pixh);
		p.drawPixmap(rthumb.topLeft(), pix);
	} else {
		if (bubble) {
			if (!_caption.isEmpty()) {
				painth -= st::mediaCaptionSkip + _caption.countHeight(captionw);
				if (isBubbleBottom()) {
					painth -= st::msgPadding.bottom();
				}
				rthumb = rtlrect(paintx, painty, paintw, painth, width());
			}
		} else {
			App::roundShadow(p, 0, 0, paintw, painth, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
		}
		auto inWebPage = (_parent->media() != this);
		auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
		auto roundCorners = inWebPage ? RectPart::AllCorners : ((isBubbleTop() ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
			| ((isBubbleBottom() && _caption.isEmpty()) ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None));
		const auto pix = loaded
			? _data->full->pixSingle(_realParent->fullId(), _pixw, _pixh, paintw, painth, roundRadius, roundCorners)
			: _data->thumb->pixBlurredSingle(_realParent->fullId(), _pixw, _pixh, paintw, painth, roundRadius, roundCorners);
		p.drawPixmap(rthumb.topLeft(), pix);
		if (selected) {
			App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
		}
	}
	if (radial || (!loaded && !_data->loading())) {
		const auto radialOpacity = (radial && loaded && !_data->uploading())
			? _animation->radial.opacity() :
			1.;
		QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
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
		auto icon = ([radial, this, selected]() -> const style::icon* {
			if (radial || _data->loading()) {
				if (_data->uploading()
					|| !_data->full->location().isNull()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		})();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		p.setOpacity(1);
		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
		}
	}

	// date
	if (!_caption.isEmpty()) {
		auto outbg = _parent->hasOutLayout();
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), painty + painth + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	} else if (!inWebPage) {
		auto fullRight = paintx + paintw;
		auto fullBottom = painty + painth;
		if (needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, 2 * paintx + paintw, selected, InfoDisplayType::Image);
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

TextState HistoryPhoto::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
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
		painth -= st::mediaCaptionSkip;
	}
	if (QRect(paintx, painty, paintw, painth).contains(point)) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else if (_data->loaded()) {
			result.link = _openl;
		} else if (_data->loading()) {
			if (!_data->full->location().isNull()) {
				result.link = _cancell;
			}
		} else {
			result.link = _savel;
		}
	}
	if (_caption.isEmpty() && _parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = painty + painth;
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Image)) {
			result.cursor = CursorState::Date;
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}
	return result;
}

QSize HistoryPhoto::sizeForGrouping() const {
	const auto width = _data->full->width();
	const auto height = _data->full->height();
	return { std::max(width, 1), std::max(height, 1) };
}

void HistoryPhoto::drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms,
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	_data->automaticLoad(_realParent->fullId(), _parent->data());

	validateGroupedCache(geometry, corners, cacheKey, cache);

	const auto selected = (selection == FullSelection);
	const auto loaded = _data->loaded();
	const auto displayLoading = _data->displayLoading();
	const auto bubble = _parent->hasBubble();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	const auto radial = isRadialAnimation(ms);

	if (!bubble) {
//		App::roundShadow(p, 0, 0, paintw, painth, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}
	p.drawPixmap(geometry.topLeft(), *cache);
	if (selected) {
		const auto roundRadius = ImageRoundRadius::Large;
		App::complexOverlayRect(p, geometry, roundRadius, corners);
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
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			auto over = _animation->a_thumbOver.current();
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}

		p.setOpacity(backOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		const auto icon = [&]() -> const style::icon* {
			if (_data->waitingForAlbum()) {
				return &(selected ? st::historyFileThumbWaitingSelected : st::historyFileThumbWaiting);
			} else if (radial || _data->loading()) {
				if (_data->uploading() || !_data->full->location().isNull()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		}();
		const auto previous = [&]() -> const style::icon* {
			if (_data->waitingForAlbum()) {
				return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
			}
			return nullptr;
		}();
		p.setOpacity(backOpacity);
		if (icon) {
			if (previous && radialOpacity > 0. && radialOpacity < 1.) {
				PaintInterpolatedIcon(p, *icon, *previous, radialOpacity, inner);
			} else {
				icon->paintInCenter(p, inner);
			}
		}
		p.setOpacity(1);
		if (radial) {
			const auto line = st::historyGroupRadialLine;
			const auto rinner = inner.marginsRemoved({ line, line, line, line });
			const auto color = selected
				? st::historyFileThumbRadialFgSelected
				: st::historyFileThumbRadialFg;
			_animation->radial.draw(p, rinner, line, color);
		}
	}
}

TextState HistoryPhoto::getStateGrouped(
		const QRect &geometry,
		QPoint point,
		StateRequest request) const {
	if (!geometry.contains(point)) {
		return {};
	}
	return TextState(_parent, _data->uploading()
		? _cancell
		: _data->loaded()
		? _openl
		: _data->loading()
		? (_data->full->location().isNull()
			? ClickHandlerPtr()
			: _cancell)
		: _savel);
}

float64 HistoryPhoto::dataProgress() const {
	return _data->progress();
}

bool HistoryPhoto::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool HistoryPhoto::dataLoaded() const {
	return _data->loaded();
}

bool HistoryPhoto::needInfoDisplay() const {
	return (_parent->data()->id < 0 || _parent->isUnderCursor());
}

void HistoryPhoto::validateGroupedCache(
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	using Option = Images::Option;
	const auto loaded = _data->loaded();
	const auto loadLevel = loaded ? 2 : _data->thumb->loaded() ? 1 : 0;
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| (loaded ? Option::None : Option::Blurred)
		| ((corners & RectPart::TopLeft) ? Option::RoundedTopLeft : Option::None)
		| ((corners & RectPart::TopRight) ? Option::RoundedTopRight : Option::None)
		| ((corners & RectPart::BottomLeft) ? Option::RoundedBottomLeft : Option::None)
		| ((corners & RectPart::BottomRight) ? Option::RoundedBottomRight : Option::None);
	const auto key = (uint64(width) << 48)
		| (uint64(height) << 32)
		| (uint64(options) << 16)
		| (uint64(loadLevel));
	if (*cacheKey == key) {
		return;
	}

	const auto originalWidth = ConvertScale(_data->full->width());
	const auto originalHeight = ConvertScale(_data->full->height());
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ originalWidth, originalHeight },
		{ width, height });
	const auto pixWidth = pixSize.width() * cIntRetinaFactor();
	const auto pixHeight = pixSize.height() * cIntRetinaFactor();
	const auto &image = loaded ? _data->full : _data->thumb;

	*cacheKey = key;
	*cache = image->pixNoCache(_realParent->fullId(), pixWidth, pixHeight, options, width, height);
}

TextWithEntities HistoryPhoto::selectedText(TextSelection selection) const {
	return _caption.originalTextWithEntities(selection, ExpandLinksAll);
}

bool HistoryPhoto::needsBubble() const {
	if (!_caption.isEmpty()) {
		return true;
	}
	const auto item = _parent->data();
	if (item->toHistoryMessage()) {
		return item->viaBot()
			|| item->Has<HistoryMessageReply>()
			|| _parent->displayForwardedFrom()
			|| _parent->displayFromName();
	}
	return false;
}

bool HistoryPhoto::isReadyForOpen() const {
	return _data->loaded();
}

void HistoryPhoto::parentTextUpdated() {
	_caption = (_parent->media() == this)
		? createCaption(_parent->data())
		: Text();
	Auth().data().requestViewResize(_parent);
}
