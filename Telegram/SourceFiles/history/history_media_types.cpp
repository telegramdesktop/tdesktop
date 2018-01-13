/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_media_types.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "layout.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "storage/storage_shared_media.h"
#include "media/media_audio.h"
#include "media/media_clip_reader.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_clip_playback.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "core/click_handler_types.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_location_manager.h"
#include "history/history_message.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "styles/style_history.h"
#include "calls/calls_instance.h"
#include "ui/empty_userpic.h"
#include "ui/grouped_layout.h"
#include "ui/text_options.h"
#include "data/data_session.h"

namespace {

constexpr auto kMaxGifForwardedBarLines = 4;
constexpr auto kMaxOriginalEntryLines = 8192;

bool needReSetInlineResultDocument(const MTPMessageMedia &media, DocumentData *existing) {
	if (media.type() == mtpc_messageMediaDocument) {
		auto &mediaDocument = media.c_messageMediaDocument();
		if (mediaDocument.has_document() && !mediaDocument.has_ttl_seconds()) {
			if (auto document = App::feedDocument(mediaDocument.vdocument)) {
				if (document == existing) {
					return false;
				} else {
					document->collectLocalData(existing);
				}
			}
		} else {
			LOG(("API Error: Got MTPMessageMediaDocument without document or with ttl_seconds in needReSetInlineResultDocument()"));
		}
	}
	return true;
}

int documentMaxStatusWidth(DocumentData *document) {
	auto result = st::normalFont->width(formatDownloadText(document->size, document->size));
	if (const auto song = document->song()) {
		accumulate_max(result, st::normalFont->width(formatPlayedText(song->duration, song->duration)));
		accumulate_max(result, st::normalFont->width(formatDurationAndSizeText(song->duration, document->size)));
	} else if (const auto voice = document->voice()) {
		accumulate_max(result, st::normalFont->width(formatPlayedText(voice->duration, voice->duration)));
		accumulate_max(result, st::normalFont->width(formatDurationAndSizeText(voice->duration, document->size)));
	} else if (document->isVideoFile()) {
		accumulate_max(result, st::normalFont->width(formatDurationAndSizeText(document->duration(), document->size)));
	} else {
		accumulate_max(result, st::normalFont->width(formatSizeText(document->size)));
	}
	return result;
}

int gifMaxStatusWidth(DocumentData *document) {
	auto result = st::normalFont->width(formatDownloadText(document->size, document->size));
	accumulate_max(result, st::normalFont->width(formatGifAndSizeText(document->size)));
	return result;
}

} // namespace

TextWithEntities WithCaptionSelectedText(
		const QString &attachType,
		const Text &caption,
		TextSelection selection) {
	if (selection != FullSelection) {
		return caption.originalTextWithEntities(selection, ExpandLinksAll);
	}

	TextWithEntities result, original;
	if (!caption.isEmpty()) {
		original = caption.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
	}
	result.text.reserve(5 + attachType.size() + original.text.size());
	result.text.append(qstr("[ ")).append(attachType).append(qstr(" ]"));
	if (!caption.isEmpty()) {
		result.text.append(qstr("\n"));
		TextUtilities::Append(result, std::move(original));
	}
	return result;
}

QString WithCaptionNotificationText(
		const QString &attachType,
		const Text &caption) {
	if (caption.isEmpty()) {
		return attachType;
	}

	auto captionText = caption.originalText();
	auto attachTypeWrapped = lng_dialogs_text_media_wrapped(lt_media, attachType);
	return lng_dialogs_text_media(lt_media_part, attachTypeWrapped, lt_caption, captionText);
}

QString WithCaptionDialogsText(
		const QString &attachType,
		const Text &caption) {
	if (caption.isEmpty()) {
		return textcmdLink(1, TextUtilities::Clean(attachType));
	}

	auto captionText = TextUtilities::Clean(caption.originalText());
	auto attachTypeWrapped = textcmdLink(1, lng_dialogs_text_media_wrapped(lt_media, TextUtilities::Clean(attachType)));
	return lng_dialogs_text_media(lt_media_part, attachTypeWrapped, lt_caption, captionText);
}

void HistoryFileMedia::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (p == _savel || p == _cancell) {
		if (active && !dataLoaded()) {
			ensureAnimation();
			_animation->a_thumbOver.start([this] { thumbAnimationCallback(); }, 0., 1., st::msgFileOverDuration);
		} else if (!active && _animation && !dataLoaded()) {
			_animation->a_thumbOver.start([this] { thumbAnimationCallback(); }, 1., 0., st::msgFileOverDuration);
		}
	}
}

void HistoryFileMedia::thumbAnimationCallback() {
	Auth().data().requestItemRepaint(_parent);
}

void HistoryFileMedia::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	Auth().data().requestItemRepaint(_parent);
}

void HistoryFileMedia::setLinks(
		FileClickHandlerPtr &&openl,
		FileClickHandlerPtr &&savel,
		FileClickHandlerPtr &&cancell) {
	_openl = std::move(openl);
	_savel = std::move(savel);
	_cancell = std::move(cancell);
}

void HistoryFileMedia::refreshParentId(not_null<HistoryItem*> realParent) {
	const auto contextId = realParent->fullId();
	_openl->setMessageId(contextId);
	_savel->setMessageId(contextId);
	_cancell->setMessageId(contextId);
}

void HistoryFileMedia::setStatusSize(int newSize, int fullSize, int duration, qint64 realDuration) const {
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

void HistoryFileMedia::step_radial(TimeMs ms, bool timer) {
	if (timer) {
		Auth().data().requestItemRepaint(_parent);
	} else {
		_animation->radial.update(dataProgress(), dataFinished(), ms);
		if (!_animation->radial.animating()) {
			checkAnimationFinished();
		}
	}
}

void HistoryFileMedia::ensureAnimation() const {
	if (!_animation) {
		_animation = std::make_unique<AnimationData>(animation(const_cast<HistoryFileMedia*>(this), &HistoryFileMedia::step_radial));
	}
}

void HistoryFileMedia::checkAnimationFinished() const {
	if (_animation && !_animation->a_thumbOver.animating() && !_animation->radial.animating()) {
		if (dataLoaded()) {
			_animation.reset();
		}
	}
}
void HistoryFileMedia::setDocumentLinks(
		not_null<DocumentData*> document,
		not_null<HistoryItem*> realParent,
		bool inlinegif) {
	FileClickHandlerPtr open, save;
	const auto context = realParent->fullId();
	if (inlinegif) {
		open = std::make_shared<GifOpenClickHandler>(document, context);
	} else {
		open = std::make_shared<DocumentOpenClickHandler>(document, context);
	}
	if (inlinegif) {
		save = std::make_shared<GifOpenClickHandler>(document, context);
	} else if (document->isVoiceMessage()) {
		save = std::make_shared<DocumentOpenClickHandler>(document, context);
	} else {
		save = std::make_shared<DocumentSaveClickHandler>(document, context);
	}
	setLinks(
		std::move(open),
		std::move(save),
		std::make_shared<DocumentCancelClickHandler>(document, context));
}

HistoryFileMedia::~HistoryFileMedia() = default;

HistoryPhoto::HistoryPhoto(
	not_null<HistoryItem*> parent,
	not_null<PhotoData*> photo,
	const QString &caption)
: HistoryFileMedia(parent)
, _data(photo)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	const auto fullId = parent->fullId();
	setLinks(
		std::make_shared<PhotoOpenClickHandler>(_data, fullId),
		std::make_shared<PhotoSaveClickHandler>(_data, fullId),
		std::make_shared<PhotoCancelClickHandler>(_data, fullId));
	if (!caption.isEmpty()) {
		_caption.setText(
			st::messageTextStyle,
			caption + _parent->skipBlock(),
			Ui::ItemTextNoMonoOptions(_parent));
	}
	init();
}

HistoryPhoto::HistoryPhoto(
	not_null<HistoryItem*> parent,
	not_null<PeerData*> chat,
	not_null<PhotoData*> photo,
	int width)
: HistoryFileMedia(parent)
, _data(photo)
, _serviceWidth(width) {
	const auto fullId = parent->fullId();
	setLinks(
		std::make_shared<PhotoOpenClickHandler>(_data, fullId, chat),
		std::make_shared<PhotoSaveClickHandler>(_data, fullId, chat),
		std::make_shared<PhotoCancelClickHandler>(_data, fullId, chat));
	init();
}

HistoryPhoto::HistoryPhoto(
	not_null<HistoryItem*> parent,
	not_null<PeerData*> chat,
	const MTPDphoto &photo,
	int width)
: HistoryPhoto(parent, chat, App::feedPhoto(photo), width) {
}

HistoryPhoto::HistoryPhoto(
	not_null<HistoryItem*> parent,
	not_null<HistoryItem*> realParent,
	const HistoryPhoto &other)
: HistoryFileMedia(parent)
, _data(other._data)
, _pixw(other._pixw)
, _pixh(other._pixh)
, _caption(other._caption) {
	const auto fullId = realParent->fullId();
	setLinks(
		std::make_shared<PhotoOpenClickHandler>(_data, fullId),
		std::make_shared<PhotoSaveClickHandler>(_data, fullId),
		std::make_shared<PhotoCancelClickHandler>(_data, fullId));

	init();
}

void HistoryPhoto::init() {
	_data->thumb->load();
}

QSize HistoryPhoto::countOptimalSize() {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	auto maxWidth = 0;
	auto minHeight = 0;

	auto tw = convertScale(_data->full->width());
	auto th = convertScale(_data->full->height());
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

	if (!_parent->toHistoryMessage()) {
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
	int tw = convertScale(_data->full->width()), th = convertScale(_data->full->height());
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

	_data->automaticLoad(_parent);
	auto selected = (selection == FullSelection);
	auto loaded = _data->loaded();
	auto displayLoading = _data->displayLoading();

	auto notChild = (_parent->getMedia() == this);
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
	if (_parent->toHistoryMessage()) {
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
		auto inWebPage = (_parent->getMedia() != this);
		auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
		auto roundCorners = inWebPage ? RectPart::AllCorners : ((isBubbleTop() ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
			| ((isBubbleBottom() && _caption.isEmpty()) ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None));
		const auto pix = loaded
			? _data->full->pixSingle(_pixw, _pixh, paintw, painth, roundRadius, roundCorners)
			: _data->thumb->pixBlurredSingle(_pixw, _pixh, paintw, painth, roundRadius, roundCorners);
		p.drawPixmap(rthumb.topLeft(), pix);
		if (selected) {
			App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
		}
	} else {
		const auto pix = loaded
			? _data->full->pixCircled(_pixw, _pixh)
			: _data->thumb->pixBlurredCircled(_pixw, _pixh);
		p.drawPixmap(rthumb.topLeft(), pix);
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
				auto delayed = _data->full->toDelayedStorageImage();
				if (!delayed || !delayed->location().isNull()) {
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
	} else if (notChild) {
		auto fullRight = paintx + paintw;
		auto fullBottom = painty + painth;
		if (needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, 2 * paintx + paintw, selected, InfoDisplayOverImage);
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

HistoryTextState HistoryPhoto::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);

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
			result = HistoryTextState(_parent, _caption.getState(
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
			auto delayed = _data->full->toDelayedStorageImage();
			if (!delayed || !delayed->location().isNull()) {
				result.link = _cancell;
			}
		} else {
			result.link = _savel;
		}
	}
	if (_caption.isEmpty() && _parent->getMedia() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = painty + painth;
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayOverImage)) {
			result.cursor = HistoryInDateCursorState;
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
	const auto width = convertScale(_data->full->width());
	const auto height = convertScale(_data->full->height());
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
	_data->automaticLoad(_parent);

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
		const auto radialOpacity = (radial && loaded && !_data->uploading())
			? _animation->radial.opacity()
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

		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		auto icon = [&]() -> const style::icon* {
			if (_data->waitingForAlbum()) {
				return &(selected ? st::historyFileThumbWaitingSelected : st::historyFileThumbWaiting);
			} else if (radial || _data->loading()) {
				auto delayed = _data->full->toDelayedStorageImage();
				if (!delayed || !delayed->location().isNull()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		}();
		if (icon) {
			icon->paintInCenter(p, inner);
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

HistoryTextState HistoryPhoto::getStateGrouped(
		const QRect &geometry,
		QPoint point,
		HistoryStateRequest request) const {
	if (!geometry.contains(point)) {
		return {};
	}
	const auto delayed = _data->full->toDelayedStorageImage();
	return HistoryTextState(_parent, _data->uploading()
		? _cancell
		: _data->loaded()
		? _openl
		: _data->loading()
		? ((!delayed || !delayed->location().isNull())
			? _cancell
			: ClickHandlerPtr())
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
	return (_data->uploading() || _parent->isUnderCursor());
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

	const auto originalWidth = convertScale(_data->full->width());
	const auto originalHeight = convertScale(_data->full->height());
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ originalWidth, originalHeight },
		{ width, height });
	const auto pixWidth = pixSize.width() * cIntRetinaFactor();
	const auto pixHeight = pixSize.height() * cIntRetinaFactor();
	const auto &image = loaded ? _data->full : _data->thumb;

	*cacheKey = key;
	*cache = image->pixNoCache(pixWidth, pixHeight, options, width, height);
}

void HistoryPhoto::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaPhoto) {
		auto &mediaPhoto = media.c_messageMediaPhoto();
		if (!mediaPhoto.has_photo() || mediaPhoto.has_ttl_seconds()) {
			LOG(("Api Error: Got MTPMessageMediaPhoto without photo or with ttl_seconds in updateSentMedia()"));
			return;
		}
		auto &photo = mediaPhoto.vphoto;
		App::feedPhoto(photo, _data);

		if (photo.type() == mtpc_photo) {
			auto &sizes = photo.c_photo().vsizes.v;
			auto max = 0;
			const MTPDfileLocation *maxLocation = 0;
			for (auto i = 0, l = int(sizes.size()); i != l; ++i) {
				char size = 0;
				const MTPFileLocation *loc = 0;
				switch (sizes.at(i).type()) {
				case mtpc_photoSize: {
					auto &s = sizes.at(i).c_photoSize().vtype.v;
					loc = &sizes.at(i).c_photoSize().vlocation;
					if (s.size()) size = s[0];
				} break;

				case mtpc_photoCachedSize: {
					auto &s = sizes.at(i).c_photoCachedSize().vtype.v;
					loc = &sizes.at(i).c_photoCachedSize().vlocation;
					if (s.size()) size = s[0];
				} break;
				}
				if (!loc || loc->type() != mtpc_fileLocation) continue;
				if (size == 's') {
					Local::writeImage(storageKey(loc->c_fileLocation()), _data->thumb);
				} else if (size == 'm') {
					Local::writeImage(storageKey(loc->c_fileLocation()), _data->medium);
				} else if (size == 'x' && max < 1) {
					max = 1;
					maxLocation = &loc->c_fileLocation();
				} else if (size == 'y' && max < 2) {
					max = 2;
					maxLocation = &loc->c_fileLocation();
					//} else if (size == 'w' && max < 3) {
					//	max = 3;
					//	maxLocation = &loc->c_fileLocation();
				}
			}
			if (maxLocation) {
				Local::writeImage(storageKey(*maxLocation), _data->full);
			}
		}
	}
}

bool HistoryPhoto::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaPhoto) {
		auto &photo = media.c_messageMediaPhoto();
		if (photo.has_photo() && !photo.has_ttl_seconds()) {
			if (auto existing = App::feedPhoto(photo.vphoto)) {
				if (existing == _data) {
					return false;
				} else {
					// collect data
				}
			}
		} else {
			LOG(("API Error: Got MTPMessageMediaPhoto without photo or with ttl_seconds in needReSetInlineResultMedia()"));
		}
	}
	return false;
}

void HistoryPhoto::attachToParent() {
	App::regPhotoItem(_data, _parent);
}

void HistoryPhoto::detachFromParent() {
	App::unregPhotoItem(_data, _parent);
}

QString HistoryPhoto::notificationText() const {
	return WithCaptionNotificationText(lang(lng_in_dlg_photo), _caption);
}

QString HistoryPhoto::inDialogsText() const {
	return WithCaptionDialogsText(lang(lng_in_dlg_photo), _caption);
}

TextWithEntities HistoryPhoto::selectedText(TextSelection selection) const {
	return WithCaptionSelectedText(
		lang(lng_in_dlg_photo),
		_caption,
		selection);
}

bool HistoryPhoto::needsBubble() const {
	if (!_caption.isEmpty()) {
		return true;
	}
	if (auto message = _parent->toHistoryMessage()) {
		return message->viaBot()
			|| message->Has<HistoryMessageReply>()
			|| message->displayForwardedFrom()
//			|| message->displayFromName() // #TODO media views
;
	}
	return false;
}

Storage::SharedMediaTypesMask HistoryPhoto::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_parent->toHistoryMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::Photo)
			.added(Type::PhotoVideo);
	}
	return Type::ChatPhoto;
}

ImagePtr HistoryPhoto::replyPreview() {
	return _data->makeReplyPreview();
}

HistoryVideo::HistoryVideo(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document,
	const QString &caption)
: HistoryFileMedia(parent)
, _data(document)
, _thumbw(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	if (!caption.isEmpty()) {
		_caption.setText(
			st::messageTextStyle,
			caption + _parent->skipBlock(),
			Ui::ItemTextNoMonoOptions(_parent));
	}

	setDocumentLinks(_data, parent);

	setStatusSize(FileStatusSizeReady);

	_data->thumb->load();
}

HistoryVideo::HistoryVideo(
	not_null<HistoryItem*> parent,
	not_null<HistoryItem*> realParent,
	const HistoryVideo &other)
: HistoryFileMedia(parent)
, _data(other._data)
, _thumbw(other._thumbw)
, _caption(other._caption) {
	setDocumentLinks(_data, realParent);

	setStatusSize(other._statusSize);
}

QSize HistoryVideo::countOptimalSize() {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	auto tw = convertScale(_data->thumb->width());
	auto th = convertScale(_data->thumb->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw * st::msgVideoSize.height() > th * st::msgVideoSize.width()) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}

	_thumbw = qMax(tw, 1);
	auto minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, documentMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	auto maxWidth = qMax(_thumbw, minWidth);
	auto minHeight = qMax(th, st::minPhotoSize);
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

QSize HistoryVideo::countCurrentSize(int newWidth) {
	int tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw * st::msgVideoSize.height() > th * st::msgVideoSize.width()) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}

	if (newWidth < tw) {
		th = qRound((newWidth / float64(tw)) * th);
		tw = newWidth;
	}

	_thumbw = qMax(tw, 1);
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

void HistoryVideo::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	bool selected = (selection == FullSelection);

	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();

	int captionw = paintw - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	updateStatusText();
	bool radial = isRadialAnimation(ms);

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

	auto inWebPage = (_parent->getMedia() != this);
	auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
	auto roundCorners = inWebPage ? RectPart::AllCorners : ((isBubbleTop() ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
		| ((isBubbleBottom() && _caption.isEmpty()) ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None));
	QRect rthumb(rtlrect(paintx, painty, paintw, painth, width()));
	p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, 0, paintw, painth, roundRadius, roundCorners));
	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
	p.setPen(Qt::NoPen);
	if (selected) {
		p.setBrush(st::msgDateImgBgSelected);
	} else if (isThumbAnimation(ms)) {
		auto over = _animation->a_thumbOver.current();
		p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
	} else {
		bool over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
		p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
	}

	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}

	if (!selected && _animation) {
		p.setOpacity(1);
	}

	auto icon = ([this, radial, selected, loaded]() -> const style::icon * {
		if (loaded && !radial) {
			return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
		} else if (radial || _data->loading()) {
			if (_parent->id > 0 || _data->uploading()) {
				return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
			}
			return nullptr;
		}
		return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
	})();
	if (icon) {
		icon->paintInCenter(p, inner);
	}
	if (radial) {
		QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
		_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
	}

	auto statusX = paintx + st::msgDateImgDelta + st::msgDateImgPadding.x(), statusY = painty + st::msgDateImgDelta + st::msgDateImgPadding.y();
	auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
	auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
	App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	p.setFont(st::normalFont);
	p.setPen(st::msgDateImgFg);
	p.drawTextLeft(statusX, statusY, width(), _statusText, statusW - 2 * st::msgDateImgPadding.x());

	// date
	if (!_caption.isEmpty()) {
		auto outbg = _parent->hasOutLayout();
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), painty + painth + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	} else if (_parent->getMedia() == this) {
		auto fullRight = paintx + paintw, fullBottom = painty + painth;
		_parent->drawInfo(p, fullRight, fullBottom, 2 * paintx + paintw, selected, InfoDisplayOverImage);
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

HistoryTextState HistoryVideo::getState(QPoint point, HistoryStateRequest request) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return {};
	}

	auto result = HistoryTextState(_parent);
	bool loaded = _data->loaded();

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
			result = HistoryTextState(_parent, _caption.getState(
				point - QPoint(st::msgPadding.left(), painth),
				captionw,
				request.forText()));
		}
		painth -= st::mediaCaptionSkip;
	}
	if (QRect(paintx, painty, paintw, painth).contains(point)) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else {
			result.link = loaded ? _openl : (_data->loading() ? _cancell : _savel);
		}
	}
	if (_caption.isEmpty() && _parent->getMedia() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = painty + painth;
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayOverImage)) {
			result.cursor = HistoryInDateCursorState;
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

QSize HistoryVideo::sizeForGrouping() const {
	const auto width = convertScale(_data->thumb->width());
	const auto height = convertScale(_data->thumb->height());
	return { std::max(width, 1), std::max(height, 1) };
}

void HistoryVideo::drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms,
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	_data->automaticLoad(_parent);

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

	const auto radialOpacity = (radial && loaded && !_data->uploading())
		? _animation->radial.opacity()
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

	p.setOpacity(radialOpacity * p.opacity());

	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}

	p.setOpacity(radialOpacity);
	auto icon = [&]() -> const style::icon * {
		if (_data->waitingForAlbum()) {
			return &(selected ? st::historyFileThumbWaitingSelected : st::historyFileThumbWaiting);
		} else if (loaded && !radial) {
			return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
		} else if (radial || _data->loading()) {
			if (_parent->id > 0 || _data->uploading()) {
				return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
			}
			return nullptr;
		}
		return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
	}();
	if (icon) {
		icon->paintInCenter(p, inner);
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

HistoryTextState HistoryVideo::getStateGrouped(
		const QRect &geometry,
		QPoint point,
		HistoryStateRequest request) const {
	if (!geometry.contains(point)) {
		return {};
	}
	return HistoryTextState(_parent, _data->uploading()
		? _cancell
		: _data->loaded()
		? _openl
		: _data->loading()
		? _cancell
		: _savel);
}

float64 HistoryVideo::dataProgress() const {
	return _data->progress();
}

bool HistoryVideo::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool HistoryVideo::dataLoaded() const {
	return _data->loaded();
}

void HistoryVideo::validateGroupedCache(
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	using Option = Images::Option;
	const auto loaded = _data->thumb->loaded();
	const auto loadLevel = loaded ? 1 : 0;
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| Option::Blurred
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

	const auto originalWidth = convertScale(_data->thumb->width());
	const auto originalHeight = convertScale(_data->thumb->height());
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ originalWidth, originalHeight },
		{ width, height });
	const auto pixWidth = pixSize.width() * cIntRetinaFactor();
	const auto pixHeight = pixSize.height() * cIntRetinaFactor();
	const auto &image = _data->thumb;

	*cacheKey = key;
	*cache = image->pixNoCache(pixWidth, pixHeight, options, width, height);
}

void HistoryVideo::setStatusSize(int newSize) const {
	HistoryFileMedia::setStatusSize(newSize, _data->size, _data->duration(), 0);
}

QString HistoryVideo::notificationText() const {
	return WithCaptionNotificationText(lang(lng_in_dlg_video), _caption);
}

QString HistoryVideo::inDialogsText() const {
	return WithCaptionDialogsText(lang(lng_in_dlg_video), _caption);
}

TextWithEntities HistoryVideo::selectedText(TextSelection selection) const {
	return WithCaptionSelectedText(
		lang(lng_in_dlg_video),
		_caption,
		selection);
}

bool HistoryVideo::needsBubble() const {
	if (!_caption.isEmpty()) {
		return true;
	}
	if (auto message = _parent->toHistoryMessage()) {
		return message->viaBot()
			|| message->Has<HistoryMessageReply>()
			|| message->displayForwardedFrom()
//			|| message->displayFromName() // #TODO media views
;
	}
	return false;
}

Storage::SharedMediaTypesMask HistoryVideo::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	return Storage::SharedMediaTypesMask{}
		.added(Type::Video)
		.added(Type::PhotoVideo);
}

void HistoryVideo::updateStatusText() const {
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
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

void HistoryVideo::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryVideo::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistoryVideo::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		auto &mediaDocument = media.c_messageMediaDocument();
		if (!mediaDocument.has_document() || mediaDocument.has_ttl_seconds()) {
			LOG(("Api Error: Got MTPMessageMediaDocument without document or with ttl_seconds in HistoryVideo::updateSentMedia()"));
			return;
		}
		App::feedDocument(mediaDocument.vdocument, _data);
	}
}

bool HistoryVideo::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistoryVideo::replyPreview() {
	if (_data->replyPreview->isNull() && !_data->thumb->isNull()) {
		if (_data->thumb->loaded()) {
			auto w = convertScale(_data->thumb->width());
			auto h = convertScale(_data->thumb->height());
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			_data->replyPreview = ImagePtr(w > h ? _data->thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : _data->thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			_data->thumb->load();
		}
	}
	return _data->replyPreview;
}

HistoryDocument::HistoryDocument(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document,
	const QString &caption)
: HistoryFileMedia(parent)
, _data(document) {
	createComponents(!caption.isEmpty());
	if (auto named = Get<HistoryDocumentNamed>()) {
		fillNamedFromData(named);
	}

	setDocumentLinks(_data, parent);

	setStatusSize(FileStatusSizeReady);

	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		captioned->_caption.setText(
			st::messageTextStyle,
			caption + _parent->skipBlock(),
			Ui::ItemTextNoMonoOptions(_parent));
	}
}

HistoryDocument::HistoryDocument(
	not_null<HistoryItem*> parent,
	const HistoryDocument &other)
: HistoryFileMedia(parent)
, _data(other._data) {
	auto captioned = other.Get<HistoryDocumentCaptioned>();
	createComponents(captioned != 0);
	if (auto named = Get<HistoryDocumentNamed>()) {
		if (auto othernamed = other.Get<HistoryDocumentNamed>()) {
			named->_name = othernamed->_name;
			named->_namew = othernamed->_namew;
		} else {
			fillNamedFromData(named);
		}
	}

	setDocumentLinks(_data, parent);

	setStatusSize(other._statusSize);

	if (captioned) {
		Get<HistoryDocumentCaptioned>()->_caption = captioned->_caption;
	}
}

float64 HistoryDocument::dataProgress() const {
	return _data->progress();
}

bool HistoryDocument::dataFinished() const {
	return !_data->loading() && !_data->uploading();
}

bool HistoryDocument::dataLoaded() const {
	return _data->loaded();
}

void HistoryDocument::createComponents(bool caption) {
	uint64 mask = 0;
	if (_data->isVoiceMessage()) {
		mask |= HistoryDocumentVoice::Bit();
	} else {
		mask |= HistoryDocumentNamed::Bit();
		if (!_data->isSong()
			&& !documentIsExecutableName(_data->filename())
			&& !_data->thumb->isNull()
			&& _data->thumb->width()
			&& _data->thumb->height()) {
			mask |= HistoryDocumentThumbed::Bit();
		}
	}
	if (caption) {
		mask |= HistoryDocumentCaptioned::Bit();
	}
	UpdateComponents(mask);
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		thumbed->_linksavel = std::make_shared<DocumentSaveClickHandler>(
			_data,
			_parent->fullId());
		thumbed->_linkcancell = std::make_shared<DocumentCancelClickHandler>(
			_data,
			_parent->fullId());
	}
	if (auto voice = Get<HistoryDocumentVoice>()) {
		voice->_seekl = std::make_shared<VoiceSeekClickHandler>(
			_data,
			_parent->fullId());
	}
}

void HistoryDocument::fillNamedFromData(HistoryDocumentNamed *named) {
	auto nameString = named->_name = _data->composeNameString();
	named->_namew = st::semiboldFont->width(nameString);
}

QSize HistoryDocument::countOptimalSize() {
	auto captioned = Get<HistoryDocumentCaptioned>();
	if (captioned && captioned->_caption.hasSkipBlock()) {
		captioned->_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	auto thumbed = Get<HistoryDocumentThumbed>();
	if (thumbed) {
		_data->thumb->load();
		auto tw = convertScale(_data->thumb->width());
		auto th = convertScale(_data->thumb->height());
		if (tw > th) {
			thumbed->_thumbw = (tw * st::msgFileThumbSize) / th;
		} else {
			thumbed->_thumbw = st::msgFileThumbSize;
		}
	}

	auto maxWidth = st::msgFileMinWidth;

	auto tleft = 0;
	auto tright = 0;
	if (thumbed) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		accumulate_max(maxWidth, tleft + documentMaxStatusWidth(_data) + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		auto unread = _data->isVoiceMessage() ? (st::mediaUnreadSkip + st::mediaUnreadSize) : 0;
		accumulate_max(maxWidth, tleft + documentMaxStatusWidth(_data) + unread + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	if (auto named = Get<HistoryDocumentNamed>()) {
		accumulate_max(maxWidth, tleft + named->_namew + tright);
		accumulate_max(maxWidth, st::msgMaxWidth);
	}

	auto minHeight = 0;
	if (thumbed) {
		minHeight = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else {
		minHeight = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (!captioned && (_parent->Has<HistoryMessageSigned>() || _parent->displayEditedBadge())) {
		minHeight += st::msgDateFont->height - st::msgDateDelta.y();
	}
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}

	if (captioned) {
		auto captionw = maxWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		minHeight += captioned->_caption.countHeight(captionw);
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize HistoryDocument::countCurrentSize(int newWidth) {
	auto captioned = Get<HistoryDocumentCaptioned>();
	if (!captioned) {
		return HistoryFileMedia::countCurrentSize(newWidth);
	}

	accumulate_min(newWidth, maxWidth());
	auto newHeight = 0;
	if (Get<HistoryDocumentThumbed>()) {
		newHeight = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else {
		newHeight = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (!isBubbleTop()) {
		newHeight -= st::msgFileTopMinus;
	}
	auto captionw = newWidth - st::msgPadding.left() - st::msgPadding.right();
	newHeight += captioned->_caption.countHeight(captionw);
	if (isBubbleBottom()) {
		newHeight += st::msgPadding.bottom();
	}

	return { newWidth, newHeight };
}

void HistoryDocument::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	bool selected = (selection == FullSelection);

	int captionw = width() - st::msgPadding.left() - st::msgPadding.right();
	auto outbg = _parent->hasOutLayout();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool showPause = updateStatusText();
	bool radial = isRadialAnimation(ms);

	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	int nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0, bottom = 0;
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop - topMinus;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop - topMinus;
		linktop = st::msgFileThumbLinkTop - topMinus;
		bottom = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom() - topMinus;

		auto inWebPage = (_parent->getMedia() != this);
		auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, st::msgFileThumbSize, st::msgFileThumbSize, width()));
		QPixmap thumb;
		if (loaded) {
			thumb = _data->thumb->pixSingle(thumbed->_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize, roundRadius);
		} else {
			thumb = _data->thumb->pixBlurredSingle(thumbed->_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize, roundRadius);
		}
		p.drawPixmap(rthumb.topLeft(), thumb);
		if (selected) {
			auto overlayCorners = inWebPage ? SelectedOverlaySmallCorners : SelectedOverlayLargeCorners;
			App::roundRect(p, rthumb, p.textPalette().selectOverlay, overlayCorners);
		}

		if (radial || (!loaded && !_data->loading())) {
			float64 radialOpacity = (radial && loaded && !_data->uploading()) ? _animation->radial.opacity() : 1;
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
			auto icon = ([radial, this, selected] {
				if (radial || _data->loading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
			})();
			p.setOpacity((radial && loaded) ? _animation->radial.opacity() : 1);
			icon->paintInCenter(p, inner);
			if (radial) {
				p.setOpacity(1);

				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
			}
		}

		if (_data->status != FileUploadFailed) {
			const auto &lnk = (_data->loading() || _data->uploading())
				? thumbed->_linkcancell
				: thumbed->_linksavel;
			bool over = ClickHandler::showAsActive(lnk);
			p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
			p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
			p.drawTextLeft(nameleft, linktop, width(), thumbed->_link, thumbed->_linkw);
		}
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop - topMinus;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop - topMinus;
		bottom = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom() - topMinus;

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top() - topMinus, st::msgFileSize, st::msgFileSize, width()));
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(outbg ? st::msgFileOutBgSelected : st::msgFileInBgSelected);
		} else if (isThumbAnimation(ms)) {
			auto over = _animation->a_thumbOver.current();
			p.setBrush(anim::brush(outbg ? st::msgFileOutBg : st::msgFileInBg, outbg ? st::msgFileOutBgOver : st::msgFileInBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(outbg ? (over ? st::msgFileOutBgOver : st::msgFileOutBg) : (over ? st::msgFileInBgOver : st::msgFileInBg));
		}

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			auto fg = outbg ? (selected ? st::historyFileOutRadialFgSelected : st::historyFileOutRadialFg) : (selected ? st::historyFileInRadialFgSelected : st::historyFileInRadialFg);
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, fg);
		}

		auto icon = ([showPause, radial, this, loaded, outbg, selected] {
			if (showPause) {
				return &(outbg ? (selected ? st::historyFileOutPauseSelected : st::historyFileOutPause) : (selected ? st::historyFileInPauseSelected : st::historyFileInPause));
			} else if (radial || _data->loading()) {
				return &(outbg ? (selected ? st::historyFileOutCancelSelected : st::historyFileOutCancel) : (selected ? st::historyFileInCancelSelected : st::historyFileInCancel));
			} else if (loaded) {
				if (_data->isAudioFile() || _data->isVoiceMessage()) {
					return &(outbg ? (selected ? st::historyFileOutPlaySelected : st::historyFileOutPlay) : (selected ? st::historyFileInPlaySelected : st::historyFileInPlay));
				} else if (_data->isImage()) {
					return &(outbg ? (selected ? st::historyFileOutImageSelected : st::historyFileOutImage) : (selected ? st::historyFileInImageSelected : st::historyFileInImage));
				}
				return &(outbg ? (selected ? st::historyFileOutDocumentSelected : st::historyFileOutDocument) : (selected ? st::historyFileInDocumentSelected : st::historyFileInDocument));
			}
			return &(outbg ? (selected ? st::historyFileOutDownloadSelected : st::historyFileOutDownload) : (selected ? st::historyFileInDownloadSelected : st::historyFileInDownload));
		})();
		icon->paintInCenter(p, inner);
	}
	auto namewidth = width() - nameleft - nameright;
	auto statuswidth = namewidth;

	auto voiceStatusOverride = QString();
	if (auto voice = Get<HistoryDocumentVoice>()) {
		const VoiceWaveform *wf = nullptr;
		uchar norm_value = 0;
		if (const auto voiceData = _data->voice()) {
			wf = &voiceData->waveform;
			if (wf->isEmpty()) {
				wf = nullptr;
				if (loaded) {
					Local::countVoiceWaveform(_data);
				}
			} else if (wf->at(0) < 0) {
				wf = nullptr;
			} else {
				norm_value = voiceData->wavemax;
			}
		}
		auto progress = ([voice] {
			if (voice->seeking()) {
				return voice->seekingCurrent();
			} else if (voice->_playback) {
				return voice->_playback->a_progress.current();
			}
			return 0.;
		})();
		if (voice->seeking()) {
			voiceStatusOverride = formatPlayedText(qRound(progress * voice->_lastDurationMs) / 1000, voice->_lastDurationMs / 1000);
		}

		// rescale waveform by going in waveform.size * bar_count 1D grid
		auto active = outbg ? (selected ? st::msgWaveformOutActiveSelected : st::msgWaveformOutActive) : (selected ? st::msgWaveformInActiveSelected : st::msgWaveformInActive);
		auto inactive = outbg ? (selected ? st::msgWaveformOutInactiveSelected : st::msgWaveformOutInactive) : (selected ? st::msgWaveformInInactiveSelected : st::msgWaveformInInactive);
		auto wf_size = wf ? wf->size() : Media::Player::kWaveformSamplesCount;
		auto availw = namewidth + st::msgWaveformSkip;
		auto activew = qRound(availw * progress);
		if (!outbg && !voice->_playback && _parent->isMediaUnread()) {
			activew = availw;
		}
		auto bar_count = qMin(availw / (st::msgWaveformBar + st::msgWaveformSkip), wf_size);
		auto max_value = 0;
		auto max_delta = st::msgWaveformMax - st::msgWaveformMin;
		auto bottom = st::msgFilePadding.top() - topMinus + st::msgWaveformMax;
		p.setPen(Qt::NoPen);
		for (auto i = 0, bar_x = 0, sum_i = 0; i < wf_size; ++i) {
			auto value = wf ? wf->at(i) : 0;
			if (sum_i + bar_count >= wf_size) { // draw bar
				sum_i = sum_i + bar_count - wf_size;
				if (sum_i < (bar_count + 1) / 2) {
					if (max_value < value) max_value = value;
				}
				auto bar_value = ((max_value * max_delta) + ((norm_value + 1) / 2)) / (norm_value + 1);

				if (bar_x >= activew) {
					p.fillRect(nameleft + bar_x, bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, inactive);
				} else if (bar_x + st::msgWaveformBar <= activew) {
					p.fillRect(nameleft + bar_x, bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, active);
				} else {
					p.fillRect(nameleft + bar_x, bottom - bar_value, activew - bar_x, st::msgWaveformMin + bar_value, active);
					p.fillRect(nameleft + activew, bottom - bar_value, st::msgWaveformBar - (activew - bar_x), st::msgWaveformMin + bar_value, inactive);
				}
				bar_x += st::msgWaveformBar + st::msgWaveformSkip;

				if (sum_i < (bar_count + 1) / 2) {
					max_value = 0;
				} else {
					max_value = value;
				}
			} else {
				if (max_value < value) max_value = value;

				sum_i += bar_count;
			}
		}
	} else if (auto named = Get<HistoryDocumentNamed>()) {
		p.setFont(st::semiboldFont);
		p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
		if (namewidth < named->_namew) {
			p.drawTextLeft(nameleft, nametop, width(), st::semiboldFont->elided(named->_name, namewidth, Qt::ElideMiddle));
		} else {
			p.drawTextLeft(nameleft, nametop, width(), named->_name, named->_namew);
		}
	}

	auto statusText = voiceStatusOverride.isEmpty() ? _statusText : voiceStatusOverride;
	auto status = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, width(), statusText);

	if (_parent->isMediaUnread()) {
		auto w = st::normalFont->width(statusText);
		if (w + st::mediaUnreadSkip + st::mediaUnreadSize <= statuswidth) {
			p.setPen(Qt::NoPen);
			p.setBrush(outbg ? (selected ? st::msgFileOutBgSelected : st::msgFileOutBg) : (selected ? st::msgFileInBgSelected : st::msgFileInBg));

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(rtlrect(nameleft + w + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width()));
			}
		}
	}

	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		captioned->_caption.draw(p, st::msgPadding.left(), bottom, captionw, style::al_left, 0, -1, selection);
	}
}

HistoryTextState HistoryDocument::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	bool loaded = _data->loaded();

	bool showPause = updateStatusText();

	auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0, bottom = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nameright = st::msgFileThumbPadding.left();
		nametop = st::msgFileThumbNameTop - topMinus;
		linktop = st::msgFileThumbLinkTop - topMinus;
		bottom = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom() - topMinus;

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, st::msgFileThumbSize, st::msgFileThumbSize, width()));

		if ((_data->loading() || _data->uploading() || !loaded) && rthumb.contains(point)) {
			result.link = (_data->loading() || _data->uploading()) ? _cancell : _savel;
			return result;
		}

		if (_data->status != FileUploadFailed) {
			if (rtlrect(nameleft, linktop, thumbed->_linkw, st::semiboldFont->height, width()).contains(point)) {
				result.link = (_data->loading() || _data->uploading())
					? thumbed->_linkcancell
					: thumbed->_linksavel;
				return result;
			}
		}
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nameright = st::msgFilePadding.left();
		nametop = st::msgFileNameTop - topMinus;
		bottom = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom() - topMinus;

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top() - topMinus, st::msgFileSize, st::msgFileSize, width()));
		if ((_data->loading() || _data->uploading() || !loaded) && inner.contains(point)) {
			result.link = (_data->loading() || _data->uploading()) ? _cancell : _savel;
			return result;
		}
	}

	if (auto voice = Get<HistoryDocumentVoice>()) {
		auto namewidth = width() - nameleft - nameright;
		auto waveformbottom = st::msgFilePadding.top() - topMinus + st::msgWaveformMax + st::msgWaveformMin;
		if (QRect(nameleft, nametop, namewidth, waveformbottom - nametop).contains(point)) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (!voice->seeking()) {
					voice->setSeekingStart((point.x() - nameleft) / float64(namewidth));
				}
				result.link = voice->_seekl;
				return result;
			}
		}
	}

	auto painth = height();
	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		if (point.y() >= bottom) {
			result = HistoryTextState(_parent, captioned->_caption.getState(
				point - QPoint(st::msgPadding.left(), bottom),
				width() - st::msgPadding.left() - st::msgPadding.right(),
				request.forText()));
			return result;
		}
		auto captionw = width() - st::msgPadding.left() - st::msgPadding.right();
		painth -= captioned->_caption.countHeight(captionw);
		if (isBubbleBottom()) {
			painth -= st::msgPadding.bottom();
		}
	}
	if (QRect(0, 0, width(), painth).contains(point) && !_data->loading() && !_data->uploading() && _data->isValid()) {
		result.link = _openl;
		return result;
	}
	return result;
}

void HistoryDocument::updatePressed(QPoint point) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->seeking()) {
			auto nameleft = 0, nameright = 0;
			if (auto thumbed = Get<HistoryDocumentThumbed>()) {
				nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
				nameright = st::msgFileThumbPadding.left();
			} else {
				nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
				nameright = st::msgFilePadding.left();
			}
			voice->setSeekingCurrent(snap((point.x() - nameleft) / float64(width() - nameleft - nameright), 0., 1.));
			Auth().data().requestItemRepaint(_parent);
		}
	}
}

QString HistoryDocument::notificationText() const {
	QString result;
	buildStringRepresentation([&result](const QString &type, const QString &fileName, const Text &caption) {
		result = WithCaptionNotificationText(
			fileName.isEmpty() ? type : fileName,
			caption);
	});
	return result;
}

QString HistoryDocument::inDialogsText() const {
	QString result;
	buildStringRepresentation([&result](const QString &type, const QString &fileName, const Text &caption) {
		result = WithCaptionDialogsText(
			fileName.isEmpty() ? type : fileName,
			caption);
	});
	return result;
}

TextSelection HistoryDocument::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		return captioned->_caption.adjustSelection(selection, type);
	}
	return selection;
}

uint16 HistoryDocument::fullSelectionLength() const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		return captioned->_caption.length();
	}
	return 0;
}

bool HistoryDocument::hasTextForCopy() const {
	return Has<HistoryDocumentCaptioned>();
}

TextWithEntities HistoryDocument::selectedText(TextSelection selection) const {
	TextWithEntities result;
	buildStringRepresentation([&result, selection](const QString &type, const QString &fileName, const Text &caption) {
		auto fullType = type;
		if (!fileName.isEmpty()) {
			fullType.append(qstr(" : ")).append(fileName);
		}
		result = WithCaptionSelectedText(fullType, caption, selection);
	});
	return result;
}

Storage::SharedMediaTypesMask HistoryDocument::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_data->isVoiceMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::VoiceFile)
			.added(Type::RoundVoiceFile);
	} else if (_data->isSharedMediaMusic()) {
		return Type::MusicFile;
	}
	return Type::File;
}

template <typename Callback>
void HistoryDocument::buildStringRepresentation(Callback callback) const {
	const Text emptyCaption;
	const Text *caption = &emptyCaption;
	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		caption = &captioned->_caption;
	}
	QString attachType = lang(lng_in_dlg_file);
	if (Has<HistoryDocumentVoice>()) {
		attachType = lang(lng_in_dlg_audio);
	} else if (_data->isAudioFile()) {
		attachType = lang(lng_in_dlg_audio_file);
	}

	QString attachFileName;
	if (auto named = Get<HistoryDocumentNamed>()) {
		if (!named->_name.isEmpty()) {
			attachFileName = named->_name;
		}
	}
	return callback(attachType, attachFileName, *caption);
}

void HistoryDocument::setStatusSize(int newSize, qint64 realDuration) const {
	auto duration = _data->isSong()
		? _data->song()->duration
		: (_data->isVoiceMessage()
			? _data->voice()->duration
			: -1);
	HistoryFileMedia::setStatusSize(newSize, _data->size, duration, realDuration);
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		if (_statusSize == FileStatusSizeReady) {
			thumbed->_link = lang(lng_media_download).toUpper();
		} else if (_statusSize == FileStatusSizeLoaded) {
			thumbed->_link = lang(lng_media_open_with).toUpper();
		} else if (_statusSize == FileStatusSizeFailed) {
			thumbed->_link = lang(lng_media_download).toUpper();
		} else if (_statusSize >= 0) {
			thumbed->_link = lang(lng_media_cancel).toUpper();
		} else {
			thumbed->_link = lang(lng_media_open_with).toUpper();
		}
		thumbed->_linkw = st::semiboldFont->width(thumbed->_link);
	}
}

bool HistoryDocument::updateStatusText() const {
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
		using State = Media::Player::State;
		statusSize = FileStatusSizeLoaded;
		if (_data->isVoiceMessage()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
				if (auto voice = Get<HistoryDocumentVoice>()) {
					bool was = (voice->_playback != nullptr);
					voice->ensurePlayback(this);
					if (!was || state.position != voice->_playback->_position) {
						auto prg = state.length ? snap(float64(state.position) / state.length, 0., 1.) : 0.;
						if (voice->_playback->_position < state.position) {
							voice->_playback->a_progress.start(prg);
						} else {
							voice->_playback->a_progress = anim::value(0., prg);
						}
						voice->_playback->_position = state.position;
						voice->_playback->_a_progress.start();
					}
					voice->_lastDurationMs = static_cast<int>((state.length * 1000LL) / state.frequency); // Bad :(
				}

				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.length / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			} else {
				if (auto voice = Get<HistoryDocumentVoice>()) {
					voice->checkPlaybackFinished();
				}
			}
			if (!showPause && (state.id == AudioMsgId(_data, _parent->fullId()))) {
				showPause = Media::Player::instance()->isSeeking(AudioMsgId::Type::Voice);
			}
		} else if (_data->isAudioFile()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.length / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			} else {
			}
			if (!showPause && (state.id == AudioMsgId(_data, _parent->fullId()))) {
				showPause = Media::Player::instance()->isSeeking(AudioMsgId::Type::Song);
			}
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize, realDuration);
	}
	return showPause;
}

QMargins HistoryDocument::bubbleMargins() const {
	return Get<HistoryDocumentThumbed>() ? QMargins(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbPadding.left(), st::msgFileThumbPadding.bottom()) : st::msgPadding;
}

void HistoryDocument::step_voiceProgress(float64 ms, bool timer) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->_playback) {
			float64 dt = ms / (2 * AudioVoiceMsgUpdateView);
			if (dt >= 1) {
				voice->_playback->_a_progress.stop();
				voice->_playback->a_progress.finish();
			} else {
				voice->_playback->a_progress.update(qMin(dt, 1.), anim::linear);
			}
			if (timer) Auth().data().requestItemRepaint(_parent);
		}
	}
}

void HistoryDocument::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (pressed && p == voice->_seekl && !voice->seeking()) {
			voice->startSeeking();
		} else if (!pressed && voice->seeking()) {
			auto type = AudioMsgId::Type::Voice;
			auto state = Media::Player::mixer()->currentState(type);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && state.length) {
				auto currentProgress = voice->seekingCurrent();
				auto currentPosition = state.frequency
					? qRound(currentProgress * state.length * 1000. / state.frequency)
					: 0;
				Media::Player::mixer()->seek(type, currentPosition);

				voice->ensurePlayback(this);
				voice->_playback->_position = 0;
				voice->_playback->a_progress = anim::value(currentProgress, currentProgress);
			}
			voice->stopSeeking();
		}
	}
	HistoryFileMedia::clickHandlerPressedChanged(p, pressed);
}

void HistoryDocument::refreshParentId(not_null<HistoryItem*> realParent) {
	HistoryFileMedia::refreshParentId(realParent);

	const auto contextId = realParent->fullId();
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		if (thumbed->_linksavel) {
			thumbed->_linksavel->setMessageId(contextId);
			thumbed->_linkcancell->setMessageId(contextId);
		}
	}
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->_seekl) {
			voice->_seekl->setMessageId(contextId);
		}
	}
}

bool HistoryDocument::playInline(bool autoplay) {
	if (_data->isVoiceMessage()) {
		DocumentOpenClickHandler::doOpen(_data, _parent, ActionOnLoadPlayInline);
		return true;
	} else if (_data->isAudioFile()) {
		Media::Player::instance()->play(AudioMsgId(_data, _parent->fullId()));
		return true;
	}
	return false;
}

void HistoryDocument::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryDocument::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistoryDocument::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		auto &mediaDocument = media.c_messageMediaDocument();
		if (!mediaDocument.has_document() || mediaDocument.has_ttl_seconds()) {
			LOG(("Api Error: Got MTPMessageMediaDocument without document or with ttl_seconds in HistoryDocument::updateSentMedia()"));
			return;
		}
		App::feedDocument(mediaDocument.vdocument, _data);
		if (!_data->data().isEmpty()) {
			if (_data->isVoiceMessage()) {
				Local::writeAudio(_data->mediaKey(), _data->data());
			} else {
				Local::writeStickerImage(_data->mediaKey(), _data->data());
			}
		}
	}
}

bool HistoryDocument::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

TextWithEntities HistoryDocument::getCaption() const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		return captioned->_caption.originalTextWithEntities();
	}
	return TextWithEntities();
}

ImagePtr HistoryDocument::replyPreview() {
	return _data->makeReplyPreview();
}

HistoryGif::HistoryGif(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document,
	const QString &caption)
: HistoryFileMedia(parent)
, _data(document)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	setDocumentLinks(_data, parent, true);

	setStatusSize(FileStatusSizeReady);

	if (!caption.isEmpty() && !_data->isVideoMessage()) {
		_caption.setText(
			st::messageTextStyle,
			caption + _parent->skipBlock(),
			Ui::ItemTextNoMonoOptions(_parent));
	}

	_data->thumb->load();
}

HistoryGif::HistoryGif(
	not_null<HistoryItem*> parent,
	const HistoryGif &other)
: HistoryFileMedia(parent)
, _data(other._data)
, _thumbw(other._thumbw)
, _thumbh(other._thumbh)
, _caption(other._caption) {
	setDocumentLinks(_data, parent, true);

	setStatusSize(other._statusSize);
}

QSize HistoryGif::countOptimalSize() {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}
	if (!_openInMediaviewLink) {
		_openInMediaviewLink = std::make_shared<DocumentOpenClickHandler>(
			_data,
			_parent->fullId());
	}

	auto tw = 0;
	auto th = 0;
	if (_gif && _gif->state() == Media::Clip::State::Error) {
		if (!_gif->autoplay()) {
			Ui::show(Box<InformBox>(lang(lng_gif_error)));
		}
		setClipReader(Media::Clip::ReaderPointer::Bad());
	}

	if (_gif && _gif->ready()) {
		tw = convertScale(_gif->width());
		th = convertScale(_gif->height());
	} else {
		tw = convertScale(_data->dimensions.width()), th = convertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = convertScale(_data->thumb->width());
			th = convertScale(_data->thumb->height());
		}
	}
	if (tw > st::maxGifSize) {
		th = (st::maxGifSize * th) / tw;
		tw = st::maxGifSize;
	}
	if (th > st::maxGifSize) {
		tw = (st::maxGifSize * tw) / th;
		th = st::maxGifSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}
	_thumbw = tw;
	_thumbh = th;
	auto maxWidth = qMax(tw, st::minPhotoSize);
	auto minHeight = qMax(th, st::minPhotoSize);
	accumulate_max(maxWidth, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	if (!_gif || !_gif->ready()) {
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
		auto via = _parent->Get<HistoryMessageVia>();
		auto reply = _parent->Get<HistoryMessageReply>();
		auto forwarded = _parent->Get<HistoryMessageForwarded>();
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
	if (_gif && _gif->ready()) {
		tw = convertScale(_gif->width());
		th = convertScale(_gif->height());
	} else {
		tw = convertScale(_data->dimensions.width()), th = convertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = convertScale(_data->thumb->width());
			th = convertScale(_data->thumb->height());
		}
	}
	if (tw > st::maxGifSize) {
		th = (st::maxGifSize * th) / tw;
		tw = st::maxGifSize;
	}
	if (th > st::maxGifSize) {
		tw = (st::maxGifSize * tw) / th;
		th = st::maxGifSize;
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
	if (_gif && _gif->ready()) {
		if (!_gif->started()) {
			auto isRound = _data->isVideoMessage();
			auto inWebPage = (_parent->getMedia() != this);
			auto roundRadius = isRound ? ImageRoundRadius::Ellipse : inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
			auto roundCorners = (isRound || inWebPage) ? RectPart::AllCorners : ((isBubbleTop() ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
				| ((isBubbleBottom() && _caption.isEmpty()) ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None));
			_gif->start(_thumbw, _thumbh, newWidth, newHeight, roundRadius, roundCorners);
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
		auto via = _parent->Get<HistoryMessageVia>();
		auto reply = _parent->Get<HistoryMessageReply>();
		auto forwarded = _parent->Get<HistoryMessageForwarded>();
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

	_data->automaticLoad(_parent);
	auto loaded = _data->loaded();
	auto displayLoading = (_parent->id < 0) || _data->displayLoading();
	auto selected = (selection == FullSelection);

	auto videoFinished = _gif && (_gif->mode() == Media::Clip::Reader::Mode::Video) && (_gif->state() == Media::Clip::State::Finished);
	if (loaded && cAutoPlayGif() && ((!_gif && !_gif.isBad()) || videoFinished)) {
		Ui::autoplayMediaInlineAsync(_parent->fullId());
	}

	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();
	auto outbg = _parent->hasOutLayout();
	auto isChildMedia = (_parent->getMedia() != this);

	auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();

	auto isRound = _data->isVideoMessage();
	auto displayMute = false;
	auto animating = (_gif && _gif->started());

	if (!animating || _parent->id < 0) {
		if (displayLoading) {
			ensureAnimation();
			if (!_animation->radial.animating()) {
				_animation->radial.start(dataProgress());
			}
		}
		updateStatusText();
	} else if (_gif && _gif->mode() == Media::Clip::Reader::Mode::Video) {
		updateStatusText();
	}
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
	auto via = separateRoundVideo ? _parent->Get<HistoryMessageVia>() : nullptr;
	auto reply = separateRoundVideo ? _parent->Get<HistoryMessageReply>() : nullptr;
	auto forwarded = separateRoundVideo ? _parent->Get<HistoryMessageForwarded>() : nullptr;
	if (via || reply || forwarded) {
		usew = maxWidth() - additionalWidth(via, reply, forwarded);
		if (outbg) {
			usex = width() - usew;
		}
	}
	if (rtl()) usex = width() - usex - usew;

	QRect rthumb(rtlrect(usex + paintx, painty, usew, painth, width()));

	auto roundRadius = isRound ? ImageRoundRadius::Ellipse : isChildMedia ? ImageRoundRadius::Small : ImageRoundRadius::Large;
	auto roundCorners = (isRound || isChildMedia) ? RectPart::AllCorners : ((isBubbleTop() ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
		| ((isBubbleBottom() && _caption.isEmpty()) ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None));
	if (animating) {
		auto paused = App::wnd()->controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
		if (isRound) {
			if (_gif->mode() == Media::Clip::Reader::Mode::Video) {
				paused = false;
			} else {
				displayMute = true;
			}
		}
		p.drawPixmap(rthumb.topLeft(), _gif->current(_thumbw, _thumbh, usew, painth, roundRadius, roundCorners, paused ? 0 : ms));

		if (displayMute) {
			_roundPlayback.reset();
		} else if (_roundPlayback) {
			auto value = _roundPlayback->value(ms);
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
		p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, _thumbh, usew, painth, roundRadius, roundCorners));
	}

	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (radial || _gif.isBad() || (!_gif && ((!loaded && !_data->loading()) || !cAutoPlayGif()))) {
		auto radialOpacity = (radial && loaded && _parent->id > 0) ? _animation->radial.opacity() : 1.;
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
		auto icon = ([this, radial, selected]() -> const style::icon * {
			if (_data->loaded() && !radial) {
				return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
			} else if (radial || _data->loading()) {
				if (_parent->id > 0 || _data->uploading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		})();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
		}

		if (!isRound && (!animating || _parent->id < 0)) {
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

	if (!isChildMedia && isRound) {
		auto mediaUnread = _parent->isMediaUnread();
		auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
		auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		auto statusX = usex + paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
		auto statusY = painty + painth - st::msgDateImgDelta - statusH + st::msgDateImgPadding.y();
		if (_parent->isMediaUnread()) {
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
	} else if (!isChildMedia) {
		auto fullRight = paintx + usex + usew;
		auto fullBottom = painty + painth;
		// #TODO view media
		auto maxRight = _parent->history()->width - st::msgMargin.left();
		if (_parent->history()->canHaveFromPhotos()) {
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
			_parent->drawInfo(p, fullRight, fullBottom, 2 * paintx + paintw, selected, isRound ? InfoDisplayOverBackground : InfoDisplayOverImage);
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

HistoryTextState HistoryGif::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);

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
			result = HistoryTextState(_parent, _caption.getState(
				point - QPoint(st::msgPadding.left(), painth),
				captionw,
				request.forText()));
			return result;
		}
		painth -= st::mediaCaptionSkip;
	}
	auto outbg = _parent->hasOutLayout();
	auto isChildMedia = (_parent->getMedia() != this);
	auto isRound = _data->isVideoMessage();
	auto usew = paintw, usex = 0;
	auto separateRoundVideo = isSeparateRoundVideo();
	auto via = separateRoundVideo ? _parent->Get<HistoryMessageVia>() : nullptr;
	auto reply = separateRoundVideo ? _parent->Get<HistoryMessageReply>() : nullptr;
	auto forwarded = separateRoundVideo ? _parent->Get<HistoryMessageForwarded>() : nullptr;
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
				result = HistoryTextState(_parent, forwarded->text.getState(
					point - QPoint(rectx + st::msgReplyPadding.left(), recty + st::msgReplyPadding.top()),
					innerw,
					textRequest));
				result.symbol = 0;
				result.afterSymbol = false;
				if (breakEverywhere) {
					result.cursor = HistoryInForwardedCursorState;
				} else {
					result.cursor = HistoryDefaultCursorState;
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
		// #TODO view media
		auto maxRight = _parent->history()->width - st::msgMargin.left();
		if (_parent->history()->canHaveFromPhotos()) {
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
		if (!isChildMedia) {
			if (_parent->pointInTime(fullRight, fullBottom, point, isRound ? InfoDisplayOverBackground : InfoDisplayOverImage)) {
				result.cursor = HistoryInDateCursorState;
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

QString HistoryGif::notificationText() const {
	return WithCaptionNotificationText(mediaTypeString(), _caption);
}

QString HistoryGif::inDialogsText() const {
	return WithCaptionDialogsText(mediaTypeString(), _caption);
}

TextWithEntities HistoryGif::selectedText(TextSelection selection) const {
	return WithCaptionSelectedText(mediaTypeString(), _caption, selection);
}

bool HistoryGif::needsBubble() const {
	if (_data->isVideoMessage()) {
		return false;
	}
	if (!_caption.isEmpty()) {
		return true;
	}
	if (auto message = _parent->toHistoryMessage()) {
		return message->viaBot()
			|| message->Has<HistoryMessageReply>()
			|| message->displayForwardedFrom()
//			|| message->displayFromName() // #TODO media views
			;
	}
	return false;
}

Storage::SharedMediaTypesMask HistoryGif::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_data->isVideoMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::RoundFile)
			.added(Type::RoundVoiceFile);
	} else if (_data->isGifv()) {
		return Type::GIF;
	}
	return Type::File;
}

int HistoryGif::additionalWidth() const {
	return additionalWidth(
		_parent->Get<HistoryMessageVia>(),
		_parent->Get<HistoryMessageReply>(),
		_parent->Get<HistoryMessageForwarded>());
}

QString HistoryGif::mediaTypeString() const {
	return _data->isVideoMessage()
		? lang(lng_in_dlg_video_message)
		: qsl("GIF");
}

bool HistoryGif::isSeparateRoundVideo() const {
	return _data->isVideoMessage()
		&& (_parent->getMedia() == this)
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
		if (_gif && _gif->mode() == Media::Clip::Reader::Mode::Video) {
			statusSize = -1 - _data->duration();

			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == _gif->audioMsgId()) {
				if (state.length) {
					auto position = int64(0);
					if (Media::Player::IsStoppedAtEnd(state.state)) {
						position = state.length;
					} else if (!Media::Player::IsStoppedOrStopping(state.state)) {
						position = state.position;
					}
					accumulate_max(statusSize, -1 - int((state.length - position) / state.frequency + 1));
				}
				if (_roundPlayback) {
					_roundPlayback->updateState(state);
				}
			}
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

QString HistoryGif::additionalInfoString() const {
	if (_data->isVideoMessage()) {
		updateStatusText();
		return _statusText;
	}
	return QString();
}

void HistoryGif::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryGif::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistoryGif::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		auto &mediaDocument = media.c_messageMediaDocument();
		if (!mediaDocument.has_document() || mediaDocument.has_ttl_seconds()) {
			LOG(("Api Error: Got MTPMessageMediaDocument without document or with ttl_seconds in HistoryGif::updateSentMedia()"));
			return;
		}
		App::feedDocument(mediaDocument.vdocument, _data);
	}
}

bool HistoryGif::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistoryGif::replyPreview() {
	return _data->makeReplyPreview();
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

bool HistoryGif::playInline(bool autoplay) {
	using Mode = Media::Clip::Reader::Mode;
	if (_data->isVideoMessage() && _gif) {
		// Stop autoplayed silent video when we start playback by click.
		// Stop finished video message when autoplay starts.
		if (!autoplay) {
			if (_gif->mode() == Mode::Gif) {
				stopInline();
			} else {
				_gif->pauseResumeVideo();
				return true;
			}
		} else if (autoplay && _gif->mode() == Mode::Video && _gif->state() == Media::Clip::State::Finished) {
			stopInline();
		}
	}
	if (_gif) {
		stopInline();
	} else if (_data->loaded(DocumentData::FilePathResolveChecked)) {
		if (!cAutoPlayGif()) {
			App::stopGifItems();
		}
		const auto mode = (!autoplay && _data->isVideoMessage())
			? Mode::Video
			: Mode::Gif;
		setClipReader(Media::Clip::MakeReader(_data, _parent->fullId(), [this](Media::Clip::Notification notification) {
			_parent->clipCallback(notification);
		}, mode));
		if (mode == Mode::Video) {
			_roundPlayback = std::make_unique<Media::Clip::Playback>();
			_roundPlayback->setValueChangedCallback([this](float64 value) {
				Auth().data().requestItemRepaint(_parent);
			});
			if (App::main()) {
				App::main()->mediaMarkRead(_data);
			}
			App::wnd()->controller()->enableGifPauseReason(Window::GifPauseReason::RoundPlaying);
		}
		if (_gif && autoplay) {
			_gif->setAutoplay();
		}
	}
	return true;
}

bool HistoryGif::isRoundVideoPlaying() const {
	return (_gif && _gif->mode() == Media::Clip::Reader::Mode::Video);
}

void HistoryGif::stopInline() {
	if (isRoundVideoPlaying()) {
		App::wnd()->controller()->disableGifPauseReason(Window::GifPauseReason::RoundPlaying);
	}
	clearClipReader();

	Auth().data().requestItemViewResize(_parent);
	Auth().data().markItemLayoutChanged(_parent);
}

void HistoryGif::setClipReader(Media::Clip::ReaderPointer gif) {
	if (_gif) {
		App::unregGifItem(_gif.get());
	}
	_gif = std::move(gif);
	if (_gif) {
		App::regGifItem(_gif.get(), _parent);
	}
}

HistoryGif::~HistoryGif() {
	clearClipReader();
}

float64 HistoryGif::dataProgress() const {
	return (_data->uploading() || _parent->id > 0)
		? _data->progress()
		: 0;
}

bool HistoryGif::dataFinished() const {
	return (_parent->id > 0)
		? (!_data->loading() && !_data->uploading())
		: false;
}

bool HistoryGif::dataLoaded() const {
	return (_parent->id > 0) ? _data->loaded() : false;
}

bool HistoryGif::needInfoDisplay() const {
	return (_data->uploading() || _parent->isUnderCursor());
}

HistorySticker::HistorySticker(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document)
: HistoryMedia(parent)
, _data(document)
, _emoji(_data->sticker()->alt) {
	_data->thumb->load();
	if (auto emoji = Ui::Emoji::Find(_emoji)) {
		_emoji = emoji->text();
	}
}

QSize HistorySticker::countOptimalSize() {
	auto sticker = _data->sticker();

	if (!_packLink && sticker && sticker->set.type() != mtpc_inputStickerSetEmpty) {
		_packLink = std::make_shared<LambdaClickHandler>([document = _data] {
			if (auto sticker = document->sticker()) {
				if (sticker->set.type() != mtpc_inputStickerSetEmpty && App::main()) {
					App::main()->stickersBox(sticker->set);
				}
			}
		});
	}
	_pixw = _data->dimensions.width();
	_pixh = _data->dimensions.height();
	if (_pixw > st::maxStickerSize) {
		_pixh = (st::maxStickerSize * _pixh) / _pixw;
		_pixw = st::maxStickerSize;
	}
	if (_pixh > st::maxStickerSize) {
		_pixw = (st::maxStickerSize * _pixw) / _pixh;
		_pixh = st::maxStickerSize;
	}
	if (_pixw < 1) _pixw = 1;
	if (_pixh < 1) _pixh = 1;
	auto maxWidth = qMax(_pixw, st::minPhotoSize);
	auto minHeight = qMax(_pixh, st::minPhotoSize);
	if (_parent->getMedia() == this) {
		maxWidth += additionalWidth();
	}
	return { maxWidth, minHeight };
}

QSize HistorySticker::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	if (_parent->getMedia() == this) {
		auto via = _parent->Get<HistoryMessageVia>();
		auto reply = _parent->Get<HistoryMessageReply>();
		if (via || reply) {
			int usew = maxWidth() - additionalWidth(via, reply);
			int availw = newWidth - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.left();
			if (via) {
				via->resize(availw);
			}
			if (reply) {
				reply->resize(availw);
			}
		}
	}
	return { newWidth, minHeight() };
}

void HistorySticker::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	auto sticker = _data->sticker();
	if (!sticker) return;

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->checkSticker();
	bool loaded = _data->loaded();
	bool selected = (selection == FullSelection);

	auto outbg = _parent->hasOutLayout();
	auto childmedia = (_parent->getMedia() != this);

	int usew = maxWidth(), usex = 0;
	auto via = childmedia ? nullptr : _parent->Get<HistoryMessageVia>();
	auto reply = childmedia ? nullptr : _parent->Get<HistoryMessageReply>();
	if (via || reply) {
		usew -= additionalWidth(via, reply);
		if (outbg) {
			usex = width() - usew;
		}
	}
	if (rtl()) usex = width() - usex - usew;

	if (selected) {
		if (sticker->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (minHeight() - _pixh) / 2), _data->thumb->pixBlurredColored(st::msgStickerOverlay, _pixw, _pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (minHeight() - _pixh) / 2), sticker->img->pixColored(st::msgStickerOverlay, _pixw, _pixh));
		}
	} else {
		if (sticker->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (minHeight() - _pixh) / 2), _data->thumb->pixBlurred(_pixw, _pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (minHeight() - _pixh) / 2), sticker->img->pix(_pixw, _pixh));
		}
	}

	if (!childmedia) {
		auto fullRight = usex + usew;
		auto fullBottom = height();
		_parent->drawInfo(p, fullRight, fullBottom, usex * 2 + usew, selected, InfoDisplayOverBackground);
		if (via || reply) {
			int rectw = width() - usew - st::msgReplyPadding.left();
			int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
			if (via) {
				recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			}
			if (reply) {
				recth += st::msgReplyBarSize.height();
			}
			int rectx = outbg ? 0 : (usew + st::msgReplyPadding.left());
			int recty = st::msgDateImgDelta;
			if (rtl()) rectx = width() - rectx - rectw;

			App::roundRect(p, rectx, recty, rectw, recth, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
			p.setPen(st::msgServiceFg);
			rectx += st::msgReplyPadding.left();
			rectw -= st::msgReplyPadding.left() + st::msgReplyPadding.right();
			if (via) {
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
		if (_parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * usex + usew);
		}
	}
}

HistoryTextState HistorySticker::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	auto outbg = _parent->hasOutLayout();
	auto childmedia = (_parent->getMedia() != this);

	int usew = maxWidth(), usex = 0;
	auto via = childmedia ? nullptr : _parent->Get<HistoryMessageVia>();
	auto reply = childmedia ? nullptr : _parent->Get<HistoryMessageReply>();
	if (via || reply) {
		usew -= additionalWidth(via, reply);
		if (outbg) {
			usex = width() - usew;
		}
	}
	if (rtl()) usex = width() - usex - usew;

	if (via || reply) {
		int rectw = width() - usew - st::msgReplyPadding.left();
		int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
		if (via) {
			recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			recth += st::msgReplyBarSize.height();
		}
		int rectx = outbg ? 0 : (usew + st::msgReplyPadding.left());
		int recty = st::msgDateImgDelta;
		if (rtl()) rectx = width() - rectx - rectw;

		if (via) {
			int viah = st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? 0 : st::msgReplyPadding.bottom());
			if (QRect(rectx, recty, rectw, viah).contains(point)) {
				result.link = via->link;
				return result;
			}
			int skip = st::msgServiceNameFont->height + (reply ? 2 * st::msgReplyPadding.top() : 0);
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
	if (_parent->getMedia() == this) {
		auto fullRight = usex + usew;
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayOverImage)) {
			result.cursor = HistoryInDateCursorState;
		}
		if (_parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}

	auto pixLeft = usex + (usew - _pixw) / 2;
	auto pixTop = (minHeight() - _pixh) / 2;
	if (QRect(pixLeft, pixTop, _pixw, _pixh).contains(point)) {
		result.link = _packLink;
		return result;
	}
	return result;
}

QString HistorySticker::toString() const {
	return _emoji.isEmpty() ? lang(lng_in_dlg_sticker) : lng_in_dlg_sticker_emoji(lt_emoji, _emoji);
}

QString HistorySticker::notificationText() const {
	return toString();
}

TextWithEntities HistorySticker::selectedText(TextSelection selection) const {
	if (selection != FullSelection) {
		return TextWithEntities();
	}
	return { qsl("[ ") + toString() + qsl(" ]"), EntitiesInText() };
}

void HistorySticker::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistorySticker::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistorySticker::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		auto &mediaDocument = media.c_messageMediaDocument();
		if (!mediaDocument.has_document() || mediaDocument.has_ttl_seconds()) {
			LOG(("Api Error: Got MTPMessageMediaDocument without document or with ttl_seconds in HistorySticker::updateSentMedia()"));
			return;
		}
		App::feedDocument(mediaDocument.vdocument, _data);
		if (!_data->data().isEmpty()) {
			Local::writeStickerImage(_data->mediaKey(), _data->data());
		}
	}
}

bool HistorySticker::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistorySticker::replyPreview() {
	return _data->makeReplyPreview();
}

int HistorySticker::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const {
	int result = 0;
	if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

int HistorySticker::additionalWidth() const {
	return additionalWidth(
		_parent->Get<HistoryMessageVia>(),
		_parent->Get<HistoryMessageReply>());
}

namespace {

ClickHandlerPtr sendMessageClickHandler(PeerData *peer) {
	return std::make_shared<LambdaClickHandler>([peer] {
		App::wnd()->controller()->showPeerHistory(
			peer->id,
			Window::SectionShow::Way::Forward);
	});
}

ClickHandlerPtr addContactClickHandler(HistoryItem *item) {
	return std::make_shared<LambdaClickHandler>([fullId = item->fullId()] {
		if (auto item = App::histItemById(fullId)) {
			if (auto media = item->getMedia()) {
				if (media->type() == MediaTypeContact) {
					auto contact = static_cast<HistoryContact*>(media);
					auto fname = contact->fname();
					auto lname = contact->lname();
					auto phone = contact->phone();
					Ui::show(Box<AddContactBox>(fname, lname, phone));
				}
			}
		}
	});
}

} // namespace

HistoryContact::HistoryContact(not_null<HistoryItem*> parent, int32 userId, const QString &first, const QString &last, const QString &phone) : HistoryMedia(parent)
, _userId(userId)
, _fname(first)
, _lname(last)
, _phone(App::formatPhone(phone)) {
	_name.setText(
		st::semiboldTextStyle,
		lng_full_name(lt_first_name, first, lt_last_name, last).trimmed(),
		Ui::NameTextOptions());
	_phonew = st::normalFont->width(_phone);
}

QSize HistoryContact::countOptimalSize() {
	auto maxWidth = st::msgFileMinWidth;

	_contact = _userId ? App::userLoaded(_userId) : nullptr;
	if (_contact) {
		_contact->loadUserpic();
	} else {
		_photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Data::PeerUserpicColor(_userId ? _userId : _parent->id),
			_name.originalText());
	}
	if (_contact
		&& _contact->contactStatus() == UserData::ContactStatus::Contact) {
		_linkl = sendMessageClickHandler(_contact);
		_link = lang(lng_profile_send_message).toUpper();
	} else if (_userId) {
		_linkl = addContactClickHandler(_parent);
		_link = lang(lng_profile_add_contact).toUpper();
	}
	_linkw = _link.isEmpty() ? 0 : st::semiboldFont->width(_link);

	auto tleft = 0;
	auto tright = 0;
	if (_userId) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		accumulate_max(maxWidth, tleft + _phonew + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		accumulate_max(maxWidth, tleft + _phonew + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	accumulate_max(maxWidth, tleft + _name.maxWidth() + tright);
	accumulate_max(maxWidth, st::msgMaxWidth);
	auto minHeight = 0;
	if (_userId) {
		minHeight = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
		if (_parent->Has<HistoryMessageSigned>()) {
			minHeight += st::msgDateFont->height - st::msgDateDelta.y();
		}
	} else {
		minHeight = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

void HistoryContact::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	accumulate_min(paintw, maxWidth());

	auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop - topMinus;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop - topMinus;
		linktop = st::msgFileThumbLinkTop - topMinus;

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, st::msgFileThumbSize, st::msgFileThumbSize, paintw));
		if (_contact) {
			_contact->paintUserpic(p, rthumb.x(), rthumb.y(), st::msgFileThumbSize);
		} else {
			_photoEmpty->paint(p, st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, paintw, st::msgFileThumbSize);
		}
		if (selected) {
			PainterHighQualityEnabler hq(p);
			p.setBrush(p.textPalette().selectOverlay);
			p.setPen(Qt::NoPen);
			p.drawEllipse(rthumb);
		}

		bool over = ClickHandler::showAsActive(_linkl);
		p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
		p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
		p.drawTextLeft(nameleft, linktop, paintw, _link, _linkw);
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop - topMinus;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop - topMinus;

		_photoEmpty->paint(p, st::msgFilePadding.left(), st::msgFilePadding.top() - topMinus, paintw, st::msgFileSize);
	}
	auto namewidth = paintw - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
	_name.drawLeftElided(p, nameleft, nametop, namewidth, paintw);

	auto &status = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, paintw, _phone);
}

HistoryTextState HistoryContact::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);

	auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		linktop = st::msgFileThumbLinkTop - topMinus;
		if (rtlrect(nameleft, linktop, _linkw, st::semiboldFont->height, width()).contains(point)) {
			result.link = _linkl;
			return result;
		}
	}
	if (QRect(0, 0, width(), height()).contains(point) && _contact) {
		result.link = _contact->openLink();
		return result;
	}
	return result;
}

QString HistoryContact::notificationText() const {
	return lang(lng_in_dlg_contact);
}

TextWithEntities HistoryContact::selectedText(TextSelection selection) const {
	if (selection != FullSelection) {
		return TextWithEntities();
	}
	return { qsl("[ ") + lang(lng_in_dlg_contact) + qsl(" ]\n") + _name.originalText() + '\n' + _phone, EntitiesInText() };
}

void HistoryContact::attachToParent() {
	if (_userId) {
		App::regSharedContactItem(_userId, _parent);
	}
}

void HistoryContact::detachFromParent() {
	if (_userId) {
		App::unregSharedContactItem(_userId, _parent);
	}
}

void HistoryContact::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaContact) {
		if (_userId != media.c_messageMediaContact().vuser_id.v) {
			detachFromParent();
			_userId = media.c_messageMediaContact().vuser_id.v;
			attachToParent();
		}
	}
}

HistoryContact::~HistoryContact() = default;

HistoryCall::HistoryCall(not_null<HistoryItem*> parent, const MTPDmessageActionPhoneCall &call) : HistoryMedia(parent)
, _reason(GetReason(call)) {
	if (_parent->out()) {
		_text = lang(_reason == FinishReason::Missed ? lng_call_cancelled : lng_call_outgoing);
	} else if (_reason == FinishReason::Missed) {
		_text = lang(lng_call_missed);
	} else if (_reason == FinishReason::Busy) {
		_text = lang(lng_call_declined);
	} else {
		_text = lang(lng_call_incoming);
	}
	_duration = call.has_duration() ? call.vduration.v : 0;

	_status = _parent->date.time().toString(cTimeFormat());
	if (_duration) {
		if (_reason != FinishReason::Missed && _reason != FinishReason::Busy) {
			_status = lng_call_duration_info(lt_time, _status, lt_duration, formatDurationWords(_duration));
		} else {
			_duration = 0;
		}
	}
}

HistoryCall::FinishReason HistoryCall::GetReason(const MTPDmessageActionPhoneCall &call) {
	if (call.has_reason()) {
		switch (call.vreason.type()) {
		case mtpc_phoneCallDiscardReasonBusy: return FinishReason::Busy;
		case mtpc_phoneCallDiscardReasonDisconnect: return FinishReason::Disconnected;
		case mtpc_phoneCallDiscardReasonHangup: return FinishReason::Hangup;
		case mtpc_phoneCallDiscardReasonMissed: return FinishReason::Missed;
		}
		Unexpected("Call reason type.");
	}
	return FinishReason::Hangup;
}

QSize HistoryCall::countOptimalSize() {
	_link = std::make_shared<LambdaClickHandler>([peer = _parent->history()->peer] {
		if (auto user = peer->asUser()) {
			Calls::Current().startOutgoingCall(user);
		}
	});

	auto maxWidth = st::historyCallWidth;
	auto minHeight = st::historyCallHeight;
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

void HistoryCall::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	auto outbg = _parent->hasOutLayout();
	auto selected = (selection == FullSelection);

	accumulate_min(paintw, maxWidth());

	auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;

	nameleft = st::historyCallLeft;
	nametop = st::historyCallTop - topMinus;
	nameright = st::msgFilePadding.left();
	statustop = st::historyCallStatusTop - topMinus;

	auto namewidth = paintw - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
	p.drawTextLeft(nameleft, nametop, paintw, _text);

	auto statusleft = nameleft;
	auto missed = (_reason == FinishReason::Missed || _reason == FinishReason::Busy);
	auto &arrow = outbg ? (selected ? st::historyCallArrowOutSelected : st::historyCallArrowOut) : missed ? (selected ? st::historyCallArrowMissedInSelected : st::historyCallArrowMissedIn) : (selected ? st::historyCallArrowInSelected : st::historyCallArrowIn);
	arrow.paint(p, statusleft + st::historyCallArrowPosition.x(), statustop + st::historyCallArrowPosition.y(), paintw);
	statusleft += arrow.width() + st::historyCallStatusSkip;

	auto &statusFg = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(statusFg);
	p.drawTextLeft(statusleft, statustop, paintw, _status);

	auto &icon = outbg ? (selected ? st::historyCallOutIconSelected : st::historyCallOutIcon) : (selected ? st::historyCallInIconSelected : st::historyCallInIcon);
	icon.paint(p, paintw - st::historyCallIconPosition.x() - icon.width(), st::historyCallIconPosition.y() - topMinus, paintw);
}

HistoryTextState HistoryCall::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);
	if (QRect(0, 0, width(), height()).contains(point)) {
		result.link = _link;
		return result;
	}
	return result;
}

QString HistoryCall::notificationText() const {
	auto result = _text;
	if (_duration > 0) {
		result = lng_call_type_and_duration(lt_type, result, lt_duration, formatDurationWords(_duration));
	}
	return result;
}

TextWithEntities HistoryCall::selectedText(TextSelection selection) const {
	if (selection != FullSelection) {
		return TextWithEntities();
	}
	return { qsl("[ ") + _text + qsl(" ]"), EntitiesInText() };
}

namespace {

int articleThumbWidth(PhotoData *thumb, int height) {
	auto w = thumb->medium->width();
	auto h = thumb->medium->height();
	return qMax(qMin(height * w / h, height), 1);
}

int articleThumbHeight(PhotoData *thumb, int width) {
	return qMax(thumb->medium->height() * width / thumb->medium->width(), 1);
}

int unitedLineHeight() {
	return qMax(st::webPageTitleFont->height, st::webPageDescriptionFont->height);
}

} // namespace

HistoryWebPage::HistoryWebPage(not_null<HistoryItem*> parent, not_null<WebPageData*> data) : HistoryMedia(parent)
, _data(data)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft) {
}

HistoryWebPage::HistoryWebPage(
	not_null<HistoryItem*> parent,
	const HistoryWebPage &other)
: HistoryMedia(parent)
, _data(other._data)
, _attach(other._attach ? other._attach->clone(parent, parent) : nullptr)
, _asArticle(other._asArticle)
, _title(other._title)
, _description(other._description)
, _siteNameWidth(other._siteNameWidth)
, _durationWidth(other._durationWidth)
, _pixw(other._pixw)
, _pixh(other._pixh) {
}

QSize HistoryWebPage::countOptimalSize() {
	if (_data->pendingTill) {
		return { 0, 0 };
	}
	const auto versionChanged = (_dataVersion != _data->version);
	if (versionChanged) {
		_dataVersion = _data->version;
		_openl = nullptr;
		if (_attach) {
			_attach->detachFromParent();
			_attach = nullptr;
		}
		_title = Text(st::msgMinWidth - st::webPageLeft);
		_description = Text(st::msgMinWidth - st::webPageLeft);
		_siteNameWidth = 0;
	}
	auto lineHeight = unitedLineHeight();

	if (!_openl && !_data->url.isEmpty()) {
		_openl = std::make_shared<UrlClickHandler>(_data->url, true);
	}

	// init layout
	auto title = TextUtilities::SingleLine(_data->title.isEmpty() ? _data->author : _data->title);
	if (!_data->document && _data->photo && _data->type != WebPagePhoto && _data->type != WebPageVideo) {
		if (_data->type == WebPageProfile) {
			_asArticle = true;
		} else if (_data->siteName == qstr("Twitter") || _data->siteName == qstr("Facebook")) {
			_asArticle = false;
		} else {
			_asArticle = true;
		}
		if (_asArticle && _data->description.text.isEmpty() && title.isEmpty() && _data->siteName.isEmpty()) {
			_asArticle = false;
		}
	} else {
		_asArticle = false;
	}

	// init attach
	if (!_attach && !_asArticle) {
		if (_data->document) {
			if (_data->document->sticker()) {
				_attach = std::make_unique<HistorySticker>(_parent, _data->document);
			} else if (_data->document->isAnimation()) {
				_attach = std::make_unique<HistoryGif>(_parent, _data->document, QString());
			} else if (_data->document->isVideoFile()) {
				_attach = std::make_unique<HistoryVideo>(_parent, _data->document, QString());
			} else {
				_attach = std::make_unique<HistoryDocument>(_parent, _data->document, QString());
			}
		} else if (_data->photo) {
			_attach = std::make_unique<HistoryPhoto>(_parent, _data->photo, QString());
		}
		if (_attach) {
			_attach->attachToParent();
		}
	}

	auto textFloatsAroundInfo = !_asArticle && !_attach && isBubbleBottom();

	// init strings
	if (_description.isEmpty() && !_data->description.text.isEmpty()) {
		auto text = _data->description;

		if (textFloatsAroundInfo) {
			text.text += _parent->skipBlock();
		}
		if (isLogEntryOriginal()) {
			// Fix layout for small bubbles (narrow media caption edit log entries).
			_description = Text(st::minPhotoSize
				- st::msgPadding.left()
				- st::msgPadding.right()
				- st::webPageLeft);
		}
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			text,
			Ui::WebpageTextDescriptionOptions(_data->siteName));
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		if (textFloatsAroundInfo && _description.isEmpty()) {
			title += _parent->skipBlock();
		}
		_title.setText(
			st::webPageTitleStyle,
			title,
			Ui::WebpageTextTitleOptions());
	}
	if (!_siteNameWidth && !_data->siteName.isEmpty()) {
		_siteNameWidth = st::webPageTitleFont->width(_data->siteName);
	}

	// init dimensions
	auto l = st::msgPadding.left() + st::webPageLeft, r = st::msgPadding.right();
	auto skipBlockWidth = _parent->skipBlockWidth();
	auto maxWidth = skipBlockWidth;
	auto minHeight = 0;

	auto siteNameHeight = _data->siteName.isEmpty() ? 0 : lineHeight;
	auto titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	auto descMaxLines = isLogEntryOriginal() ? kMaxOriginalEntryLines : (3 + (siteNameHeight ? 0 : 1) + (titleMinHeight ? 0 : 1));
	auto descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * lineHeight);
	auto articleMinHeight = siteNameHeight + titleMinHeight + descriptionMinHeight;
	auto articlePhotoMaxWidth = 0;
	if (_asArticle) {
		articlePhotoMaxWidth = st::webPagePhotoDelta + qMax(articleThumbWidth(_data->photo, articleMinHeight), lineHeight);
	}

	if (_siteNameWidth) {
		if (_title.isEmpty() && _description.isEmpty()) {
			accumulate_max(maxWidth, _siteNameWidth + _parent->skipBlockWidth());
		} else {
			accumulate_max(maxWidth, _siteNameWidth + articlePhotoMaxWidth);
		}
		minHeight += lineHeight;
	}
	if (!_title.isEmpty()) {
		accumulate_max(maxWidth, _title.maxWidth() + articlePhotoMaxWidth);
		minHeight += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(maxWidth, _description.maxWidth() + articlePhotoMaxWidth);
		minHeight += descriptionMinHeight;
	}
	if (_attach) {
		auto attachAtTop = !_siteNameWidth && _title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) minHeight += st::mediaInBubbleSkip;

		_attach->initDimensions();
		auto bubble = _attach->bubbleMargins();
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(maxWidth, maxMediaWidth);
		minHeight += _attach->minHeight() - bubble.top() - bubble.bottom();
		if (!_attach->additionalInfoString().isEmpty()) {
			minHeight += bottomInfoPadding();
		}
	}
	if (_data->type == WebPageVideo && _data->duration) {
		_duration = formatDurationText(_data->duration);
		_durationWidth = st::msgDateFont->width(_duration);
	}
	maxWidth += st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();
	auto padding = inBubblePadding();
	minHeight += padding.top() + padding.bottom();

	if (_asArticle) {
		minHeight = resizeGetHeight(maxWidth);
	}
	return { maxWidth, minHeight };
}

QSize HistoryWebPage::countCurrentSize(int newWidth) {
	if (_data->pendingTill) {
		return { newWidth, minHeight() };
	}

	auto innerWidth = newWidth - st::msgPadding.left() - st::webPageLeft - st::msgPadding.right();
	auto newHeight = 0;

	auto lineHeight = unitedLineHeight();
	auto linesMax = isLogEntryOriginal() ? kMaxOriginalEntryLines : 5;
	auto siteNameLines = _siteNameWidth ? 1 : 0;
	auto siteNameHeight = _siteNameWidth ? lineHeight : 0;
	if (_asArticle) {
		_pixh = linesMax * lineHeight;
		do {
			_pixw = articleThumbWidth(_data->photo, _pixh);
			auto wleft = innerWidth - st::webPagePhotoDelta - qMax(_pixw, lineHeight);

			newHeight = siteNameHeight;

			if (_title.isEmpty()) {
				_titleLines = 0;
			} else {
				if (_title.countHeight(wleft) < 2 * st::webPageTitleFont->height) {
					_titleLines = 1;
				} else {
					_titleLines = 2;
				}
				newHeight += _titleLines * lineHeight;
			}

			auto descriptionHeight = _description.countHeight(wleft);
			if (descriptionHeight < (linesMax - siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				// We have height for all the lines.
				_descriptionLines = -1;
				newHeight += descriptionHeight;
			} else {
				_descriptionLines = (linesMax - siteNameLines - _titleLines);
				newHeight += _descriptionLines * lineHeight;
			}

			if (newHeight >= _pixh) {
				break;
			}

			_pixh -= lineHeight;
		} while (_pixh > lineHeight);
		newHeight += bottomInfoPadding();
	} else {
		newHeight = siteNameHeight;

		if (_title.isEmpty()) {
			_titleLines = 0;
		} else {
			if (_title.countHeight(innerWidth) < 2 * st::webPageTitleFont->height) {
				_titleLines = 1;
			} else {
				_titleLines = 2;
			}
			newHeight += _titleLines * lineHeight;
		}

		if (_description.isEmpty()) {
			_descriptionLines = 0;
		} else {
			auto descriptionHeight = _description.countHeight(innerWidth);
			if (descriptionHeight < (linesMax - siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				// We have height for all the lines.
				_descriptionLines = -1;
				newHeight += descriptionHeight;
			} else {
				_descriptionLines = (linesMax - siteNameLines - _titleLines);
				newHeight += _descriptionLines * lineHeight;
			}
		}

		if (_attach) {
			auto attachAtTop = !_siteNameWidth && !_titleLines && !_descriptionLines;
			if (!attachAtTop) newHeight += st::mediaInBubbleSkip;

			auto bubble = _attach->bubbleMargins();

			_attach->resizeGetHeight(innerWidth + bubble.left() + bubble.right());
			newHeight += _attach->height() - bubble.top() - bubble.bottom();
			if (!_attach->additionalInfoString().isEmpty()) {
				newHeight += bottomInfoPadding();
			} else if (isBubbleBottom() && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > innerWidth + bubble.left() + bubble.right()) {
				newHeight += bottomInfoPadding();
			}
		}
	}
	auto padding = inBubblePadding();
	newHeight += padding.top() + padding.bottom();

	return { newWidth, newHeight };
}

TextSelection HistoryWebPage::toDescriptionSelection(TextSelection selection) const {
	return internal::unshiftSelection(selection, _title);
}

TextSelection HistoryWebPage::fromDescriptionSelection(TextSelection selection) const {
	return internal::shiftSelection(selection, _title);
}

void HistoryWebPage::refreshParentId(not_null<HistoryItem*> realParent) {
	if (_attach) {
		_attach->refreshParentId(realParent);
	}
}

void HistoryWebPage::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	auto &barfg = selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
	auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	paintw -= padding.left() + padding.right();
	auto attachAdditionalInfoText = _attach ? _attach->additionalInfoString() : QString();
	if (_asArticle) {
		bshift += bottomInfoPadding();
	} else if (!attachAdditionalInfoText.isEmpty()) {
		bshift += bottomInfoPadding();
	} else if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}

	QRect bar(rtlrect(st::msgPadding.left(), tshift, st::webPageBar, height() - tshift - bshift, width()));
	p.fillRect(bar, barfg);

	auto lineHeight = unitedLineHeight();
	if (_asArticle) {
		_data->photo->medium->load(false, false);
		bool full = _data->photo->medium->loaded();
		QPixmap pix;
		auto pw = qMax(_pixw, lineHeight);
		auto ph = _pixh;
		auto pixw = _pixw, pixh = articleThumbHeight(_data->photo, _pixw);
		auto maxw = convertScale(_data->photo->medium->width()), maxh = convertScale(_data->photo->medium->height());
		if (pixw * ph != pixh * pw) {
			float64 coef = (pixw * ph > pixh * pw) ? qMin(ph / float64(pixh), maxh / float64(pixh)) : qMin(pw / float64(pixw), maxw / float64(pixw));
			pixh = qRound(pixh * coef);
			pixw = qRound(pixw * coef);
		}
		if (full) {
			pix = _data->photo->medium->pixSingle(pixw, pixh, pw, ph, ImageRoundRadius::Small);
		} else {
			pix = _data->photo->thumb->pixBlurredSingle(pixw, pixh, pw, ph, ImageRoundRadius::Small);
		}
		p.drawPixmapLeft(padding.left() + paintw - pw, tshift, width(), pix);
		if (selected) {
			App::roundRect(p, rtlrect(padding.left() + paintw - pw, tshift, pw, _pixh, width()), p.textPalette().selectOverlay, SelectedOverlaySmallCorners);
		}
		paintw -= pw + st::webPagePhotoDelta;
	}
	if (_siteNameWidth) {
		p.setFont(st::webPageTitleFont);
		p.setPen(semibold);
		p.drawTextLeft(padding.left(), tshift, width(), (paintw >= _siteNameWidth) ? _data->siteName : st::webPageTitleFont->elided(_data->siteName, paintw));
		tshift += lineHeight;
	}
	if (_titleLines) {
		p.setPen(outbg ? st::webPageTitleOutFg : st::webPageTitleInFg);
		auto endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, paintw, width(), _titleLines, style::al_left, 0, -1, endskip, false, selection);
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		auto endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		if (_descriptionLines > 0) {
			_description.drawLeftElided(p, padding.left(), tshift, paintw, width(), _descriptionLines, style::al_left, 0, -1, endskip, false, toDescriptionSelection(selection));
			tshift += _descriptionLines * lineHeight;
		} else {
			_description.drawLeft(p, padding.left(), tshift, paintw, width(), style::al_left, 0, -1, toDescriptionSelection(selection));
			tshift += _description.countHeight(paintw);
		}
	}
	if (_attach) {
		auto attachAtTop = !_siteNameWidth && !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		p.translate(attachLeft, attachTop);

		auto attachSelection = selected ? FullSelection : TextSelection { 0, 0 };
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		auto pixwidth = _attach->width();
		auto pixheight = _attach->height();

		if (_data->type == WebPageVideo && _attach->type() == MediaTypePhoto) {
			if (_attach->isReadyForOpen()) {
				if (_data->siteName == qstr("YouTube")) {
					st::youtubeIcon.paint(p, (pixwidth - st::youtubeIcon.width()) / 2, (pixheight - st::youtubeIcon.height()) / 2, width());
				} else {
					st::videoIcon.paint(p, (pixwidth - st::videoIcon.width()) / 2, (pixheight - st::videoIcon.height()) / 2, width());
				}
			}
			if (_durationWidth) {
				auto dateX = pixwidth - _durationWidth - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
				auto dateY = pixheight - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
				auto dateW = pixwidth - dateX - st::msgDateImgDelta;
				auto dateH = pixheight - dateY - st::msgDateImgDelta;

				App::roundRect(p, dateX, dateY, dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

				p.setFont(st::msgDateFont);
				p.setPen(st::msgDateImgFg);
				p.drawTextLeft(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y(), pixwidth, _duration);
			}
		}

		p.translate(-attachLeft, -attachTop);

		if (!attachAdditionalInfoText.isEmpty()) {
			p.setFont(st::msgDateFont);
			p.setPen(selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg));
			p.drawTextLeft(st::msgPadding.left(), bar.y() + bar.height() + st::mediaInBubbleSkip, width(), attachAdditionalInfoText);
		}
	}
}

HistoryTextState HistoryWebPage::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	if (_asArticle || (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right())) {
		bshift += bottomInfoPadding();
	}
	paintw -= padding.left() + padding.right();

	auto lineHeight = unitedLineHeight();
	auto inThumb = false;
	if (_asArticle) {
		auto pw = qMax(_pixw, lineHeight);
		if (rtlrect(padding.left() + paintw - pw, 0, pw, _pixh, width()).contains(point)) {
			inThumb = true;
		}
		paintw -= pw + st::webPagePhotoDelta;
	}
	int symbolAdd = 0;
	if (_siteNameWidth) {
		tshift += lineHeight;
	}
	if (_titleLines) {
		if (point.y() >= tshift && point.y() < tshift + _titleLines * lineHeight) {
			Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleLines;
			result = HistoryTextState(_parent, _title.getStateElidedLeft(
				point - QPoint(padding.left(), tshift),
				paintw,
				width(),
				titleRequest));
		} else if (point.y() >= tshift + _titleLines * lineHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		auto descriptionHeight = (_descriptionLines > 0) ? _descriptionLines * lineHeight : _description.countHeight(paintw);
		if (point.y() >= tshift && point.y() < tshift + descriptionHeight) {
			if (_descriptionLines > 0) {
				Text::StateRequestElided descriptionRequest = request.forText();
				descriptionRequest.lines = _descriptionLines;
				result = HistoryTextState(_parent, _description.getStateElidedLeft(
					point - QPoint(padding.left(), tshift),
					paintw,
					width(),
					descriptionRequest));
			} else {
				result = HistoryTextState(_parent, _description.getStateLeft(
					point - QPoint(padding.left(), tshift),
					paintw,
					width(),
					request.forText()));
			}
		} else if (point.y() >= tshift + descriptionHeight) {
			symbolAdd += _description.length();
		}
		tshift += descriptionHeight;
	}
	if (inThumb) {
		result.link = _openl;
	} else if (_attach) {
		auto attachAtTop = !_siteNameWidth && !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		if (QRect(padding.left(), tshift, paintw, height() - tshift - bshift).contains(point)) {
			auto attachLeft = padding.left() - bubble.left();
			auto attachTop = tshift - bubble.top();
			if (rtl()) attachLeft = width() - attachLeft - _attach->width();
			result = _attach->getState(point - QPoint(attachLeft, attachTop), request);

			if (result.link && !_data->document && _data->photo && _attach->isReadyForOpen()) {
				if (_data->type == WebPageProfile || _data->type == WebPageVideo) {
					result.link = _openl;
				} else if (_data->type == WebPagePhoto || _data->siteName == qstr("Twitter") || _data->siteName == qstr("Facebook")) {
					// leave photo link
				} else {
					result.link = _openl;
				}
			}
		}
	}

	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryWebPage::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionLines || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

void HistoryWebPage::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void HistoryWebPage::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}
bool HistoryWebPage::isDisplayed() const {
	return !_data->pendingTill
		&& !_parent->Has<HistoryMessageLogEntryOriginal>();
}

void HistoryWebPage::attachToParent() {
	App::regWebPageItem(_data, _parent);
	if (_attach) _attach->attachToParent();
}

void HistoryWebPage::detachFromParent() {
	App::unregWebPageItem(_data, _parent);
	if (_attach) _attach->detachFromParent();
}

TextWithEntities HistoryWebPage::selectedText(TextSelection selection) const {
	if (selection == FullSelection && !isLogEntryOriginal()) {
		return TextWithEntities();
	}
	auto titleResult = _title.originalTextWithEntities((selection == FullSelection) ? AllTextSelection : selection, ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection((selection == FullSelection) ? AllTextSelection : selection), ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	TextUtilities::Append(titleResult, std::move(descriptionResult));
	return titleResult;
}

bool HistoryWebPage::hasReplyPreview() const {
	return _attach ? _attach->hasReplyPreview() : (_data->photo ? true : false);
}

ImagePtr HistoryWebPage::replyPreview() {
	return _attach ? _attach->replyPreview() : (_data->photo ? _data->photo->makeReplyPreview() : ImagePtr());
}

QMargins HistoryWebPage::inBubblePadding() const {
	auto lshift = st::msgPadding.left() + st::webPageLeft;
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

bool HistoryWebPage::isLogEntryOriginal() const {
	return _parent->isLogEntry() && _parent->getMedia() != this;
}

int HistoryWebPage::bottomInfoPadding() const {
	if (!isBubbleBottom()) return 0;

	auto result = st::msgDateFont->height;

	// We use padding greater than st::msgPadding.bottom() in the
	// bottom of the bubble so that the left line looks pretty.
	// but if we have bottom skip because of the info display
	// we don't need that additional padding so we replace it
	// back with st::msgPadding.bottom() instead of left().
	result += st::msgPadding.bottom() - st::msgPadding.left();
	return result;
}

HistoryGame::HistoryGame(
	not_null<HistoryItem*> parent,
	not_null<GameData*> data)
: HistoryMedia(parent)
, _data(data)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft) {
}

HistoryGame::HistoryGame(
	not_null<HistoryItem*> parent,
	const HistoryGame &other)
: HistoryMedia(parent)
, _data(other._data)
, _attach(other._attach ? other._attach->clone(parent, parent) : nullptr)
, _title(other._title)
, _description(other._description) {
}

QSize HistoryGame::countOptimalSize() {
	auto lineHeight = unitedLineHeight();

	if (!_openl && IsServerMsgId(_parent->id)) {
		const auto row = 0;
		const auto column = 0;
		_openl = std::make_shared<ReplyMarkupClickHandler>(
			row,
			column,
			_parent->fullId());
	}

	auto title = TextUtilities::SingleLine(_data->title);

	// init attach
	if (!_attach) {
		if (_data->document) {
			if (_data->document->sticker()) {
				_attach = std::make_unique<HistorySticker>(_parent, _data->document);
			} else if (_data->document->isAnimation()) {
				_attach = std::make_unique<HistoryGif>(_parent, _data->document, QString());
			} else if (_data->document->isVideoFile()) {
				_attach = std::make_unique<HistoryVideo>(_parent, _data->document, QString());
			} else {
				_attach = std::make_unique<HistoryDocument>(_parent, _data->document, QString());
			}
		} else if (_data->photo) {
			_attach = std::make_unique<HistoryPhoto>(_parent, _data->photo, QString());
		}
	}

	// init strings
	if (_description.isEmpty() && !_data->description.isEmpty()) {
		auto text = _data->description;
		if (!text.isEmpty()) {
			if (!_attach) {
				text += _parent->skipBlock();
			}
			auto marked = TextWithEntities { text };
			auto parseFlags = TextParseLinks | TextParseMultiline | TextParseRichText;
			TextUtilities::ParseEntities(marked, parseFlags);
			_description.setMarkedText(
				st::webPageDescriptionStyle,
				marked,
				Ui::WebpageTextDescriptionOptions());
		}
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			title,
			Ui::WebpageTextTitleOptions());
	}

	// init dimensions
	auto l = st::msgPadding.left() + st::webPageLeft, r = st::msgPadding.right();
	auto skipBlockWidth = _parent->skipBlockWidth();
	auto maxWidth = skipBlockWidth;
	auto minHeight = 0;

	auto titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	// enable any count of lines in game description / message
	auto descMaxLines = 4096;
	auto descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * lineHeight);

	if (!_title.isEmpty()) {
		accumulate_max(maxWidth, _title.maxWidth());
		minHeight += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(maxWidth, _description.maxWidth());
		minHeight += descriptionMinHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) minHeight += st::mediaInBubbleSkip;

		_attach->initDimensions();
		QMargins bubble(_attach->bubbleMargins());
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(maxWidth, maxMediaWidth);
		minHeight += _attach->minHeight() - bubble.top() - bubble.bottom();
	}
	maxWidth += st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();
	auto padding = inBubblePadding();
	minHeight += padding.top() + padding.bottom();

	if (!_gameTagWidth) {
		_gameTagWidth = st::msgDateFont->width(lang(lng_game_tag).toUpper());
	}
	return { maxWidth, minHeight };
}

void HistoryGame::refreshParentId(not_null<HistoryItem*> realParent) {
	if (_openl) {
		_openl->setMessageId(_parent->fullId());
	}
	if (_attach) {
		_attach->refreshParentId(realParent);
	}
}

QSize HistoryGame::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	auto innerWidth = newWidth - st::msgPadding.left() - st::webPageLeft - st::msgPadding.right();

	// enable any count of lines in game description / message
	auto linesMax = 4096;
	auto lineHeight = unitedLineHeight();
	auto newHeight = 0;
	if (_title.isEmpty()) {
		_titleLines = 0;
	} else {
		if (_title.countHeight(innerWidth) < 2 * st::webPageTitleFont->height) {
			_titleLines = 1;
		} else {
			_titleLines = 2;
		}
		newHeight += _titleLines * lineHeight;
	}

	if (_description.isEmpty()) {
		_descriptionLines = 0;
	} else {
		auto descriptionHeight = _description.countHeight(innerWidth);
		if (descriptionHeight < (linesMax - _titleLines) * st::webPageDescriptionFont->height) {
			_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
		} else {
			_descriptionLines = (linesMax - _titleLines);
		}
		newHeight += _descriptionLines * lineHeight;
	}

	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) newHeight += st::mediaInBubbleSkip;

		QMargins bubble(_attach->bubbleMargins());

		_attach->resizeGetHeight(innerWidth + bubble.left() + bubble.right());
		newHeight += _attach->height() - bubble.top() - bubble.bottom();
		if (isBubbleBottom() && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > innerWidth + bubble.left() + bubble.right()) {
			newHeight += bottomInfoPadding();
		}
	}
	auto padding = inBubblePadding();
	newHeight += padding.top() + padding.bottom();

	return { newWidth, newHeight };
}

TextSelection HistoryGame::toDescriptionSelection(TextSelection selection) const {
	return internal::unshiftSelection(selection, _title);
}

TextSelection HistoryGame::fromDescriptionSelection(TextSelection selection) const {
	return internal::shiftSelection(selection, _title);
}

void HistoryGame::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width(), painth = height();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	auto &barfg = selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
	auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	paintw -= padding.left() + padding.right();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}

	QRect bar(rtlrect(st::msgPadding.left(), tshift, st::webPageBar, height() - tshift - bshift, width()));
	p.fillRect(bar, barfg);

	auto lineHeight = unitedLineHeight();
	if (_titleLines) {
		p.setPen(semibold);
		auto endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, paintw, width(), _titleLines, style::al_left, 0, -1, endskip, false, selection);
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		auto endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_description.drawLeftElided(p, padding.left(), tshift, paintw, width(), _descriptionLines, style::al_left, 0, -1, endskip, false, toDescriptionSelection(selection));
		tshift += _descriptionLines * lineHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		auto attachSelection = selected ? FullSelection : TextSelection { 0, 0 };

		p.translate(attachLeft, attachTop);
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		auto pixwidth = _attach->width();
		auto pixheight = _attach->height();

		auto gameW = _gameTagWidth + 2 * st::msgDateImgPadding.x();
		auto gameH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		auto gameX = pixwidth - st::msgDateImgDelta - gameW;
		auto gameY = pixheight - st::msgDateImgDelta - gameH;

		App::roundRect(p, rtlrect(gameX, gameY, gameW, gameH, pixwidth), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

		p.setFont(st::msgDateFont);
		p.setPen(st::msgDateImgFg);
		p.drawTextLeft(gameX + st::msgDateImgPadding.x(), gameY + st::msgDateImgPadding.y(), pixwidth, lang(lng_game_tag).toUpper());

		p.translate(-attachLeft, -attachTop);
	}
}

HistoryTextState HistoryGame::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintw = width(), painth = height();

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}
	paintw -= padding.left() + padding.right();

	auto inThumb = false;
	auto symbolAdd = 0;
	auto lineHeight = unitedLineHeight();
	if (_titleLines) {
		if (point.y() >= tshift && point.y() < tshift + _titleLines * lineHeight) {
			Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleLines;
			result = HistoryTextState(_parent, _title.getStateElidedLeft(
				point - QPoint(padding.left(), tshift),
				paintw,
				width(),
				titleRequest));
		} else if (point.y() >= tshift + _titleLines * lineHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		if (point.y() >= tshift && point.y() < tshift + _descriptionLines * lineHeight) {
			Text::StateRequestElided descriptionRequest = request.forText();
			descriptionRequest.lines = _descriptionLines;
			result = HistoryTextState(_parent, _description.getStateElidedLeft(
				point - QPoint(padding.left(), tshift),
				paintw,
				width(),
				descriptionRequest));
		} else if (point.y() >= tshift + _descriptionLines * lineHeight) {
			symbolAdd += _description.length();
		}
		tshift += _descriptionLines * lineHeight;
	}
	if (inThumb) {
		if (!_parent->isLogEntry()) {
			result.link = _openl;
		}
	} else if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		if (QRect(attachLeft, tshift, _attach->width(), height() - tshift - bshift).contains(point)) {
			if (_attach->isReadyForOpen()) {
				if (!_parent->isLogEntry()) {
					result.link = _openl;
				}
			} else {
				result = _attach->getState(point - QPoint(attachLeft, attachTop), request);
			}
		}
	}

	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryGame::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionLines || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

bool HistoryGame::consumeMessageText(const TextWithEntities &textWithEntities) {
	_description.setMarkedText(
		st::webPageDescriptionStyle,
		textWithEntities,
		Ui::ItemTextOptions(_parent));
	return true;
}

void HistoryGame::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void HistoryGame::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

void HistoryGame::attachToParent() {
	App::regGameItem(_data, _parent);
	if (_attach) _attach->attachToParent();
}

void HistoryGame::detachFromParent() {
	App::unregGameItem(_data, _parent);
	if (_attach) _attach->detachFromParent();
}

void HistoryGame::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaGame) {
		auto &game = media.c_messageMediaGame().vgame;
		if (game.type() == mtpc_game) {
			App::feedGame(game.c_game(), _data);
		}
	}
}

bool HistoryGame::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	updateSentMedia(media);
	return false;
}

QString HistoryGame::notificationText() const {
	QString result; // add a game controller emoji before game title
	result.reserve(_data->title.size() + 3);
	result.append(QChar(0xD83C)).append(QChar(0xDFAE)).append(QChar(' ')).append(_data->title);
	return result;
}

TextWithEntities HistoryGame::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		return TextWithEntities();
	}
	auto titleResult = _title.originalTextWithEntities(selection, ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection), ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	TextUtilities::Append(titleResult, std::move(descriptionResult));
	return titleResult;
}

ImagePtr HistoryGame::replyPreview() {
	return _attach ? _attach->replyPreview() : (_data->photo ? _data->photo->makeReplyPreview() : ImagePtr());
}

QMargins HistoryGame::inBubblePadding() const {
	auto lshift = st::msgPadding.left() + st::webPageLeft;
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

int HistoryGame::bottomInfoPadding() const {
	if (!isBubbleBottom()) return 0;

	auto result = st::msgDateFont->height;

	// we use padding greater than st::msgPadding.bottom() in the
	// bottom of the bubble so that the left line looks pretty.
	// but if we have bottom skip because of the info display
	// we don't need that additional padding so we replace it
	// back with st::msgPadding.bottom() instead of left().
	result += st::msgPadding.bottom() - st::msgPadding.left();
	return result;
}

HistoryInvoice::HistoryInvoice(
	not_null<HistoryItem*> parent,
	const MTPDmessageMediaInvoice &data)
: HistoryMedia(parent)
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _status(st::msgMinWidth) {
	fillFromData(data);
}

HistoryInvoice::HistoryInvoice(
	not_null<HistoryItem*> parent,
	const HistoryInvoice &other)
: HistoryMedia(parent)
, _attach(other._attach ? other._attach->clone(parent, parent) : nullptr)
, _titleHeight(other._titleHeight)
, _descriptionHeight(other._descriptionHeight)
, _title(other._title)
, _description(other._description)
, _status(other._status) {
}

QString HistoryInvoice::fillAmountAndCurrency(uint64 amount, const QString &currency) {
	static auto shortCurrencyNames = QMap<QString, QString> {
		{ qsl("USD"), QString::fromUtf8("\x24") },
		{ qsl("GBP"), QString::fromUtf8("\xC2\xA3") },
		{ qsl("EUR"), QString::fromUtf8("\xE2\x82\xAC") },
		{ qsl("JPY"), QString::fromUtf8("\xC2\xA5") },
	};
	auto currencyText = shortCurrencyNames.value(currency, currency);
	return QLocale::system().toCurrencyString(amount / 100., currencyText);
	//auto amountBucks = amount / 100;
	//auto amountCents = amount % 100;
	//auto amountText = qsl("%1,%2").arg(amountBucks).arg(amountCents, 2, 10, QChar('0'));
	//return currencyText + amountText;
}

void HistoryInvoice::fillFromData(const MTPDmessageMediaInvoice &data) {
	// init attach
	if (data.has_photo() && data.vphoto.type() == mtpc_webDocument) {
		auto &doc = data.vphoto.c_webDocument();
		auto imageSize = QSize();
		for (auto &attribute : doc.vattributes.v) {
			if (attribute.type() == mtpc_documentAttributeImageSize) {
				auto &size = attribute.c_documentAttributeImageSize();
				imageSize = QSize(size.vw.v, size.vh.v);
				break;
			}
		}
		if (!imageSize.isEmpty()) {
			auto thumbsize = shrinkToKeepAspect(imageSize.width(), imageSize.height(), 100, 100);
			auto thumb = ImagePtr(thumbsize.width(), thumbsize.height());

			auto mediumsize = shrinkToKeepAspect(imageSize.width(), imageSize.height(), 320, 320);
			auto medium = ImagePtr(mediumsize.width(), mediumsize.height());

			// We don't use size from WebDocument, because it is not reliable.
			// It can be > 0 and different from the real size that we get in upload.WebFile result.
			auto filesize = 0; // doc.vsize.v;
			auto full = ImagePtr(WebFileImageLocation(imageSize.width(), imageSize.height(), doc.vdc_id.v, doc.vurl.v, doc.vaccess_hash.v), filesize);
			auto photoId = rand_value<PhotoId>();
			auto photo = App::photoSet(photoId, 0, 0, unixtime(), thumb, medium, full);

			_attach = std::make_unique<HistoryPhoto>(_parent, photo, QString());
		}
	}
	auto labelText = [&data] {
		if (data.has_receipt_msg_id()) {
			if (data.is_test()) {
				return lang(lng_payments_receipt_label_test);
			}
			return lang(lng_payments_receipt_label);
		} else if (data.is_test()) {
			return lang(lng_payments_invoice_label_test);
		}
		return lang(lng_payments_invoice_label);
	};
	auto statusText = TextWithEntities {
		fillAmountAndCurrency(data.vtotal_amount.v, qs(data.vcurrency)),
		EntitiesInText()
	};
	statusText.entities.push_back(EntityInText(EntityInTextBold, 0, statusText.text.size()));
	statusText.text += ' ' + labelText().toUpper();
	_status.setMarkedText(
		st::defaultTextStyle,
		statusText,
		Ui::ItemTextOptions(_parent));

	_receiptMsgId = data.has_receipt_msg_id() ? data.vreceipt_msg_id.v : 0;

	// init strings
	auto description = qs(data.vdescription);
	if (!description.isEmpty()) {
		auto marked = TextWithEntities { description };
		auto parseFlags = TextParseLinks | TextParseMultiline | TextParseRichText;
		TextUtilities::ParseEntities(marked, parseFlags);
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			marked,
			Ui::WebpageTextDescriptionOptions());
	}
	auto title = TextUtilities::SingleLine(qs(data.vtitle));
	if (!title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			title,
			Ui::WebpageTextTitleOptions());
	}
}

QSize HistoryInvoice::countOptimalSize() {
	auto lineHeight = unitedLineHeight();

	if (_attach) {
		if (_status.hasSkipBlock()) {
			_status.removeSkipBlock();
		}
	} else if (!_status.hasSkipBlock()) {
		_status.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	// init dimensions
	auto l = st::msgPadding.left(), r = st::msgPadding.right();
	auto skipBlockWidth = _parent->skipBlockWidth();
	auto maxWidth = skipBlockWidth;
	auto minHeight = 0;

	auto titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	// enable any count of lines in game description / message
	auto descMaxLines = 4096;
	auto descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * lineHeight);

	if (!_title.isEmpty()) {
		accumulate_max(maxWidth, _title.maxWidth());
		minHeight += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(maxWidth, _description.maxWidth());
		minHeight += descriptionMinHeight;
	}
	if (_attach) {
		auto attachAtTop = _title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) minHeight += st::mediaInBubbleSkip;

		_attach->initDimensions();
		auto bubble = _attach->bubbleMargins();
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(maxWidth, maxMediaWidth);
		minHeight += _attach->minHeight() - bubble.top() - bubble.bottom();
	} else {
		accumulate_max(maxWidth, _status.maxWidth());
		minHeight += st::mediaInBubbleSkip + _status.minHeight();
	}
	auto padding = inBubblePadding();
	maxWidth += padding.left() + padding.right();
	minHeight += padding.top() + padding.bottom();
	return { maxWidth, minHeight };
}

QSize HistoryInvoice::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	auto innerWidth = newWidth - st::msgPadding.left() - st::msgPadding.right();

	auto lineHeight = unitedLineHeight();

	auto newHeight = 0;
	if (_title.isEmpty()) {
		_titleHeight = 0;
	} else {
		if (_title.countHeight(innerWidth) < 2 * st::webPageTitleFont->height) {
			_titleHeight = lineHeight;
		} else {
			_titleHeight = 2 * lineHeight;
		}
		newHeight += _titleHeight;
	}

	if (_description.isEmpty()) {
		_descriptionHeight = 0;
	} else {
		_descriptionHeight = _description.countHeight(innerWidth);
		newHeight += _descriptionHeight;
	}

	if (_attach) {
		auto attachAtTop = !_title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) newHeight += st::mediaInBubbleSkip;

		QMargins bubble(_attach->bubbleMargins());

		_attach->resizeGetHeight(innerWidth + bubble.left() + bubble.right());
		newHeight += _attach->height() - bubble.top() - bubble.bottom();
		if (isBubbleBottom() && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > innerWidth + bubble.left() + bubble.right()) {
			newHeight += bottomInfoPadding();
		}
	} else {
		newHeight += st::mediaInBubbleSkip + _status.countHeight(innerWidth);
	}
	auto padding = inBubblePadding();
	newHeight += padding.top() + padding.bottom();

	return { newWidth, newHeight };
}

TextSelection HistoryInvoice::toDescriptionSelection(TextSelection selection) const {
	return internal::unshiftSelection(selection, _title);
}

TextSelection HistoryInvoice::fromDescriptionSelection(TextSelection selection) const {
	return internal::shiftSelection(selection, _title);
}

void HistoryInvoice::refreshParentId(not_null<HistoryItem*> realParent) {
	if (_attach) {
		_attach->refreshParentId(realParent);
	}
}

void HistoryInvoice::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width(), painth = height();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	auto &barfg = selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
	auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	paintw -= padding.left() + padding.right();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}

	auto lineHeight = unitedLineHeight();
	if (_titleHeight) {
		p.setPen(semibold);
		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outSemiboldPalette : st::inSemiboldPalette));

		auto endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, paintw, width(), _titleHeight / lineHeight, style::al_left, 0, -1, endskip, false, selection);
		tshift += _titleHeight;

		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));
	}
	if (_descriptionHeight) {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		_description.drawLeft(p, padding.left(), tshift, paintw, width(), style::al_left, 0, -1, toDescriptionSelection(selection));
		tshift += _descriptionHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleHeight && !_descriptionHeight;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		auto attachSelection = selected ? FullSelection : TextSelection { 0, 0 };

		p.translate(attachLeft, attachTop);
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		auto pixwidth = _attach->width();
		auto pixheight = _attach->height();

		auto available = _status.maxWidth();
		auto statusW = available + 2 * st::msgDateImgPadding.x();
		auto statusH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		auto statusX = st::msgDateImgDelta;
		auto statusY = st::msgDateImgDelta;

		App::roundRect(p, rtlrect(statusX, statusY, statusW, statusH, pixwidth), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

		p.setFont(st::msgDateFont);
		p.setPen(st::msgDateImgFg);
		_status.drawLeftElided(p, statusX + st::msgDateImgPadding.x(), statusY + st::msgDateImgPadding.y(), available, pixwidth);

		p.translate(-attachLeft, -attachTop);
	} else {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		_status.drawLeft(p, padding.left(), tshift + st::mediaInBubbleSkip, paintw, width());
	}
}

HistoryTextState HistoryInvoice::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintw = width(), painth = height();

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}
	paintw -= padding.left() + padding.right();

	auto lineHeight = unitedLineHeight();
	auto symbolAdd = 0;
	if (_titleHeight) {
		if (point.y() >= tshift && point.y() < tshift + _titleHeight) {
			Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleHeight / lineHeight;
			result = HistoryTextState(_parent, _title.getStateElidedLeft(
				point - QPoint(padding.left(), tshift),
				paintw,
				width(),
				titleRequest));
		} else if (point.y() >= tshift + _titleHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleHeight;
	}
	if (_descriptionHeight) {
		if (point.y() >= tshift && point.y() < tshift + _descriptionHeight) {
			result = HistoryTextState(_parent, _description.getStateLeft(
				point - QPoint(padding.left(), tshift),
				paintw,
				width(),
				request.forText()));
		} else if (point.y() >= tshift + _descriptionHeight) {
			symbolAdd += _description.length();
		}
		tshift += _descriptionHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleHeight && !_descriptionHeight;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		if (QRect(attachLeft, tshift, _attach->width(), height() - tshift - bshift).contains(point)) {
			result = _attach->getState(point - QPoint(attachLeft, attachTop), request);
		}
	}

	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryInvoice::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionHeight || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

void HistoryInvoice::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void HistoryInvoice::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

void HistoryInvoice::attachToParent() {
	if (_attach) _attach->attachToParent();
}

void HistoryInvoice::detachFromParent() {
	if (_attach) _attach->detachFromParent();
}

QString HistoryInvoice::notificationText() const {
	return _title.originalText();
}

TextWithEntities HistoryInvoice::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		return TextWithEntities();
	}
	auto titleResult = _title.originalTextWithEntities(selection, ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection), ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	TextUtilities::Append(titleResult, std::move(descriptionResult));
	return titleResult;
}

QMargins HistoryInvoice::inBubblePadding() const {
	auto lshift = st::msgPadding.left();
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.top() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.bottom() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

int HistoryInvoice::bottomInfoPadding() const {
	if (!isBubbleBottom()) return 0;

	auto result = st::msgDateFont->height;
	return result;
}

HistoryLocation::HistoryLocation(not_null<HistoryItem*> parent, const LocationCoords &coords, const QString &title, const QString &description) : HistoryMedia(parent)
, _data(App::location(coords))
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _link(std::make_shared<LocationClickHandler>(coords)) {
	if (!title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			TextUtilities::Clean(title),
			Ui::WebpageTextTitleOptions());
	}
	if (!description.isEmpty()) {
		auto marked = TextWithEntities { TextUtilities::Clean(description) };
		auto parseFlags = TextParseLinks | TextParseMultiline | TextParseRichText;
		TextUtilities::ParseEntities(marked, parseFlags);
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			marked,
			Ui::WebpageTextDescriptionOptions());
	}
}

HistoryLocation::HistoryLocation(not_null<HistoryItem*> parent, const HistoryLocation &other) : HistoryMedia(parent)
, _data(other._data)
, _title(other._title)
, _description(other._description)
, _link(std::make_shared<LocationClickHandler>(_data->coords)) {
}

QSize HistoryLocation::countOptimalSize() {
	auto tw = fullWidth();
	auto th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	auto minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	auto maxWidth = qMax(tw, minWidth);
	auto minHeight = qMax(th, st::minPhotoSize);

	if (_parent->hasBubble()) {
		if (!_title.isEmpty()) {
			minHeight += qMin(_title.countHeight(maxWidth - st::msgPadding.left() - st::msgPadding.right()), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			minHeight += qMin(_description.countHeight(maxWidth - st::msgPadding.left() - st::msgPadding.right()), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			minHeight += st::mediaInBubbleSkip;
			if (isBubbleTop()) {
				minHeight += st::msgPadding.top();
			}
		}
	}
	return { maxWidth, minHeight };
}

QSize HistoryLocation::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());

	auto tw = fullWidth();
	auto th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	auto newHeight = th;
	if (tw > newWidth) {
		newHeight = (newWidth * newHeight / tw);
	} else {
		newWidth = tw;
	}
	auto minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	accumulate_max(newWidth, minWidth);
	accumulate_max(newHeight, st::minPhotoSize);
	if (_parent->hasBubble()) {
		if (!_title.isEmpty()) {
			newHeight += qMin(_title.countHeight(newWidth - st::msgPadding.left() - st::msgPadding.right()), st::webPageTitleFont->height * 2);
		}
		if (!_description.isEmpty()) {
			newHeight += qMin(_description.countHeight(newWidth - st::msgPadding.left() - st::msgPadding.right()), st::webPageDescriptionFont->height * 3);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			newHeight += st::mediaInBubbleSkip;
			if (isBubbleTop()) {
				newHeight += st::msgPadding.top();
			}
		}
	}
	return { newWidth, newHeight };
}

TextSelection HistoryLocation::toDescriptionSelection(TextSelection selection) const {
	return internal::unshiftSelection(selection, _title);
}

TextSelection HistoryLocation::fromDescriptionSelection(TextSelection selection) const {
	return internal::shiftSelection(selection, _title);
}

void HistoryLocation::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();
	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	if (bubble) {
		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (isBubbleTop()) {
				painty += st::msgPadding.top();
			}
		}

		auto textw = width() - st::msgPadding.left() - st::msgPadding.right();

		if (!_title.isEmpty()) {
			p.setPen(outbg ? st::webPageTitleOutFg : st::webPageTitleInFg);
			_title.drawLeftElided(p, paintx + st::msgPadding.left(), painty, textw, width(), 2, style::al_left, 0, -1, 0, false, selection);
			painty += qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
			_description.drawLeftElided(p, paintx + st::msgPadding.left(), painty, textw, width(), 3, style::al_left, 0, -1, 0, false, toDescriptionSelection(selection));
			painty += qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			painty += st::mediaInBubbleSkip;
		}
		painth -= painty;
	} else {
		App::roundShadow(p, 0, 0, paintw, painth, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	_data->load();
	auto roundRadius = ImageRoundRadius::Large;
	auto roundCorners = ((isBubbleTop() && _title.isEmpty() && _description.isEmpty()) ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
		| (isBubbleBottom() ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None);
	auto rthumb = QRect(paintx, painty, paintw, painth);
	if (_data && !_data->thumb->isNull()) {
		auto w = _data->thumb->width(), h = _data->thumb->height();
		QPixmap pix;
		if (paintw * h == painth * w || (w == fullWidth() && h == fullHeight())) {
			pix = _data->thumb->pixSingle(paintw, painth, paintw, painth, roundRadius, roundCorners);
		} else if (paintw * h > painth * w) {
			auto nw = painth * w / h;
			pix = _data->thumb->pixSingle(nw, painth, paintw, painth, roundRadius, roundCorners);
		} else {
			auto nh = paintw * h / w;
			pix = _data->thumb->pixSingle(paintw, nh, paintw, painth, roundRadius, roundCorners);
		}
		p.drawPixmap(rthumb.topLeft(), pix);
	} else {
		App::complexLocationRect(p, rthumb, roundRadius, roundCorners);
	}
	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (_parent->getMedia() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
		_parent->drawInfo(p, fullRight, fullBottom, paintx * 2 + paintw, selected, InfoDisplayOverImage);
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

HistoryTextState HistoryLocation::getState(QPoint point, HistoryStateRequest request) const {
	auto result = HistoryTextState(_parent);
	auto symbolAdd = 0;

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();

	if (bubble) {
		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (isBubbleTop()) {
				painty += st::msgPadding.top();
			}
		}

		auto textw = width() - st::msgPadding.left() - st::msgPadding.right();

		if (!_title.isEmpty()) {
			auto titleh = qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
			if (point.y() >= painty && point.y() < painty + titleh) {
				result = HistoryTextState(_parent, _title.getStateLeft(
					point - QPoint(paintx + st::msgPadding.left(), painty),
					textw,
					width(),
					request.forText()));
				return result;
			} else if (point.y() >= painty + titleh) {
				symbolAdd += _title.length();
			}
			painty += titleh;
		}
		if (!_description.isEmpty()) {
			auto descriptionh = qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
			if (point.y() >= painty && point.y() < painty + descriptionh) {
				result = HistoryTextState(_parent, _description.getStateLeft(
					point - QPoint(paintx + st::msgPadding.left(), painty),
					textw,
					width(),
					request.forText()));
			} else if (point.y() >= painty + descriptionh) {
				symbolAdd += _description.length();
			}
			painty += descriptionh;
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			painty += st::mediaInBubbleSkip;
		}
		painth -= painty;
	}
	if (QRect(paintx, painty, paintw, painth).contains(point) && _data) {
		result.link = _link;
	}
	if (_parent->getMedia() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayOverImage)) {
			result.cursor = HistoryInDateCursorState;
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}
	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryLocation::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (_description.isEmpty() || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

QString HistoryLocation::notificationText() const {
	return WithCaptionNotificationText(lang(lng_maps_point), _title);
}

QString HistoryLocation::inDialogsText() const {
	return WithCaptionDialogsText(lang(lng_maps_point), _title);
}

TextWithEntities HistoryLocation::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		TextWithEntities result = { qsl("[ ") + lang(lng_maps_point) + qsl(" ]\n"), EntitiesInText() };
		auto info = selectedText(AllTextSelection);
		if (!info.text.isEmpty()) {
			TextUtilities::Append(result, std::move(info));
			result.text.append('\n');
		}
		result.text += _link->dragText();
		return result;
	}

	auto titleResult = _title.originalTextWithEntities(selection);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection));
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}
	titleResult.text += '\n';
	TextUtilities::Append(titleResult, std::move(descriptionResult));
	return titleResult;
}

bool HistoryLocation::needsBubble() const {
	if (!_title.isEmpty() || !_description.isEmpty()) {
		return true;
	}
	if (auto message = _parent->toHistoryMessage()) {
		return message->viaBot()
			|| message->Has<HistoryMessageReply>()
			|| message->displayForwardedFrom()
//			|| message->displayFromName() // #TODO media views
;
	}
	return false;
}

int HistoryLocation::fullWidth() const {
	return st::locationSize.width();
}

int HistoryLocation::fullHeight() const {
	return st::locationSize.height();
}
