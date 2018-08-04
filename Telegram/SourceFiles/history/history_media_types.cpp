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
#include "media/player/media_player_round_controller.h"
#include "media/view/media_clip_playback.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "core/click_handler_types.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_location_manager.h"
#include "history/history_message.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "styles/style_history.h"
#include "calls/calls_instance.h"
#include "ui/empty_userpic.h"
#include "ui/grouped_layout.h"
#include "ui/text_options.h"
#include "data/data_session.h"
#include "data/data_media_types.h"

namespace {

constexpr auto kMaxGifForwardedBarLines = 4;
constexpr auto kMaxOriginalEntryLines = 8192;

using TextState = HistoryView::TextState;

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

std::unique_ptr<HistoryMedia> CreateAttach(
		not_null<HistoryView::Element*> parent,
		DocumentData *document,
		PhotoData *photo) {
	if (document) {
		if (document->sticker()) {
			return std::make_unique<HistorySticker>(parent, document);
		} else if (document->isAnimation()) {
			return std::make_unique<HistoryGif>(
				parent,
				document);
		} else if (document->isVideoFile()) {
			return std::make_unique<HistoryVideo>(
				parent,
				parent->data(),
				document);
		}
		return std::make_unique<HistoryDocument>(
			parent,
			document);
	} else if (photo) {
		return std::make_unique<HistoryPhoto>(
			parent,
			parent->data(),
			photo);
	}
	return nullptr;
}

} // namespace

QString FillAmountAndCurrency(uint64 amount, const QString &currency) {
	static const auto ShortCurrencyNames = QMap<QString, QString> {
		{ qsl("USD"), QString::fromUtf8("\x24") },
		{ qsl("GBP"), QString::fromUtf8("\xC2\xA3") },
		{ qsl("EUR"), QString::fromUtf8("\xE2\x82\xAC") },
		{ qsl("JPY"), QString::fromUtf8("\xC2\xA5") },
	};
	static const auto Denominators = QMap<QString, int> {
		{ qsl("CLF"), 10000 },
		{ qsl("BHD"), 1000 },
		{ qsl("IQD"), 1000 },
		{ qsl("JOD"), 1000 },
		{ qsl("KWD"), 1000 },
		{ qsl("LYD"), 1000 },
		{ qsl("OMR"), 1000 },
		{ qsl("TND"), 1000 },
		{ qsl("BIF"), 1 },
		{ qsl("BYR"), 1 },
		{ qsl("CLP"), 1 },
		{ qsl("CVE"), 1 },
		{ qsl("DJF"), 1 },
		{ qsl("GNF"), 1 },
		{ qsl("ISK"), 1 },
		{ qsl("JPY"), 1 },
		{ qsl("KMF"), 1 },
		{ qsl("KRW"), 1 },
		{ qsl("MGA"), 1 },
		{ qsl("PYG"), 1 },
		{ qsl("RWF"), 1 },
		{ qsl("UGX"), 1 },
		{ qsl("UYI"), 1 },
		{ qsl("VND"), 1 },
		{ qsl("VUV"), 1 },
		{ qsl("XAF"), 1 },
		{ qsl("XOF"), 1 },
		{ qsl("XPF"), 1 },
		{ qsl("MRO"), 10 },
	};
	const auto currencyText = ShortCurrencyNames.value(currency, currency);
	const auto denominator = Denominators.value(currency, 100);
	const auto currencyValue = amount / float64(denominator);
	const auto digits = [&] {
		auto result = 0;
		for (auto test = 1; test < denominator; test *= 10) {
			++result;
		}
		return result;
	}();
	return QLocale::system().toCurrencyString(currencyValue, currencyText);
	//auto amountBucks = amount / 100;
	//auto amountCents = amount % 100;
	//auto amountText = qsl("%1,%2").arg(amountBucks).arg(amountCents, 2, 10, QChar('0'));
	//return currencyText + amountText;
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
	Auth().data().requestViewRepaint(_parent);
}

void HistoryFileMedia::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	Auth().data().requestViewRepaint(_parent);
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
		Auth().data().requestViewRepaint(_parent);
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
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<PhotoData*> photo)
: HistoryFileMedia(parent)
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
: HistoryFileMedia(parent)
, _data(photo)
, _serviceWidth(width) {
	create(parent->data()->fullId(), chat);
}

void HistoryPhoto::create(FullMsgId contextId, PeerData *chat) {
	setLinks(
		std::make_shared<PhotoOpenClickHandler>(_data, contextId, chat),
		std::make_shared<PhotoSaveClickHandler>(_data, contextId, chat),
		std::make_shared<PhotoCancelClickHandler>(_data, contextId, chat));
	_data->thumb->load();
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

	_data->automaticLoad(_parent->data());
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
			? _data->full->pixCircled(_pixw, _pixh)
			: _data->thumb->pixBlurredCircled(_pixw, _pixh);
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
			? _data->full->pixSingle(_pixw, _pixh, paintw, painth, roundRadius, roundCorners)
			: _data->thumb->pixBlurredSingle(_pixw, _pixh, paintw, painth, roundRadius, roundCorners);
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
			auto delayed = _data->full->toDelayedStorageImage();
			if (!delayed || !delayed->location().isNull()) {
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
	_data->automaticLoad(_parent->data());

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

TextState HistoryPhoto::getStateGrouped(
		const QRect &geometry,
		QPoint point,
		StateRequest request) const {
	if (!geometry.contains(point)) {
		return {};
	}
	const auto delayed = _data->full->toDelayedStorageImage();
	return TextState(_parent, _data->uploading()
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

void HistoryPhoto::parentTextUpdated() {
	_caption = (_parent->media() == this)
		? createCaption(_parent->data())
		: Text();
	Auth().data().requestViewResize(_parent);
}

HistoryVideo::HistoryVideo(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<DocumentData*> document)
: HistoryFileMedia(parent)
, _data(document)
, _thumbw(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	_caption = createCaption(realParent);

	setDocumentLinks(_data, realParent);

	setStatusSize(FileStatusSizeReady);

	_data->thumb->load();
}

QSize HistoryVideo::countOptimalSize() {
	if (_parent->media() != this) {
		_caption = Text();
	} else if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
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

	_data->automaticLoad(_parent->data());
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

	auto inWebPage = (_parent->media() != this);
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
			if (_parent->data()->id > 0 || _data->uploading()) {
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

TextState HistoryVideo::textState(QPoint point, StateRequest request) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return {};
	}

	auto result = TextState(_parent);
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
			result = TextState(_parent, _caption.getState(
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

QSize HistoryVideo::sizeForGrouping() const {
	const auto width = _data->dimensions.isEmpty()
		? _data->thumb->width()
		: _data->dimensions.width();
	const auto height = _data->dimensions.isEmpty()
		? _data->thumb->height()
		: _data->dimensions.height();
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
	_data->automaticLoad(_parent->data());

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
			if (_parent->data()->id > 0 || _data->uploading()) {
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

TextState HistoryVideo::getStateGrouped(
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

TextWithEntities HistoryVideo::selectedText(TextSelection selection) const {
	return _caption.originalTextWithEntities(selection, ExpandLinksAll);
}

bool HistoryVideo::needsBubble() const {
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

void HistoryVideo::parentTextUpdated() {
	_caption = (_parent->media() == this)
		? createCaption(_parent->data())
		: Text();
	Auth().data().requestViewResize(_parent);
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

HistoryDocument::HistoryDocument(
	not_null<Element*> parent,
	not_null<DocumentData*> document)
: HistoryFileMedia(parent)
, _data(document) {
	const auto item = parent->data();
	auto caption = createCaption(item);

	createComponents(!caption.isEmpty());
	if (auto named = Get<HistoryDocumentNamed>()) {
		fillNamedFromData(named);
	}

	setDocumentLinks(_data, item);

	setStatusSize(FileStatusSizeReady);

	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		captioned->_caption = std::move(caption);
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
			_parent->data()->fullId());
		thumbed->_linkcancell = std::make_shared<DocumentCancelClickHandler>(
			_data,
			_parent->data()->fullId());
	}
	if (auto voice = Get<HistoryDocumentVoice>()) {
		voice->_seekl = std::make_shared<VoiceSeekClickHandler>(
			_data,
			_parent->data()->fullId());
	}
}

void HistoryDocument::fillNamedFromData(HistoryDocumentNamed *named) {
	auto nameString = named->_name = _data->composeNameString();
	named->_namew = st::semiboldFont->width(nameString);
}

QSize HistoryDocument::countOptimalSize() {
	const auto item = _parent->data();

	auto captioned = Get<HistoryDocumentCaptioned>();
	if (_parent->media() != this) {
		if (captioned) {
			RemoveComponents(HistoryDocumentCaptioned::Bit());
			captioned = nullptr;
		}
	} else if (captioned && captioned->_caption.hasSkipBlock()) {
		captioned->_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
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
		accumulate_min(maxWidth, st::msgMaxWidth);
	}

	auto minHeight = 0;
	if (thumbed) {
		minHeight = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else {
		minHeight = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (!captioned && (item->Has<HistoryMessageSigned>()
		|| item->Has<HistoryMessageViews>()
		|| _parent->displayEditedBadge())) {
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

	_data->automaticLoad(_parent->data());
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

		auto inWebPage = (_parent->media() != this);
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
		if (!outbg && !voice->_playback && _parent->data()->isMediaUnread()) {
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

	if (_parent->data()->isMediaUnread()) {
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

TextState HistoryDocument::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

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
			if (state.id == AudioMsgId(_data, _parent->data()->fullId())
				&& !Media::Player::IsStoppedOrStopping(state.state)) {
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
			result = TextState(_parent, captioned->_caption.getState(
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
			Auth().data().requestViewRepaint(_parent);
		}
	}
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
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		const auto &caption = captioned->_caption;
		return caption.originalTextWithEntities(selection, ExpandLinksAll);
	}
	return TextWithEntities();
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
			if (state.id == AudioMsgId(_data, _parent->data()->fullId())
				&& !Media::Player::IsStoppedOrStopping(state.state)) {
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
			if (!showPause && (state.id == AudioMsgId(_data, _parent->data()->fullId()))) {
				showPause = Media::Player::instance()->isSeeking(AudioMsgId::Type::Voice);
			}
		} else if (_data->isAudioFile()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(_data, _parent->data()->fullId())
				&& !Media::Player::IsStoppedOrStopping(state.state)) {
				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.length / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			} else {
			}
			if (!showPause && (state.id == AudioMsgId(_data, _parent->data()->fullId()))) {
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
			if (timer) {
				Auth().data().requestViewRepaint(_parent);
			}
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
			if (state.id == AudioMsgId(_data, _parent->data()->fullId()) && state.length) {
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

	const auto fullId = realParent->fullId();
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		if (thumbed->_linksavel) {
			thumbed->_linksavel->setMessageId(fullId);
			thumbed->_linkcancell->setMessageId(fullId);
		}
	}
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->_seekl) {
			voice->_seekl->setMessageId(fullId);
		}
	}
}

void HistoryDocument::parentTextUpdated() {
	auto caption = (_parent->media() == this)
		? createCaption(_parent->data())
		: Text();
	if (!caption.isEmpty()) {
		AddComponents(HistoryDocumentCaptioned::Bit());
		auto captioned = Get<HistoryDocumentCaptioned>();
		captioned->_caption = std::move(caption);
	} else {
		RemoveComponents(HistoryDocumentCaptioned::Bit());
	}
	Auth().data().requestViewResize(_parent);
}

TextWithEntities HistoryDocument::getCaption() const {
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		return captioned->_caption.originalTextWithEntities();
	}
	return TextWithEntities();
}

HistoryGif::HistoryGif(
	not_null<Element*> parent,
	not_null<DocumentData*> document)
: HistoryFileMedia(parent)
, _data(document)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	const auto item = parent->data();
	setDocumentLinks(_data, item, true);

	setStatusSize(FileStatusSizeReady);

	_caption = createCaption(item);
	_data->thumb->load();
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
		tw = convertScale(reader->width());
		th = convertScale(reader->height());
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
		tw = convertScale(reader->width());
		th = convertScale(reader->height());
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
	_data->automaticLoad(item);
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

	if (!animating || item->id < 0) {
		if (displayLoading) {
			ensureAnimation();
			if (!_animation->radial.animating()) {
				_animation->radial.start(dataProgress());
			}
		}
		updateStatusText();
	} else if (playingVideo) {
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
		p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, _thumbh, usew, painth, roundRadius, roundCorners));
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
		_data->forget();
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

HistorySticker::HistorySticker(
	not_null<Element*> parent,
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
	accumulate_max(
		maxWidth,
		_parent->infoWidth() + 2 * st::msgDateImgPadding.x());
	if (_parent->media() == this) {
		maxWidth += additionalWidth();
	}
	return { maxWidth, minHeight };
}

QSize HistorySticker::countCurrentSize(int newWidth) {
	const auto item = _parent->data();
	accumulate_min(newWidth, maxWidth());
	if (_parent->media() == this) {
		auto via = item->Get<HistoryMessageVia>();
		auto reply = item->Get<HistoryMessageReply>();
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
	auto inWebPage = (_parent->media() != this);

	const auto item = _parent->data();
	int usew = maxWidth(), usex = 0;
	auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
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

	if (!inWebPage) {
		auto fullRight = usex + usew;
		auto fullBottom = height();
		if (needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, usex * 2 + usew, selected, InfoDisplayType::Background);
		}
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

TextState HistorySticker::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	auto outbg = _parent->hasOutLayout();
	auto inWebPage = (_parent->media() != this);

	const auto item = _parent->data();
	int usew = maxWidth(), usex = 0;
	auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
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
	if (_parent->media() == this) {
		auto fullRight = usex + usew;
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Image)) {
			result.cursor = CursorState::Date;
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

bool HistorySticker::needInfoDisplay() const {
	return (_parent->data()->id < 0 || _parent->isUnderCursor());
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
	const auto item = _parent->data();
	return additionalWidth(
		item->Get<HistoryMessageVia>(),
		item->Get<HistoryMessageReply>());
}

namespace {

ClickHandlerPtr sendMessageClickHandler(PeerData *peer) {
	return std::make_shared<LambdaClickHandler>([peer] {
		App::wnd()->controller()->showPeerHistory(
			peer->id,
			Window::SectionShow::Way::Forward);
	});
}

ClickHandlerPtr addContactClickHandler(not_null<HistoryItem*> item) {
	return std::make_shared<LambdaClickHandler>([fullId = item->fullId()] {
		if (const auto item = App::histItemById(fullId)) {
			if (const auto media = item->media()) {
				if (const auto contact = media->sharedContact()) {
					Ui::show(Box<AddContactBox>(
						contact->firstName,
						contact->lastName,
						contact->phoneNumber));
				}
			}
		}
	});
}

} // namespace

HistoryContact::HistoryContact(
	not_null<Element*> parent,
	UserId userId,
	const QString &first,
	const QString &last,
	const QString &phone)
: HistoryMedia(parent)
, _userId(userId)
, _fname(first)
, _lname(last)
, _phone(App::formatPhone(phone)) {
	Auth().data().registerContactView(userId, parent);

	_name.setText(
		st::semiboldTextStyle,
		lng_full_name(lt_first_name, first, lt_last_name, last).trimmed(),
		Ui::NameTextOptions());
	_phonew = st::normalFont->width(_phone);
}

HistoryContact::~HistoryContact() {
	Auth().data().unregisterContactView(_userId, _parent);
}

void HistoryContact::updateSharedContactUserId(UserId userId) {
	if (_userId != userId) {
		Auth().data().unregisterContactView(_userId, _parent);
		_userId = userId;
		Auth().data().registerContactView(_userId, _parent);
	}
}

QSize HistoryContact::countOptimalSize() {
	const auto item = _parent->data();
	auto maxWidth = st::msgFileMinWidth;

	_contact = _userId ? App::userLoaded(_userId) : nullptr;
	if (_contact) {
		_contact->loadUserpic();
	} else {
		_photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Data::PeerUserpicColor(_userId ? _userId : _parent->data()->id),
			_name.originalText());
	}
	if (_contact
		&& _contact->contactStatus() == UserData::ContactStatus::Contact) {
		_linkl = sendMessageClickHandler(_contact);
		_link = lang(lng_profile_send_message).toUpper();
	} else if (_userId) {
		_linkl = addContactClickHandler(_parent->data());
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
	accumulate_min(maxWidth, st::msgMaxWidth);
	auto minHeight = 0;
	if (_userId) {
		minHeight = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
		if (item->Has<HistoryMessageSigned>()
			|| item->Has<HistoryMessageViews>()) {
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

TextState HistoryContact::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

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

HistoryCall::HistoryCall(
	not_null<Element*> parent,
	not_null<Data::Call*> call)
: HistoryMedia(parent) {
	_duration = call->duration;
	_reason = call->finishReason;

	const auto item = parent->data();
	_text = Data::MediaCall::Text(item, _reason);
	_status = parent->dateTime().time().toString(cTimeFormat());
	if (_duration) {
		if (_reason != FinishReason::Missed
			&& _reason != FinishReason::Busy) {
			_status = lng_call_duration_info(
				lt_time,
				_status,
				lt_duration,
				formatDurationWords(_duration));
		} else {
			_duration = 0;
		}
	}
}

QSize HistoryCall::countOptimalSize() {
	const auto user = _parent->data()->history()->peer->asUser();
	_link = std::make_shared<LambdaClickHandler>([=] {
		if (user) {
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

TextState HistoryCall::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (QRect(0, 0, width(), height()).contains(point)) {
		result.link = _link;
		return result;
	}
	return result;
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

HistoryWebPage::HistoryWebPage(
	not_null<Element*> parent,
	not_null<WebPageData*> data)
: HistoryMedia(parent)
, _data(data)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft) {
	Auth().data().registerWebPageView(_data, _parent);
}

QSize HistoryWebPage::countOptimalSize() {
	if (_data->pendingTill) {
		return { 0, 0 };
	}
	const auto versionChanged = (_dataVersion != _data->version);
	if (versionChanged) {
		_dataVersion = _data->version;
		_openl = nullptr;
		_attach = nullptr;
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
		_attach = CreateAttach(_parent, _data->document, _data->photo);
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

TextSelection HistoryWebPage::toDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::UnshiftItemSelection(selection, _title);
}

TextSelection HistoryWebPage::fromDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::ShiftItemSelection(selection, _title);
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

TextState HistoryWebPage::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

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
			result = TextState(_parent, _title.getStateElidedLeft(
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
				result = TextState(_parent, _description.getStateElidedLeft(
					point - QPoint(padding.left(), tshift),
					paintw,
					width(),
					descriptionRequest));
			} else {
				result = TextState(_parent, _description.getStateLeft(
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
			result = _attach->textState(point - QPoint(attachLeft, attachTop), request);

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

void HistoryWebPage::playAnimation(bool autoplay) {
	if (_attach) {
		if (autoplay) {
			_attach->autoplayAnimation();
		} else {
			_attach->playAnimation();
		}
	}
}

bool HistoryWebPage::isDisplayed() const {
	const auto item = _parent->data();
	return !_data->pendingTill
		&& !item->Has<HistoryMessageLogEntryOriginal>();
}

TextWithEntities HistoryWebPage::selectedText(TextSelection selection) const {
	auto titleResult = _title.originalTextWithEntities(
		selection,
		ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(
		toDescriptionSelection(selection),
		ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	TextUtilities::Append(titleResult, std::move(descriptionResult));
	return titleResult;
}

QMargins HistoryWebPage::inBubblePadding() const {
	auto lshift = st::msgPadding.left() + st::webPageLeft;
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

bool HistoryWebPage::isLogEntryOriginal() const {
	return _parent->data()->isLogEntry() && _parent->media() != this;
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

HistoryWebPage::~HistoryWebPage() {
	Auth().data().unregisterWebPageView(_data, _parent);
}

HistoryGame::HistoryGame(
	not_null<Element*> parent,
	not_null<GameData*> data,
	const TextWithEntities &consumed)
: HistoryMedia(parent)
, _data(data)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft) {
	if (!consumed.text.isEmpty()) {
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			consumed,
			Ui::ItemTextOptions(parent->data()));
	}
	Auth().data().registerGameView(_data, _parent);
}

QSize HistoryGame::countOptimalSize() {
	auto lineHeight = unitedLineHeight();

	const auto item = _parent->data();
	if (!_openl && IsServerMsgId(item->id)) {
		const auto row = 0;
		const auto column = 0;
		_openl = std::make_shared<ReplyMarkupClickHandler>(
			row,
			column,
			item->fullId());
	}

	auto title = TextUtilities::SingleLine(_data->title);

	// init attach
	if (!_attach) {
		_attach = CreateAttach(_parent, _data->document, _data->photo);
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
		_openl->setMessageId(realParent->fullId());
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

TextSelection HistoryGame::toDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::UnshiftItemSelection(selection, _title);
}

TextSelection HistoryGame::fromDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::ShiftItemSelection(selection, _title);
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

TextState HistoryGame::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

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
			result = TextState(_parent, _title.getStateElidedLeft(
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
			result = TextState(_parent, _description.getStateElidedLeft(
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
		if (!_parent->data()->isLogEntry()) {
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
				if (!_parent->data()->isLogEntry()) {
					result.link = _openl;
				}
			} else {
				result = _attach->textState(point - QPoint(attachLeft, attachTop), request);
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

TextWithEntities HistoryGame::selectedText(TextSelection selection) const {
	auto titleResult = _title.originalTextWithEntities(
		selection,
		ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(
		toDescriptionSelection(selection),
		ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	TextUtilities::Append(titleResult, std::move(descriptionResult));
	return titleResult;
}

void HistoryGame::playAnimation(bool autoplay) {
	if (_attach) {
		if (autoplay) {
			_attach->autoplayAnimation();
		} else {
			_attach->playAnimation();
		}
	}
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

void HistoryGame::parentTextUpdated() {
	if (const auto media = _parent->data()->media()) {
		const auto consumed = media->consumedMessageText();
		if (!consumed.text.isEmpty()) {
			_description.setMarkedText(
				st::webPageDescriptionStyle,
				consumed,
				Ui::ItemTextOptions(_parent->data()));
		} else {
			_description = Text(st::msgMinWidth - st::webPageLeft);
		}
		Auth().data().requestViewResize(_parent);
	}
}

HistoryGame::~HistoryGame() {
	Auth().data().unregisterGameView(_data, _parent);
}

HistoryInvoice::HistoryInvoice(
	not_null<Element*> parent,
	not_null<Data::Invoice*> invoice)
: HistoryMedia(parent)
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _status(st::msgMinWidth) {
	fillFromData(invoice);
}

void HistoryInvoice::fillFromData(not_null<Data::Invoice*> invoice) {
	// init attach
	auto labelText = [&] {
		if (invoice->receiptMsgId) {
			if (invoice->isTest) {
				return lang(lng_payments_receipt_label_test);
			}
			return lang(lng_payments_receipt_label);
		} else if (invoice->isTest) {
			return lang(lng_payments_invoice_label_test);
		}
		return lang(lng_payments_invoice_label);
	};
	auto statusText = TextWithEntities {
		FillAmountAndCurrency(invoice->amount, invoice->currency),
		EntitiesInText()
	};
	statusText.entities.push_back(EntityInText(EntityInTextBold, 0, statusText.text.size()));
	statusText.text += ' ' + labelText().toUpper();
	_status.setMarkedText(
		st::defaultTextStyle,
		statusText,
		Ui::ItemTextOptions(_parent->data()));

	_receiptMsgId = invoice->receiptMsgId;

	// init strings
	if (!invoice->description.isEmpty()) {
		auto marked = TextWithEntities { invoice->description };
		auto parseFlags = TextParseLinks | TextParseMultiline | TextParseRichText;
		TextUtilities::ParseEntities(marked, parseFlags);
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			marked,
			Ui::WebpageTextDescriptionOptions());
	}
	if (!invoice->title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			invoice->title,
			Ui::WebpageTextTitleOptions());
	}
}

QSize HistoryInvoice::countOptimalSize() {
	auto lineHeight = unitedLineHeight();

	if (_attach) {
		if (_status.hasSkipBlock()) {
			_status.removeSkipBlock();
		}
	} else {
		_status.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
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

TextSelection HistoryInvoice::toDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::UnshiftItemSelection(selection, _title);
}

TextSelection HistoryInvoice::fromDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::ShiftItemSelection(selection, _title);
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

TextState HistoryInvoice::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

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
			result = TextState(_parent, _title.getStateElidedLeft(
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
			result = TextState(_parent, _description.getStateLeft(
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
			result = _attach->textState(point - QPoint(attachLeft, attachTop), request);
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

TextWithEntities HistoryInvoice::selectedText(TextSelection selection) const {
	auto titleResult = _title.originalTextWithEntities(
		selection,
		ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(
		toDescriptionSelection(selection),
		ExpandLinksAll);
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

HistoryLocation::HistoryLocation(
	not_null<Element*> parent,
	not_null<LocationData*> location,
	const QString &title,
	const QString &description)
: HistoryMedia(parent)
, _data(location)
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _link(std::make_shared<LocationClickHandler>(_data->coords)) {
	if (!title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			TextUtilities::Clean(title),
			Ui::WebpageTextTitleOptions());
	}
	if (!description.isEmpty()) {
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			TextUtilities::ParseEntities(
				TextUtilities::Clean(description),
				TextParseLinks | TextParseMultiline | TextParseRichText),
			Ui::WebpageTextDescriptionOptions());
	}
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

TextSelection HistoryLocation::toDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::UnshiftItemSelection(selection, _title);
}

TextSelection HistoryLocation::fromDescriptionSelection(
		TextSelection selection) const {
	return HistoryView::ShiftItemSelection(selection, _title);
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

	if (_parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
		_parent->drawInfo(p, fullRight, fullBottom, paintx * 2 + paintw, selected, InfoDisplayType::Image);
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

TextState HistoryLocation::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
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
				result = TextState(_parent, _title.getStateLeft(
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
				result = TextState(_parent, _description.getStateLeft(
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
	if (_parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
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

TextWithEntities HistoryLocation::selectedText(TextSelection selection) const {
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
	const auto item = _parent->data();
	return item->viaBot()
		|| item->Has<HistoryMessageReply>()
		|| _parent->displayForwardedFrom()
		|| _parent->displayFromName();
	return false;
}

int HistoryLocation::fullWidth() const {
	return st::locationSize.width();
}

int HistoryLocation::fullHeight() const {
	return st::locationSize.height();
}
