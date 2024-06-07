/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_location.h"

#include "base/unixtime.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_cloud_file.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kUntilOffPeriod = std::numeric_limits<TimeId>::max();
constexpr auto kLiveElapsedPartOpacity = 0.2;

[[nodiscard]] TimeId ResolveUpdateDate(not_null<Element*> view) {
	const auto item = view->data();
	const auto edited = item->Get<HistoryMessageEdited>();
	return edited ? edited->date : item->date();
}

[[nodiscard]] QString RemainingTimeText(
		not_null<Element*> view,
		TimeId period) {
	if (period == kUntilOffPeriod) {
		return QString(1, QChar(0x221E));
	}
	const auto elapsed = base::unixtime::now() - view->data()->date();
	const auto remaining = std::clamp(period - elapsed, 0, period);
	if (remaining < 10) {
		return tr::lng_seconds_tiny(tr::now, lt_count, remaining);
	} else if (remaining < 600) {
		return tr::lng_minutes_tiny(tr::now, lt_count, remaining / 60);
	} else if (remaining < 3600) {
		return QString::number(remaining / 60);
	} else if (remaining < 86400) {
		return tr::lng_hours_tiny(tr::now, lt_count, remaining / 3600);
	}
	return tr::lng_days_tiny(tr::now, lt_count, remaining / 86400);
}

[[nodiscard]] float64 RemainingTimeProgress(
		not_null<Element*> view,
		TimeId period) {
	if (period == kUntilOffPeriod) {
		return 1.;
	} else if (period < 1) {
		return 0.;
	}
	const auto elapsed = base::unixtime::now() - view->data()->date();
	return std::clamp(period - elapsed, 0, period) / float64(period);
}

} // namespace

struct Location::Live {
	explicit Live(TimeId period) : period(period) {
	}

	base::Timer updateStatusTimer;
	base::Timer updateRemainingTimer;
	QImage previous;
	QImage previousCache;
	Ui::BubbleRounding previousRounding;
	Ui::Animations::Simple crossfade;
	TimeId period = 0;
	int thumbnailHeight = 0;
};

Location::Location(
	not_null<Element*> parent,
	not_null<Data::CloudImage*> data,
	Data::LocationPoint point,
	Element *replacing,
	TimeId livePeriod)
: Media(parent)
, _data(data)
, _live(CreateLiveTracker(parent, livePeriod))
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _link(std::make_shared<LocationClickHandler>(point)) {
	if (_live) {
		_title.setText(
			st::webPageTitleStyle,
			tr::lng_live_location(tr::now),
			Ui::WebpageTextTitleOptions());
		_live->updateStatusTimer.setCallback([=] {
			updateLiveStatus();
			checkLiveFinish();
		});
		_live->updateRemainingTimer.setCallback([=] {
			checkLiveFinish();
		});
		updateLiveStatus();
		if (const auto media = replacing ? replacing->media() : nullptr) {
			_live->previous = media->locationTakeImage();
			if (!_live->previous.isNull()) {
				history()->owner().registerHeavyViewPart(_parent);
			}
		}
	}
}

Location::Location(
	not_null<Element*> parent,
	not_null<Data::CloudImage*> data,
	Data::LocationPoint point,
	const QString &title,
	const QString &description)
: Media(parent)
, _data(data)
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _link(std::make_shared<LocationClickHandler>(point)) {
	if (!title.isEmpty()) {
		_title.setText(
			st::webPageTitleStyle,
			title,
			Ui::WebpageTextTitleOptions());
	}
	if (!description.isEmpty()) {
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			TextUtilities::ParseEntities(
				description,
				TextParseLinks | TextParseMultiline),
			Ui::WebpageTextDescriptionOptions());
	}
}

Location::~Location() {
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

void Location::checkLiveFinish() {
	Expects(_live != nullptr);

	const auto now = base::unixtime::now();
	const auto item = _parent->data();
	const auto start = item->date();
	if (_live->period != kUntilOffPeriod && now - start >= _live->period) {
		_live = nullptr;
		item->history()->owner().requestViewResize(_parent);
	} else {
		_parent->repaint();
	}
}

std::unique_ptr<Location::Live> Location::CreateLiveTracker(
		not_null<Element*> parent,
		TimeId period) {
	if (!period) {
		return nullptr;
	}
	const auto now = base::unixtime::now();
	const auto date = parent->data()->date();
	return (now < date || now - date < period)
		? std::make_unique<Live>(period)
		: nullptr;
}

void Location::updateLiveStatus() {
	const auto date = ResolveUpdateDate(_parent);
	const auto now = base::unixtime::now();
	const auto elapsed = now - date;
	auto next = TimeId();
	const auto text = [&] {
		if (elapsed < 60) {
			next = 60 - elapsed;
			return tr::lng_live_location_now(tr::now);
		} else if (const auto minutes = elapsed / 60; minutes < 60) {
			next = 60 - (elapsed % 60);
			return tr::lng_live_location_minutes(tr::now, lt_count, minutes);
		} else if (const auto hours = elapsed / 3600; hours < 12) {
			next = 3600 - (elapsed % 3600);
			return tr::lng_live_location_hours(tr::now, lt_count, hours);
		}
		const auto dateFull = base::unixtime::parse(date);
		const auto nowFull = base::unixtime::parse(now);
		const auto nextTomorrow = [&] {
			const auto tomorrow = nowFull.date().addDays(1);
			next = nowFull.secsTo(QDateTime(tomorrow, QTime(0, 0)));
		};
		const auto locale = QLocale();
		const auto format = QLocale::ShortFormat;
		if (dateFull.date() == nowFull.date()) {
			nextTomorrow();
			const auto time = locale.toString(dateFull.time(), format);
			return tr::lng_live_location_today(tr::now, lt_time, time);
		} else if (dateFull.date().addDays(1) == nowFull.date()) {
			nextTomorrow();
			const auto time = locale.toString(dateFull.time(), format);
			return tr::lng_live_location_yesterday(tr::now, lt_time, time);
		}
		return tr::lng_live_location_date_time(
			tr::now,
			lt_date,
			locale.toString(dateFull.date(), format),
			lt_time,
			locale.toString(dateFull.time(), format));
	}();
	_description.setMarkedText(
		st::webPageDescriptionStyle,
		{ text },
		Ui::WebpageTextDescriptionOptions());
	if (next > 0 && next < 86400) {
		_live->updateStatusTimer.callOnce(next * crl::time(1000));
	}
}

QImage Location::locationTakeImage() {
	if (_media && !_media->isNull()) {
		return *_media;
	} else if (_live && !_live->previous.isNull()) {
		return _live->previous;
	}
	return {};
}

void Location::unloadHeavyPart() {
	_media = nullptr;
	if (_live) {
		_live->previous = QImage();
	}
}

bool Location::hasHeavyPart() const {
	return (_media != nullptr) || (_live && !_live->previous.isNull());
}

void Location::ensureMediaCreated() const {
	if (_media) {
		return;
	}
	_media = _data->createView();
	_data->load(&history()->session(), _parent->data()->fullId());
	history()->owner().registerHeavyViewPart(_parent);
}

QSize Location::countOptimalSize() {
	auto tw = fullWidth();
	auto th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	auto minWidth = std::clamp(
		_parent->minWidthForMedia(),
		st::minPhotoSize,
		st::maxMediaSize);
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
			if (_live) {
				if (isBubbleBottom()) {
					minHeight += st::msgPadding.bottom();
				}
			} else {
				if (isBubbleTop()) {
					minHeight += st::msgPadding.top();
				}
			}
		}
	}
	return { maxWidth, minHeight };
}

QSize Location::countCurrentSize(int newWidth) {
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
	auto minWidth = std::clamp(
		_parent->minWidthForMedia(),
		st::minPhotoSize,
		std::min(newWidth, st::maxMediaSize));
	accumulate_max(newWidth, minWidth);
	accumulate_max(newHeight, st::minPhotoSize);
	if (_live) {
		_live->thumbnailHeight = newHeight;
	}
	if (_parent->hasBubble()) {
		if (!_title.isEmpty()) {
			newHeight += qMin(_title.countHeight(newWidth - st::msgPadding.left() - st::msgPadding.right()), st::webPageTitleFont->height * 2);
		}
		if (!_description.isEmpty()) {
			newHeight += qMin(_description.countHeight(newWidth - st::msgPadding.left() - st::msgPadding.right()), st::webPageDescriptionFont->height * 3);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			newHeight += st::mediaInBubbleSkip;
			if (_live) {
				if (isBubbleBottom()) {
					newHeight += st::msgPadding.bottom();
				}
			} else {
				if (isBubbleTop()) {
					newHeight += st::msgPadding.top();
				}
			}
		}
	}
	return { newWidth, newHeight };
}

TextSelection Location::toDescriptionSelection(
		TextSelection selection) const {
	return UnshiftItemSelection(selection, _title);
}

TextSelection Location::fromDescriptionSelection(
		TextSelection selection) const {
	return ShiftItemSelection(selection, _title);
}

void Location::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();
	const auto st = context.st;
	const auto stm = context.messageStyle();

	const auto hasText = !_title.isEmpty() || !_description.isEmpty();
	const auto rounding = adjustedBubbleRounding(_live
		? RectPart::FullBottom
		: hasText
		? RectPart::FullTop
		: RectPart());
	const auto paintText = [&] {
		if (_live) {
			painty += st::mediaInBubbleSkip;
		} else if (!hasText) {
			return;
		} else if (isBubbleTop()) {
			painty += st::msgPadding.top();
		}

		auto textw = width() - st::msgPadding.left() - st::msgPadding.right();

		p.setPen(stm->historyTextFg);
		if (!_title.isEmpty()) {
			_title.drawLeftElided(p, paintx + st::msgPadding.left(), painty, textw, width(), 2, style::al_left, 0, -1, 0, false, context.selection);
			painty += qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			if (_live) {
				p.setPen(stm->msgDateFg);
			}
			_description.drawLeftElided(p, paintx + st::msgPadding.left(), painty, textw, width(), 3, style::al_left, 0, -1, 0, false, toDescriptionSelection(context.selection));
			painty += qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
		}
		if (!_live) {
			painty += st::mediaInBubbleSkip;
			painth -= painty;
		}
	};
	if (!_live) {
		paintText();
	}
	const auto thumbh = _live ? _live->thumbnailHeight : painth;
	auto rthumb = QRect(paintx, painty, paintw, thumbh);
	if (!bubble) {
		fillImageShadow(p, rthumb, rounding, context);
	}

	ensureMediaCreated();
	validateImageCache(rthumb.size(), rounding);
	const auto paintPrevious = _live && !_live->previous.isNull();
	auto opacity = _imageCache.isNull() ? 0. : 1.;
	if (paintPrevious) {
		opacity = _live->crossfade.value(opacity);
		if (opacity < 1.) {
			p.drawImage(rthumb.topLeft(), _live->previousCache);
			if (opacity > 0.) {
				p.setOpacity(opacity);
			}
		}
	}
	if (!_imageCache.isNull() && opacity > 0.) {
		p.drawImage(rthumb.topLeft(), _imageCache);
		if (opacity < 1.) {
			p.setOpacity(1.);
		}
	} else if (!bubble && !paintPrevious) {
		Ui::PaintBubble(
			p,
			Ui::SimpleBubble{
				.st = context.st,
				.geometry = rthumb,
				.pattern = context.bubblesPattern,
				.patternViewport = context.viewport,
				.outerWidth = width(),
				.selected = context.selected(),
				.outbg = context.outbg,
				.rounding = rounding,
			});
	}
	const auto paintMarker = [&](const style::icon &icon) {
		icon.paint(
			p,
			rthumb.x() + ((rthumb.width() - icon.width()) / 2),
			rthumb.y() + (rthumb.height() / 2) - icon.height(),
			width());
	};
	paintMarker(st->historyMapPoint());
	paintMarker(st->historyMapPointInner());
	if (context.selected()) {
		fillImageOverlay(p, rthumb, rounding, context);
	}
	if (_live) {
		painty += _live->thumbnailHeight;
		painth -= _live->thumbnailHeight;
		paintLiveRemaining(p, context, { paintx, painty, paintw, painth });
		paintText();
	} else if (_parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
		_parent->drawInfo(
			p,
			context,
			fullRight,
			fullBottom,
			paintx * 2 + paintw,
			InfoDisplayType::Image);
		if (const auto size = bubble ? std::nullopt : _parent->rightActionSize()) {
			auto fastShareLeft = _parent->hasRightLayout()
				? (paintx - size->width() - st::historyFastShareLeft)
				: (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - size->height());
			_parent->drawRightAction(p, context, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

void Location::paintLiveRemaining(
		QPainter &p,
		const PaintContext &context,
		QRect bottom) const {
	const auto size = st::liveLocationRemainingSize;
	const auto skip = (bottom.height() - size) / 2;
	const auto rect = QRect(
		bottom.x() + bottom.width() - size - skip,
		bottom.y() + skip,
		size,
		size);
	auto hq = PainterHighQualityEnabler(p);
	const auto stm = context.messageStyle();
	const auto color = stm->msgServiceFg->c;
	const auto untiloff = (_live->period == kUntilOffPeriod);
	const auto progress = RemainingTimeProgress(_parent, _live->period);
	const auto part = 1. / 360;
	const auto full = (progress >= 1. - part);
	auto elapsed = color;
	if (!full) {
		elapsed.setAlphaF(elapsed.alphaF() * kLiveElapsedPartOpacity);
	}
	auto pen = QPen(elapsed);
	const auto stroke = style::ConvertScaleExact(2.);
	pen.setWidthF(stroke);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(rect);

	if (untiloff) {
		stm->liveLocationLongIcon.paintInCenter(p, rect);
	} else {
		if (!full && progress > part) {
			auto pen = QPen(color);
			pen.setWidthF(stroke);
			p.setPen(pen);
			p.drawArc(
				rect,
				arc::kQuarterLength,
				int(base::SafeRound(arc::kFullLength * progress)));
		}

		p.setPen(stm->msgServiceFg);
		p.setFont(st::semiboldFont);
		const auto text = RemainingTimeText(_parent, _live->period);
		p.drawText(rect, text, style::al_center);
		const auto each = std::clamp(_live->period / 360, 1, 86400);
		_live->updateRemainingTimer.callOnce(each * crl::time(1000));
	}
}

void Location::validateImageCache(
		QSize outer,
		Ui::BubbleRounding rounding) const {
	Expects(_media != nullptr);

	if (_live && !_live->previous.isNull()) {
		validateImageCache(
			_live->previous,
			_live->previousCache,
			_live->previousRounding,
			outer,
			rounding);
	}
	validateImageCache(
		*_media,
		_imageCache,
		_imageCacheRounding,
		outer,
		rounding);
	checkLiveCrossfadeStart();
}

void Location::checkLiveCrossfadeStart() const {
	if (!_live
		|| _live->previous.isNull()
		|| !_media
		|| _media->isNull()
		|| _live->crossfade.animating()) {
		return;
	}
	_live->crossfade.start([=] {
		if (!_live->crossfade.animating()) {
			_live->previous = QImage();
			_live->previousCache = QImage();
		}
		_parent->repaint();
	}, 0., 1., st::fadeWrapDuration);
}

void Location::validateImageCache(
		const QImage &source,
		QImage &cache,
		Ui::BubbleRounding &cacheRounding,
		QSize outer,
		Ui::BubbleRounding rounding) const {
	if (source.isNull()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	if (cache.size() == (outer * ratio) && cacheRounding == rounding) {
		return;
	}
	cache = Images::Round(
		source.scaled(
			outer * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation),
		MediaRoundingMask(rounding));
	cache.setDevicePixelRatio(ratio);
	cacheRounding = rounding;
}

TextState Location::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	auto symbolAdd = 0;

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();

	auto checkText = [&] {
		if (_live) {
			painty += st::mediaInBubbleSkip;
		} else if (_title.isEmpty() && _description.isEmpty()) {
			return false;
		} else if (isBubbleTop()) {
			painty += st::msgPadding.top();
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
				return true;
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
				result.symbol += symbolAdd;
				return true;
			} else if (point.y() >= painty + descriptionh) {
				symbolAdd += _description.length();
			}
			painty += descriptionh;
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			painty += st::mediaInBubbleSkip;
		}
		painth -= painty;
		return false;
	};
	if (!_live && checkText()) {
		return result;
	}
	const auto thumbh = _live ? _live->thumbnailHeight : painth;
	if (QRect(paintx, painty, paintw, thumbh).contains(point) && _data) {
		result.link = _link;
	}
	if (_live) {
		painty += _live->thumbnailHeight;
		painth -= _live->thumbnailHeight;
		if (checkText()) {
			return result;
		}
	} else if (_parent->media() == this) {
		auto fullRight = paintx + paintw;
		auto fullBottom = height();
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
	result.symbol += symbolAdd;
	return result;
}

TextSelection Location::adjustSelection(TextSelection selection, TextSelectType type) const {
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

TextForMimeData Location::selectedText(TextSelection selection) const {
	auto titleResult = _title.toTextForMimeData(selection);
	auto descriptionResult = _description.toTextForMimeData(
		toDescriptionSelection(selection));
	if (titleResult.empty()) {
		return descriptionResult;
	} else if (descriptionResult.empty()) {
		return titleResult;
	}
	return titleResult.append('\n').append(std::move(descriptionResult));
}

bool Location::needsBubble() const {
	if (!_title.isEmpty() || !_description.isEmpty()) {
		return true;
	}
	const auto item = _parent->data();
	return item->repliesAreComments()
		|| item->externalReply()
		|| item->viaBot()
		|| _parent->displayReply()
		|| _parent->displayForwardedFrom()
		|| _parent->displayFromName()
		|| _parent->displayedTopicButton();
}

QPoint Location::resolveCustomInfoRightBottom() const {
	const auto skipx = (st::msgDateImgDelta + st::msgDateImgPadding.x());
	const auto skipy = (st::msgDateImgDelta + st::msgDateImgPadding.y());
	return QPoint(width() - skipx, height() - skipy);
}

int Location::fullWidth() const {
	return st::locationSize.width();
}

int Location::fullHeight() const {
	return st::locationSize.height();
}

} // namespace HistoryView
