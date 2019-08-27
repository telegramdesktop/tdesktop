/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_video.h"

#include "history/view/media/history_view_media_common.h"
#include "layout.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/image/image.h"
#include "ui/grouped_layout.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "styles/style_history.h"

namespace HistoryView {

Video::Video(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<DocumentData*> document)
: File(parent, realParent)
, _data(document)
, _thumbw(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	_caption = createCaption(realParent);

	setDocumentLinks(_data, realParent);

	setStatusSize(FileStatusSizeReady);
	_downloadSize = formatSizeText(_data->size);

	_data->loadThumbnail(realParent->fullId());
}

QSize Video::sizeForAspectRatio() const {
	// We use size only for aspect ratio and we want to have it
	// as close to the thumbnail as possible.
	//if (!_data->dimensions.isEmpty()) {
	//	return _data->dimensions;
	//}
	if (const auto thumb = _data->thumbnail()) {
		if (!thumb->size().isEmpty()) {
			return thumb->size();
		}
	}
	return { 1, 1 };
}

QSize Video::countOptimalDimensions() const {
	const auto desired = ConvertScale(_data->dimensions);
	const auto size = desired.isEmpty() ? sizeForAspectRatio() : desired;
	auto tw = size.width();
	auto th = size.height();
	if (!tw || !th) {
		tw = th = 1;
	} else if (tw >= th && tw > st::maxMediaSize) {
		th = qRound((st::maxMediaSize / float64(tw)) * th);
		tw = st::maxMediaSize;
	} else if (tw < th && th > st::maxMediaSize) {
		tw = qRound((st::maxMediaSize / float64(th)) * tw);
		th = st::maxMediaSize;
	} else if ((tw < st::msgVideoSize.width())
		&& (tw * st::msgVideoSize.height()
			>= th * st::msgVideoSize.width())) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else if ((th < st::msgVideoSize.height())
		&& (tw * st::msgVideoSize.height()
			< th * st::msgVideoSize.width())) {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}
	return QSize(tw, th);
}

QSize Video::countOptimalSize() {
	if (_parent->media() != this) {
		_caption = Ui::Text::String();
	} else if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}

	const auto size = countOptimalDimensions();
	const auto tw = size.width();
	const auto th = size.height();
	_thumbw = qMax(tw, 1);
	_thumbh = qMax(th, 1);

	auto minWidth = qMax(st::minVideoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, documentMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	auto maxWidth = qMax(_thumbw, minWidth);
	auto minHeight = qMax(th, st::minVideoSize);
	if (_parent->hasBubble() && !_caption.isEmpty()) {
		const auto captionw = maxWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		minHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize Video::countCurrentSize(int newWidth) {
	const auto size = countOptimalDimensions();
	auto tw = size.width();
	auto th = size.height();
	if (newWidth < tw) {
		th = qRound((newWidth / float64(tw)) * th);
		tw = newWidth;
	}

	_thumbw = qMax(tw, 1);
	_thumbh = qMax(th, 1);
	auto minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, documentMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	newWidth = qMax(_thumbw, minWidth);
	auto newHeight = qMax(th, st::minPhotoSize);
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

bool Video::downloadInCorner() const {
	return _data->canBeStreamed()
		&& !_data->inappPlaybackFailed()
		&& IsServerMsgId(_parent->data()->id);
}

void Video::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_realParent->fullId(), _parent->data());
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	bool selected = (selection == FullSelection);

	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();
	const auto cornerDownload = downloadInCorner();

	int captionw = paintw - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	updateStatusText();
	const auto radial = isRadialAnimation();

	if (bubble) {
		if (!_caption.isEmpty()) {
			painth -= st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				painth -= st::msgPadding.bottom();
			}
		}
	} else {
		App::roundShadow(p, 0, 0, paintw, painth, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	auto inWebPage = (_parent->media() != this);
	auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
	auto roundCorners = inWebPage ? RectPart::AllCorners : ((isBubbleTop() ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
		| ((isBubbleBottom() && _caption.isEmpty()) ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None));
	QRect rthumb(rtlrect(paintx, painty, paintw, painth, width()));

	const auto good = _data->goodThumbnail();
	if (good && good->loaded()) {
		p.drawPixmap(rthumb.topLeft(), good->pixSingle({}, _thumbw, _thumbh, paintw, painth, roundRadius, roundCorners));
	} else {
		if (good) {
			good->load({});
		}
		const auto normal = _data->thumbnail();
		if (normal && normal->loaded()) {
			p.drawPixmap(rthumb.topLeft(), normal->pixSingle(_realParent->fullId(), _thumbw, _thumbh, paintw, painth, roundRadius, roundCorners));
		} else if (const auto blurred = _data->thumbnailInline()) {
			p.drawPixmap(rthumb.topLeft(), blurred->pixBlurredSingle(_realParent->fullId(), _thumbw, _thumbh, paintw, painth, roundRadius, roundCorners));
		} else {
			const auto roundTop = (roundCorners & RectPart::TopLeft);
			const auto roundBottom = (roundCorners & RectPart::BottomLeft);
			const auto margin = inWebPage
				? st::buttonRadius
				: st::historyMessageRadius;
			const auto parts = roundCorners
				| RectPart::NoTopBottom
				| (roundTop ? RectPart::Top : RectPart::None)
				| (roundBottom ? RectPart::Bottom : RectPart::None);
			App::roundRect(p, rthumb.marginsAdded({ 0, roundTop ? 0 : margin, 0, roundBottom ? 0 : margin }), st::imageBg, roundRadius, parts);
		}
	}
	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
	p.setPen(Qt::NoPen);
	if (selected) {
		p.setBrush(st::msgDateImgBgSelected);
	} else if (isThumbAnimation()) {
		auto over = _animation->a_thumbOver.value(1.);
		p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
	} else {
		bool over = ClickHandler::showAsActive((_data->loading() || _data->uploading()) ? _cancell : _savel);
		p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
	}

	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}

	const auto icon = [&]() -> const style::icon * {
		if (!cornerDownload && (_data->loading() || _data->uploading())) {
			return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
		} else if (!IsServerMsgId(_parent->data()->id)) {
			return nullptr;
		} else if (loaded || _data->canBePlayed()) {
			return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
		}
		return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
	}();
	if (icon) {
		icon->paintInCenter(p, inner);
	}
	if (radial && !cornerDownload) {
		QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
		_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
	}

	drawCornerStatus(p, selected);

	// date
	if (!_caption.isEmpty()) {
		auto outbg = _parent->hasOutLayout();
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), painty + painth + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	} else if (_parent->media() == this) {
		auto fullRight = paintx + paintw, fullBottom = painty + painth;
		_parent->drawInfo(p, fullRight, fullBottom, 2 * paintx + paintw, selected, InfoDisplayType::Image);
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

void Video::drawCornerStatus(Painter &p, bool selected) const {
	const auto padding = st::msgDateImgPadding;
	const auto radial = _animation && _animation->radial.animating();
	const auto cornerDownload = downloadInCorner() && !_data->loaded() && !_data->loadedInMediaCache();
	const auto addWidth = cornerDownload ? (st::historyVideoDownloadSize + 2 * padding.y()) : 0;
	const auto downloadWidth = cornerDownload ? st::normalFont->width(_downloadSize) : 0;
	const auto statusW = std::max(downloadWidth, st::normalFont->width(_statusText)) + 2 * padding.x() + addWidth;
	const auto statusH = cornerDownload ? (st::historyVideoDownloadSize + 2 * padding.y()) : (st::normalFont->height + 2 * padding.y());
	const auto statusX = st::msgDateImgDelta + padding.x();
	const auto statusY = st::msgDateImgDelta + padding.y();
	const auto around = rtlrect(statusX - padding.x(), statusY - padding.y(), statusW, statusH, width());
	const auto statusTextTop = statusY + (cornerDownload ? (((statusH - 2 * st::normalFont->height) / 3)  - padding.y()) : 0);
	App::roundRect(p, around, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	p.setFont(st::normalFont);
	p.setPen(st::msgDateImgFg);
	p.drawTextLeft(statusX + addWidth, statusTextTop, width(), _statusText, statusW - 2 * padding.x());
	if (cornerDownload) {
		const auto downloadTextTop = statusY + st::normalFont->height + (2 * (statusH - 2 * st::normalFont->height) / 3)  - padding.y();
		p.drawTextLeft(statusX + addWidth, downloadTextTop, width(), _downloadSize, statusW - 2 * padding.x());
		const auto inner = QRect(statusX + padding.y() - padding.x(), statusY, st::historyVideoDownloadSize, st::historyVideoDownloadSize);
		const auto icon = [&]() -> const style::icon * {
			if (_data->loading()) {
				return &(selected ? st::historyVideoCancelSelected : st::historyVideoCancel);
			}
			return &(selected ? st::historyVideoDownloadSelected : st::historyVideoDownload);
		}();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::historyVideoRadialLine, st::historyVideoRadialLine, st::historyVideoRadialLine, st::historyVideoRadialLine)));
			_animation->radial.draw(p, rinner, st::historyVideoRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
		}
	}
}

TextState Video::cornerStatusTextState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(_parent);
	if (!downloadInCorner() || _data->loaded()) {
		return result;
	}
	const auto padding = st::msgDateImgPadding;
	const auto addWidth = st::historyVideoDownloadSize + 2 * padding.y() - padding.x();
	const auto statusX = st::msgDateImgDelta + padding.x(), statusY = st::msgDateImgDelta + padding.y();
	const auto inner = QRect(statusX + padding.y() - padding.x(), statusY, st::historyVideoDownloadSize, st::historyVideoDownloadSize);
	if (inner.contains(point)) {
		result.link = _data->loading() ? _cancell : _savel;
	}
	return result;
}

TextState Video::textState(QPoint point, StateRequest request) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return {};
	}

	auto result = TextState(_parent);

	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();

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
		}
		painth -= st::mediaCaptionSkip;
	}
	if (const auto state = cornerStatusTextState(point, request); state.link) {
		return state;
	}
	if (QRect(paintx, painty, paintw, painth).contains(point)) {
		if (!downloadInCorner() && (_data->loading() || _data->uploading())) {
			result.link = _cancell;
		} else if (!IsServerMsgId(_parent->data()->id)) {
		} else if (_data->loaded() || _data->canBePlayed()) {
			result.link = _openl;
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

QSize Video::sizeForGrouping() const {
	return sizeForAspectRatio();
}

void Video::drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		crl::time ms,
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
	const auto radial = isRadialAnimation();

	if (!bubble) {
//		App::roundShadow(p, 0, 0, paintw, painth, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}
	p.drawPixmap(geometry.topLeft(), *cache);
	if (selected) {
		const auto roundRadius = ImageRoundRadius::Large;
		App::complexOverlayRect(p, geometry, roundRadius, corners);
	}

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
	} else if (isThumbAnimation()) {
		auto over = _animation->a_thumbOver.value(1.);
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

	auto icon = [&]() -> const style::icon * {
		if (_data->waitingForAlbum()) {
			return &(selected ? st::historyFileThumbWaitingSelected : st::historyFileThumbWaiting);
		} else if (_data->loading() || _data->uploading()) {
			return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
		} else if (!IsServerMsgId(_realParent->id)) {
			return nullptr;
		} else if (loaded || _data->canBePlayed()) {
			return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
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

TextState Video::getStateGrouped(
		const QRect &geometry,
		QPoint point,
		StateRequest request) const {
	if (!geometry.contains(point)) {
		return {};
	}
	return TextState(_parent, (_data->loading() || _data->uploading())
		? _cancell
		: !IsServerMsgId(_realParent->id)
		? nullptr
		: (_data->loaded() || _data->canBePlayed())
		? _openl
		: _savel);
}

bool Video::uploading() const {
	return _data->uploading();
}

float64 Video::dataProgress() const {
	return _data->progress();
}

bool Video::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool Video::dataLoaded() const {
	return _data->loaded();
}

void Video::validateGroupedCache(
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	using Option = Images::Option;
	const auto good = _data->goodThumbnail();
	const auto useGood = (good && good->loaded());
	const auto thumb = _data->thumbnail();
	const auto useThumb = (thumb && thumb->loaded());
	const auto image = useGood
		? good
		: useThumb
		? thumb
		: _data->thumbnailInline();
	if (good && !useGood) {
		good->load({});
	}

	const auto loadLevel = useGood ? 3 : useThumb ? 2 : image ? 1 : 0;
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| (useGood ? Option(0) : Option::Blurred)
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

	const auto original = sizeForAspectRatio();
	const auto originalWidth = ConvertScale(original.width());
	const auto originalHeight = ConvertScale(original.height());
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ originalWidth, originalHeight },
		{ width, height });
	const auto pixWidth = pixSize.width() * cIntRetinaFactor();
	const auto pixHeight = pixSize.height() * cIntRetinaFactor();

	*cacheKey = key;
	*cache = (image ? image : Image::BlankMedia().get())->pixNoCache(
		_realParent->fullId(),
		pixWidth,
		pixHeight,
		options,
		width,
		height);
}

void Video::setStatusSize(int newSize) const {
	File::setStatusSize(newSize, _data->size, _data->getDuration(), 0);
}

TextForMimeData Video::selectedText(TextSelection selection) const {
	return _caption.toTextForMimeData(selection);
}

bool Video::needsBubble() const {
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

void Video::parentTextUpdated() {
	_caption = (_parent->media() == this)
		? createCaption(_parent->data())
		: Ui::Text::String();
	history()->owner().requestViewResize(_parent);
}

void Video::updateStatusText() const {
	auto showPause = false;
	auto statusSize = 0;
	auto realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (!downloadInCorner() && _data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->canBePlayed()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

} // namespace HistoryView
