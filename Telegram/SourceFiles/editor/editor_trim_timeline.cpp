/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_trim_timeline.h"

#include "base/event_filter.h"
#include "ui/widgets/fields/masked_input_field.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "styles/style_editor.h"

#include <QtGui/QPainterPath>
#include <QtGui/QtEvents>
#include <QtWidgets/QApplication>

namespace Editor {
namespace {

constexpr auto kZoomStep = 1.2;
constexpr auto kZoomSnap = 0.001;
constexpr auto kMinVisibleSpan = crl::time(2000);
constexpr auto kMinSelection = crl::time(100);
constexpr auto kCompactHandleSpans = 3;
constexpr auto kEdgeScrollFrame = crl::time(16);
constexpr auto kEdgeScrollSpeed = 9.; // Per second per pixel of overshoot.
constexpr auto kEdgeScrollMaxSpeed = 3.; // Strip widths per second.
constexpr auto kScrollIndicatorOpacity = 0.5;
constexpr auto kScrollIndicatorFadeZoom = 0.25;
constexpr auto kOverviewTrackOpacity = 0.18;
constexpr auto kHintBgOpacityBoost = 1.6;
constexpr auto kHintDuration = crl::time(150);
constexpr auto kScrollToSelectionDuration = crl::time(250);

constexpr auto kStampPrecision = 3;

[[nodiscard]] QString Stamp(crl::time value, int precision = 1) {
	const auto digits = QString::number(value % 1000).rightJustified(
		kStampPrecision,
		'0');
	return Ui::FormatDurationText(int(value / 1000))
		+ '.'
		+ digits.left(precision);
}

[[nodiscard]] bool IsRangeDash(QChar ch) {
	return (ch == '-') || (ch == QChar(0x2013)) || (ch == QChar(0x2014));
}

[[nodiscard]] bool IsStampChar(QChar ch) {
	return (ch >= '0' && ch <= '9')
		|| (ch == ':')
		|| (ch == '.')
		|| (ch == ',')
		|| (ch == ' ')
		|| IsRangeDash(ch);
}

[[nodiscard]] std::optional<crl::time> ParseStamp(QStringView text) {
	const auto parts = text.split(':');
	if (parts.isEmpty() || parts.size() > 3) {
		return std::nullopt;
	}
	auto seconds = crl::time(0);
	for (auto i = 0; i + 1 < parts.size(); ++i) {
		auto ok = false;
		const auto value = parts[i].toInt(&ok);
		if (!ok || value < 0) {
			return std::nullopt;
		}
		seconds = seconds * 60 + value;
	}
	const auto last = parts.back();
	const auto dot = std::max(last.indexOf('.'), last.indexOf(','));
	const auto whole = (dot >= 0) ? last.left(dot) : last;
	const auto fraction = (dot >= 0) ? last.mid(dot + 1) : QStringView();
	if (whole.isEmpty() && fraction.isEmpty()) {
		return std::nullopt;
	}
	auto ok = true;
	const auto wholeValue = whole.isEmpty() ? 0 : whole.toInt(&ok);
	if (!ok || wholeValue < 0) {
		return std::nullopt;
	}
	const auto digits = fraction.left(kStampPrecision);
	const auto fractionValue = digits.isEmpty() ? 0 : digits.toInt(&ok);
	if (!ok || fractionValue < 0) {
		return std::nullopt;
	}
	auto milliseconds = crl::time(fractionValue);
	for (auto i = digits.size(); i < kStampPrecision; ++i) {
		milliseconds *= 10;
	}
	return (seconds * 60 + wholeValue) * 1000 + milliseconds;
}

[[nodiscard]] QStringList SplitStamps(const QString &text) {
	auto result = QStringList();
	auto current = QString();
	for (const auto ch : text) {
		if (!ch.isSpace() && !IsRangeDash(ch)) {
			current.append(ch);
		} else if (!current.isEmpty()) {
			result.push_back(base::take(current));
		}
	}
	if (!current.isEmpty()) {
		result.push_back(current);
	}
	return result;
}

[[nodiscard]] int HandleWidth(int selectionWidth) {
	const auto full = st::videoTimelineHandleWidth;
	const auto compact = st::videoTimelineHandleMinWidth;
	const auto threshold = full * kCompactHandleSpans;
	if (selectionWidth >= threshold) {
		return full;
	}
	return compact + int(base::SafeRound(
		(full - compact) * std::max(selectionWidth, 0) / float64(threshold)));
}

class DurationInput final : public Ui::MaskedInputField {
public:
	DurationInput(QWidget *parent, const QString &value);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

DurationInput::DurationInput(QWidget *parent, const QString &value)
: MaskedInputField(
	parent,
	st::videoTimelineDurationField,
	rpl::single(QString()),
	value) {
}

void DurationInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto result = QString();
	result.reserve(now.size());
	auto cursor = nowCursor;
	for (auto i = 0; i != now.size(); ++i) {
		const auto ch = now[i];
		const auto digit = ch.digitValue();
		if (digit >= 0) {
			result.append(QChar('0' + digit));
		} else if (IsStampChar(ch)) {
			result.append(ch);
		} else if (i < nowCursor) {
			--cursor;
		}
	}
	if (result != now) {
		setCorrectedText(now, nowCursor, result, cursor);
	}
}

} // namespace

TrimTimeline::TrimTimeline(
	not_null<Ui::RpWidget*> parent,
	TrimTimelineDescriptor descriptor)
: RpWidget(parent)
, _duration(std::max(descriptor.duration, crl::time(1)))
, _maxDuration((descriptor.maxDuration > 0)
	? std::min(descriptor.maxDuration, _duration)
	: _duration)
, _minDuration(std::min(descriptor.minDuration, _maxDuration))
, _trimOnly(descriptor.trimOnly)
, _from(0)
, _till(_maxDuration)
, _cover(0)
, _edgeScrollAnimation([=](crl::time now) { return edgeScrollStep(now); }) {
	setMouseTracking(true);

	_from = std::clamp(
		descriptor.from,
		crl::time(0),
		std::max(_duration - _minDuration, crl::time(0)));
	const auto limit = std::min(_from + _maxDuration, _duration);
	_till = (descriptor.till > _from)
		? std::min(descriptor.till, limit)
		: limit;
	_cover = std::clamp(descriptor.cover, _from, _till);
	const auto &font = st::videoTimelineDurationStyle.font;
	const auto separator = QString::fromUtf8(" – ");
	if (_trimOnly) {
		_labelWidth = font->width(
			Stamp(_duration) + separator + Stamp(_duration));
	}
	const auto widest = Stamp(_duration, kStampPrecision)
		+ separator
		+ Stamp(_duration, kStampPrecision);
	_durationFieldWidth = font->width(widest)
		+ st::videoTimelineDurationFieldSkip;

	sizeValue(
	) | rpl::on_next([=] {
		updateDurationFieldGeometry();
		updateHints();
	}, lifetime());
}

int TrimTimeline::resizeGetHeight(int newWidth) {
	const auto playhead = st::videoTimelinePlayheadOverflow
		+ st::videoTimelinePlayheadOutline;
	if (_trimOnly) {
		return playhead * 2 + st::videoTimelineTrimStripHeight;
	}
	return st::videoTimelineLabelHeight
		+ st::videoTimelineLabelSkip
		+ st::videoTimelineStripHeight
		+ playhead;
}

QRect TrimTimeline::stripRect() const {
	const auto handle = st::videoTimelineHandleWidth;
	if (_trimOnly) {
		const auto reserved = _durationField
			? _durationFieldWidth
			: _labelWidth;
		const auto label = reserved + st::videoTimelineTrimLabelSkip;
		return QRect(
			handle,
			st::videoTimelinePlayheadOverflow
				+ st::videoTimelinePlayheadOutline,
			std::max(width() - handle * 2 - label, 1),
			st::videoTimelineTrimStripHeight);
	}
	const auto top = st::videoTimelineLabelHeight
		+ st::videoTimelineLabelSkip;
	return QRect(
		handle,
		top,
		std::max(width() - handle * 2, 1),
		st::videoTimelineStripHeight);
}

QRect TrimTimeline::labelRect() const {
	if (_trimOnly) {
		return QRect(width() - _labelWidth, 0, _labelWidth, height());
	}
	return QRect(0, 0, width(), st::videoTimelineLabelHeight);
}

TrimTimeline::DurationLabel TrimTimeline::durationLabel() const {
	const auto text = (_till - _from >= _duration)
		? Stamp(_till - _from)
		: (Stamp(_from) + QString::fromUtf8(" – ") + Stamp(_till));
	const auto label = labelRect();
	const auto &font = st::videoTimelineDurationStyle.font;
	const auto width = font->width(text);
	const auto top = label.y() + (label.height() - font->height) / 2;
	if (_trimOnly) {
		return {
			.text = text,
			.rect = QRect(
				rect::right(label) - width,
				top,
				width,
				font->height),
		};
	}

	const auto sizeWidth = _sizeLabel.isEmpty()
		? 0
		: font->width(_sizeLabel);
	const auto skip = st::videoTimelineSizeSkip;
	const auto sizeShown = sizeWidth
		&& (width + skip + sizeWidth <= label.width());
	const auto available = sizeShown
		? (label.width() - sizeWidth - skip)
		: label.width();
	const auto shown = (width <= available)
		? text
		: font->elided(text, available);
	const auto shownWidth = std::min(width, available);
	const auto strip = stripRect();
	const auto visibleLeft = std::max(xAt(_from), strip.x());
	const auto visibleRight = std::min(xAt(_till), rect::right(strip));
	const auto center = (visibleLeft + visibleRight) / 2;
	const auto x = std::clamp(
		center - shownWidth / 2,
		label.x(),
		label.x() + std::max(available - shownWidth, 0));
	return {
		.text = shown,
		.rect = QRect(x, top, shownWidth, font->height),
		.sizeShown = sizeShown,
	};
}

QRect TrimTimeline::durationHitRect() const {
	const auto label = labelRect();
	if (_trimOnly) {
		return label;
	}
	const auto text = durationLabel().rect;
	return QRect(text.x(), label.y(), text.width(), label.height());
}

QRect TrimTimeline::durationFieldRect() const {
	const auto label = labelRect();
	const auto &font = st::videoTimelineDurationStyle.font;
	const auto top = label.y() + (label.height() - font->height) / 2;
	const auto height = font->height;
	const auto width = _durationFieldWidth;
	if (_trimOnly) {
		return QRect(rect::right(label) - width, top, width, height);
	}
	const auto painted = durationLabel();
	const auto available = painted.sizeShown
		? (label.width()
			- font->width(_sizeLabel)
			- st::videoTimelineSizeSkip)
		: label.width();
	const auto center = rect::center(painted.rect).x();
	const auto x = std::clamp(
		center - width / 2,
		label.x(),
		label.x() + std::max(available - width, 0));
	return QRect(x, top, width, height);
}

QString TrimTimeline::durationEditText() const {
	return Stamp(_from, kStampPrecision)
		+ QString::fromUtf8(" – ")
		+ Stamp(_till, kStampPrecision);
}

void TrimTimeline::updateDurationFieldGeometry() {
	if (_durationField) {
		_durationField->setGeometry(durationFieldRect());
	}
}

void TrimTimeline::editDuration() {
	if (_durationField) {
		return;
	}
	_scrollAnimation.stop();
	_durationFocusReturn = QApplication::focusWidget();
	_durationField = base::make_unique_q<DurationInput>(
		this,
		durationEditText());
	const auto field = _durationField.get();
	field->setAlignment(Qt::AlignVCenter
		| (_trimOnly ? Qt::AlignRight : Qt::AlignHCenter));
	field->setGeometry(durationFieldRect());
	field->show();
	field->selectAll();
	field->setFocus();
	base::install_event_filter(field, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get())->key();
			if (key == Qt::Key_Escape) {
				finishDurationEdit(false, true);
				return base::EventFilterResult::Cancel;
			} else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
				finishDurationEdit(true, true);
				return base::EventFilterResult::Cancel;
			}
		} else if (type == QEvent::FocusOut) {
			const auto reason = static_cast<QFocusEvent*>(e.get())->reason();
			if (reason != Qt::PopupFocusReason
				&& reason != Qt::ActiveWindowFocusReason) {
				finishDurationEdit(true, false);
			}
		}
		return base::EventFilterResult::Continue;
	});
	updateHints();
	update();
}

void TrimTimeline::finishDurationEdit(bool apply, bool restoreFocus) {
	const auto field = _durationField.release();
	if (!field) {
		return;
	}
	const auto text = field->text();
	const auto focusReturn = base::take(_durationFocusReturn).data();
	if (restoreFocus && focusReturn && focusReturn->isVisible()) {
		focusReturn->setFocus();
	}
	field->hide();
	field->deleteLater();
	if (apply) {
		applyDurationText(text);
	}
	updateHints();
	update();
}

void TrimTimeline::commitPendingEdit() {
	finishDurationEdit(true, false);
}

void TrimTimeline::applyDurationText(const QString &text) {
	const auto parts = SplitStamps(text);
	if (parts.isEmpty() || parts.size() > 2) {
		return;
	}
	const auto till = ParseStamp(parts.back());
	const auto from = (parts.size() > 1)
		? ParseStamp(parts.front())
		: std::optional<crl::time>(_from);
	if (!from || !till) {
		return;
	}
	setTrim(std::min(*from, *till), std::max(*from, *till));
	if (selectionHiddenLeft() || selectionHiddenRight()) {
		scrollToSelection();
	}
}

void TrimTimeline::moveWindowTo(crl::time center) {
	const auto span = _till - _from;
	const auto half = span / 2;
	const auto from = std::clamp(
		center - half,
		crl::time(0),
		std::max(_duration - span, crl::time(0)));
	if (_from == from) {
		return;
	}
	_from = from;
	_till = from + span;
	setCover(std::clamp(_cover, _from, _till), true);
	_trimChanges.fire_copy(_from);
	updateHints();
}

float64 TrimTimeline::visibleSpan() const {
	return _duration / _zoom;
}

crl::time TrimTimeline::visibleFrom() const {
	return crl::time(base::SafeRound(_visibleFrom));
}

crl::time TrimTimeline::visibleTill() const {
	return std::min(
		crl::time(base::SafeRound(_visibleFrom + visibleSpan())),
		_duration);
}

crl::time TrimTimeline::timeAt(int x) const {
	const auto strip = stripRect();
	if (strip.width() <= 0) {
		return 0;
	}
	const auto shift = std::clamp(x - strip.x(), 0, strip.width());
	return std::clamp(
		crl::time(base::SafeRound(
			_visibleFrom + shift * visibleSpan() / strip.width())),
		crl::time(0),
		_duration);
}

int TrimTimeline::xAt(crl::time time) const {
	const auto strip = stripRect();
	const auto clamped = std::clamp(time, crl::time(0), _duration);
	return strip.x() + int(base::SafeRound(
		(clamped - _visibleFrom) * strip.width() / visibleSpan()));
}

bool TrimTimeline::draggingHead() const {
	return (_grab == Grab::Head);
}

void TrimTimeline::setTrim(crl::time from, crl::time till) {
	const auto minimum = minSelection();
	from = std::clamp(
		from,
		crl::time(0),
		std::max(_duration - minimum, crl::time(0)));
	const auto limit = std::min(from + _maxDuration, _duration);
	till = std::clamp(till, std::min(from + minimum, limit), limit);
	if (_from == from && _till == till) {
		return;
	}
	_from = from;
	_till = till;
	setCover(std::clamp(_cover, _from, _till), true);
	_trimChanges.fire_copy(_from);
	if (_durationField) {
		_durationField->setText(durationEditText());
	}
	updateHints();
	update();
}

void TrimTimeline::setPlaybackPosition(crl::time position) {
	const auto clamped = std::clamp(position, _from, _till);
	if (_playback == clamped) {
		return;
	}
	_playback = clamped;
	update();
}

void TrimTimeline::setSizeLabel(const QString &text) {
	if (_sizeLabel == text) {
		return;
	}
	_sizeLabel = text;
	update();
}

float64 TrimTimeline::maxZoom() const {
	return std::max(_duration / float64(kMinVisibleSpan), 1.);
}

bool TrimTimeline::setVisibleRange(float64 zoom, float64 from) {
	zoom = std::clamp(zoom, 1., maxZoom());
	if (zoom < 1. + kZoomSnap) {
		zoom = 1.;
	}
	const auto span = _duration / zoom;
	from = (zoom > 1.) ? std::clamp(from, 0., _duration - span) : 0.;
	if (_zoom == zoom && _visibleFrom == from) {
		return false;
	}
	_zoom = zoom;
	_visibleFrom = from;
	visibleRangeChanged();
	if (grabMovesSelection()) {
		applyGrab(_dragPosition);
	}
	updateDurationFieldGeometry();
	updateHints();
	update();
	return true;
}

void TrimTimeline::zoomBy(float64 factor, int anchorX) {
	_scrollAnimation.stop();
	const auto strip = stripRect();
	if (strip.width() <= 0 || factor <= 0.) {
		return;
	}
	const auto shift = std::clamp(anchorX - strip.x(), 0, strip.width());
	const auto anchor = _visibleFrom + shift * visibleSpan() / strip.width();
	const auto zoom = std::clamp(_zoom * factor, 1., maxZoom());
	const auto span = _duration / zoom;
	setVisibleRange(zoom, anchor - shift * span / strip.width());
}

bool TrimTimeline::scrollBy(float64 pixels) {
	_scrollAnimation.stop();
	const auto strip = stripRect();
	if (strip.width() <= 0 || _zoom <= 1.) {
		return false;
	}
	return setVisibleRange(
		_zoom,
		_visibleFrom + pixels * visibleSpan() / strip.width());
}

void TrimTimeline::updateEdgeScroll(QPoint position) {
	const auto strip = stripRect();
	const auto x = position.x();
	const auto right = rect::right(strip);
	_edgeOvershoot = (x < strip.x())
		? (x - strip.x())
		: (x > right)
		? (x - right)
		: 0;
	const auto active = _edgeOvershoot
		&& (_zoom > 1.)
		&& grabMovesSelection();
	if (!active) {
		_edgeScrollAnimation.stop();
	} else if (!_edgeScrollAnimation.animating()) {
		_edgeScrollLast = crl::now();
		_edgeScrollAnimation.start();
	}
}

bool TrimTimeline::edgeScrollStep(crl::time now) {
	const auto dt = std::min(now - _edgeScrollLast, kEdgeScrollFrame);
	_edgeScrollLast = now;
	if (dt <= 0) {
		return true;
	}
	const auto strip = stripRect();
	const auto seconds = dt / 1000.;
	const auto limit = std::max(
		strip.width() * kEdgeScrollMaxSpeed * seconds,
		1.);
	const auto pixels = std::clamp(
		_edgeOvershoot * kEdgeScrollSpeed * seconds,
		-limit,
		limit);
	return !grabClamped(pixels > 0.) && scrollBy(pixels);
}

bool TrimTimeline::grabClamped(bool forward) const {
	const auto minimum = minSelection();
	switch (_grab) {
	case Grab::Left:
		return forward ? (_from >= _till - minimum) : (_from <= 0);
	case Grab::Right:
		return forward ? (_till >= _duration) : (_till <= _from + minimum);
	case Grab::Head:
		return forward ? (_cover >= _till) : (_cover <= _from);
	case Grab::Window:
		return forward ? (_till >= _duration) : (_from <= 0);
	case Grab::None:
	case Grab::Scroll:
	case Grab::Hint:
	case Grab::Label: return true;
	}
	return true;
}

void TrimTimeline::visibleRangeChanged() {
}

bool TrimTimeline::selectionHiddenLeft() const {
	return (_zoom > 1.) && (xAt(_till) <= stripRect().x());
}

bool TrimTimeline::selectionHiddenRight() const {
	const auto strip = stripRect();
	return (_zoom > 1.) && (xAt(_from) >= rect::right(strip));
}

QRect TrimTimeline::hintRect(bool left) const {
	const auto strip = stripRect();
	const auto size = QSize(
		st::videoTimelineHintWidth,
		st::videoTimelineHintHeight);
	const auto skip = st::videoTimelineHintSkip;
	const auto x = left
		? (strip.x() + skip)
		: (rect::right(strip) - skip - size.width());
	return QRect(
		QPoint(x, strip.y() + (strip.height() - size.height()) / 2),
		size);
}

void TrimTimeline::updateHints() {
	const auto toggle = [&](
			Ui::Animations::Simple &animation,
			bool &shown,
			bool now) {
		if (shown == now) {
			return;
		}
		shown = now;
		animation.start(
			[=] { update(); },
			now ? 0. : 1.,
			now ? 1. : 0.,
			kHintDuration);
	};
	toggle(_hintLeft, _hintLeftShown, selectionHiddenLeft());
	toggle(_hintRight, _hintRightShown, selectionHiddenRight());
}

void TrimTimeline::scrollToSelection() {
	const auto span = visibleSpan();
	const auto target = (_till - _from <= span)
		? ((_from + _till) / 2. - span / 2.)
		: float64(_from);
	const auto from = std::clamp(target, 0., _duration - span);
	_scrollAnimation.start(
		[=] { setVisibleRange(_zoom, _scrollAnimation.value(from)); },
		_visibleFrom,
		from,
		kScrollToSelectionDuration,
		anim::easeOutCubic);
}

bool TrimTimeline::grabMovesSelection() const {
	return (_grab == Grab::Left)
		|| (_grab == Grab::Right)
		|| (_grab == Grab::Head)
		|| (_grab == Grab::Window);
}

TrimTimeline::Grab TrimTimeline::grabAt(
		QPoint position,
		Qt::KeyboardModifiers modifiers) const {
	if (!_durationField && durationHitRect().contains(position)) {
		return Grab::Label;
	} else if (modifiers & (Qt::ShiftModifier | Qt::AltModifier)) {
		return Grab::Window;
	}
	const auto strip = stripRect();
	const auto slop = st::videoTimelineHandleHitSlop;
	const auto x = position.x();
	const auto stripLeft = strip.x() - st::videoTimelineHandleWidth;
	const auto stripRight = strip.x()
		+ strip.width()
		+ st::videoTimelineHandleWidth;
	if (x < stripLeft - slop || x > stripRight + slop) {
		return Grab::None;
	} else if (_hintLeftShown && hintRect(true).contains(position)) {
		return Grab::Hint;
	} else if (_hintRightShown && hintRect(false).contains(position)) {
		return Grab::Hint;
	}
	const auto left = xAt(_from);
	const auto right = xAt(_till);
	const auto handle = HandleWidth(right - left);
	const auto stripEnd = rect::right(strip);
	const auto leftShown = (left >= strip.x()) && (left <= stripEnd);
	const auto rightShown = (right >= strip.x()) && (right <= stripEnd);

	const auto span = std::max(right - left, 1);
	const auto inside = std::min(int(slop), span / 3);
	if (leftShown && x >= left - handle - slop && x <= left + inside) {
		return Grab::Left;
	} else if (rightShown
		&& x <= right + handle + slop
		&& x >= right - inside) {
		return Grab::Right;
	} else if (x > left && x < right) {
		return Grab::Head;
	} else if (_zoom > 1.) {
		return Grab::Scroll;
	}
	return Grab::None;
}

crl::time TrimTimeline::minSelection() const {
	return std::clamp(
		std::max(_minDuration, kMinSelection),
		crl::time(0),
		_maxDuration);
}

void TrimTimeline::updateCursor(Grab grab) {
	setCursor((grab == Grab::None)
		? style::cur_default
		: (grab == Grab::Label)
		? style::cur_text
		: (grab == Grab::Hint)
		? style::cur_pointer
		: (grab != Grab::Scroll)
		? style::cur_sizehor
		: (_grab == Grab::Scroll)
		? Qt::ClosedHandCursor
		: Qt::OpenHandCursor);
}

void TrimTimeline::mousePressEvent(QMouseEvent *e) {
	const auto position = e->pos();
	if (_durationField) {
		finishDurationEdit(true, true);
		return;
	} else if (_grab != Grab::None) {
		return;
	} else if (e->button() == Qt::MiddleButton) {
		if (_zoom <= 1.) {
			return;
		}
		_scrollAnimation.stop();
		_grab = Grab::Scroll;
		_grabButton = e->button();
		_dragPosition = position;
		updateCursor(_grab);
		return;
	} else if (e->button() != Qt::LeftButton) {
		return;
	}
	_grab = grabAt(position, e->modifiers());
	_grabButton = e->button();
	_dragPosition = position;
	if (_grab == Grab::None) {
		return;
	}
	_scrollAnimation.stop();
	if (_grab == Grab::Scroll) {
		updateCursor(_grab);
		return;
	} else if (_grab == Grab::Hint) {
		_hintGrabLeft = hintRect(true).contains(position);
		updateCursor(_grab);
		return;
	} else if (_grab == Grab::Label) {
		updateCursor(_grab);
		return;
	} else if (_grab == Grab::Left) {
		_grabShift = position.x() - xAt(_from);
	} else if (_grab == Grab::Right) {
		_grabShift = position.x() - xAt(_till);
	} else if (_grab == Grab::Window) {
		_grabShift = position.x() - (xAt(_from) + xAt(_till)) / 2;
	} else {
		_grabShift = 0;
	}
	updateCursor(_grab);
	if (_grab == Grab::Head) {
		headGrabChanged(true);
	}
	_draggingChanges.fire(true);
	applyGrab(position);
}

void TrimTimeline::mouseMoveEvent(QMouseEvent *e) {
	const auto position = e->pos();
	if (_grab != Grab::None && !(e->buttons() & _grabButton)) {
		releaseGrab();
	}
	if (_grab == Grab::None) {
		updateCursor(grabAt(position, e->modifiers()));
		return;
	} else if (_grab == Grab::Scroll) {
		scrollBy(_dragPosition.x() - position.x());
		_dragPosition = position;
		return;
	} else if (_grab == Grab::Hint || _grab == Grab::Label) {
		return;
	}
	_dragPosition = position;
	applyGrab(position);
	updateEdgeScroll(position);
}

void TrimTimeline::mouseReleaseEvent(QMouseEvent *e) {
	if (_grab == Grab::None || e->button() != _grabButton) {
		return;
	}
	const auto hintClicked = (_grab == Grab::Hint)
		&& hintRect(_hintGrabLeft).contains(e->pos())
		&& (_hintGrabLeft ? _hintLeftShown : _hintRightShown);
	const auto labelClicked = (_grab == Grab::Label)
		&& durationHitRect().contains(e->pos());
	releaseGrab();
	if (hintClicked) {
		scrollToSelection();
	} else if (labelClicked) {
		editDuration();
	}
	updateCursor(grabAt(e->pos(), e->modifiers()));
}

void TrimTimeline::releaseGrab() {
	if (_grab == Grab::None) {
		return;
	}
	const auto wasHead = (_grab == Grab::Head);
	const auto wasSelection = grabMovesSelection();
	_grab = Grab::None;
	_grabButton = Qt::NoButton;
	_grabShift = 0;
	_edgeScrollAnimation.stop();
	if (wasHead) {
		headGrabChanged(false);
	}
	if (wasSelection) {
		_draggingChanges.fire(false);
	}
	update();
}

void TrimTimeline::wheelEvent(QWheelEvent *e) {
	const auto angle = e->angleDelta();
	const auto delta = Ui::ScrollDeltaF(e);
	const auto locked = _wheelDirectionLock.update(e->phase(), delta);
	const auto horizontal = locked
		? (*locked == Qt::Horizontal)
		: (std::abs(angle.x()) > std::abs(angle.y()));
	if (horizontal || e->modifiers().testFlag(Qt::ShiftModifier)) {
		scrollBy(-(horizontal ? delta.x() : delta.y()));
	} else if (angle.y()) {
		const auto steps = angle.y()
			/ float64(QWheelEvent::DefaultDeltasPerStep);
		zoomBy(std::pow(kZoomStep, steps), int(e->position().x()));
	}
	e->accept();
}

bool TrimTimeline::eventHook(QEvent *e) {
	const auto type = e->type();
	if (type == QEvent::ContextMenu || type == QEvent::Hide) {
		releaseGrab();
	} else if (type == QEvent::NativeGesture) {
		const auto gesture = static_cast<QNativeGestureEvent*>(e);
		const auto type = gesture->gestureType();
		if (type == Qt::ZoomNativeGesture) {
			const auto global = gesture->globalPosition().toPoint();
			zoomBy(1. + gesture->value(), mapFromGlobal(global).x());
			return true;
		} else if (type == Qt::SmartZoomNativeGesture) {
			setVisibleRange(1., 0.);
			return true;
		}
	}
	return RpWidget::eventHook(e);
}

void TrimTimeline::leaveEventHook(QEvent *e) {
	if (_grab == Grab::None) {
		setCursor(style::cur_default);
	}
}

void TrimTimeline::applyGrab(QPoint position) {
	const auto x = position.x() - _grabShift;
	const auto at = ((_grab == Grab::Left) && (x == xAt(_from)))
		? _from
		: ((_grab == Grab::Right) && (x == xAt(_till)))
		? _till
		: timeAt(x);
	const auto minimum = minSelection();
	switch (_grab) {
	case Grab::Left: {
		const auto highest = std::max(_till - minimum, crl::time(0));
		_from = std::clamp(at, crl::time(0), highest);
		if (_till - _from > _maxDuration) {
			_till = _from + _maxDuration;
		}
		setCover(std::clamp(_cover, _from, _till), true);
		_trimChanges.fire_copy(_from);
		updateHints();
	} break;
	case Grab::Right: {
		const auto lowest = std::min(_from + minimum, _duration);
		_till = std::clamp(at, lowest, _duration);
		if (_till - _from > _maxDuration) {
			_from = _till - _maxDuration;
		}
		setCover(std::clamp(_cover, _from, _till), true);
		_trimChanges.fire_copy(_till);
		updateHints();
	} break;
	case Grab::Head: {
		setCover(std::clamp(at, _from, _till), true);
	} break;
	case Grab::Window: {
		moveWindowTo(at);
	} break;
	case Grab::None:
	case Grab::Scroll:
	case Grab::Hint: return;
	}
	update();
}

void TrimTimeline::setCover(crl::time cover, bool notify) {
	if (_cover == cover) {
		return;
	}
	_cover = cover;
	_playback = cover;
	if (notify) {
		_coverChanges.fire_copy(_cover);
	}
	update();
}

void TrimTimeline::paintOverlay(QPainter &p) {
}

void TrimTimeline::headGrabChanged(bool grabbed) {
}

void TrimTimeline::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto strip = stripRect();
	if (strip.isEmpty()) {
		return;
	}
	auto path = QPainterPath();
	const auto radius = st::videoTimelineRadius;
	path.addRoundedRect(QRectF(strip), radius, radius);
	p.setClipPath(path);
	paintStrip(p, strip);
	p.setClipping(false);

	paintSelection(p, strip);
	p.setClipPath(path);
	paintOverview(p, strip);
	p.setClipping(false);
	paintHead(p, strip);
	paintHints(p);
	paintDuration(p);
	paintOverlay(p);
}

void TrimTimeline::paintSelection(QPainter &p, const QRect &strip) {
	const auto left = xAt(_from);
	const auto right = xAt(_till);
	const auto radius = st::videoTimelineRadius;
	const auto stripLeft = strip.x();
	const auto stripRight = rect::right(strip);
	const auto dimLeft = std::clamp(left, stripLeft, stripRight);
	const auto dimRight = std::clamp(right, stripLeft, stripRight);

	if (dimLeft > stripLeft) {
		p.fillRect(
			QRect(stripLeft, strip.y(), dimLeft - stripLeft, strip.height()),
			st::videoTimelineDimBg);
	}
	if (dimRight < stripRight) {
		p.fillRect(
			QRect(dimRight, strip.y(), stripRight - dimRight, strip.height()),
			st::videoTimelineDimBg);
	}

	const auto handle = HandleWidth(right - left);
	const auto border = st::videoTimelineHandleGripWidth;
	const auto outer = QRectF(
		left - handle,
		strip.y(),
		(right - left) + handle * 2,
		strip.height());
	auto frame = QPainterPath();
	frame.addRoundedRect(outer, radius, radius);
	auto inner = QPainterPath();
	inner.addRect(QRectF(
		left,
		strip.y() + border,
		std::max(right - left, 0),
		std::max(strip.height() - border * 2, 0)));

	const auto clipLeft = (left >= stripLeft)
		? (stripLeft - handle)
		: stripLeft;
	const auto clipRight = (right <= stripRight)
		? (stripRight + handle)
		: stripRight;
	p.setClipRect(clipLeft, 0, clipRight - clipLeft, height());
	p.setPen(Qt::NoPen);
	p.setBrush(st::videoTimelineFg);
	p.drawPath(frame.subtracted(inner));

	const auto gripWidth = st::videoTimelineHandleGripWidth;
	const auto gripHeight = std::min(
		int(st::videoTimelineHandleGripHeight),
		strip.height() / 2);
	const auto gripY = strip.y() + (strip.height() - gripHeight) / 2;
	p.setBrush(st::videoTimelineDimBg);
	for (const auto x : { left - handle + (handle - gripWidth) / 2,
			right + (handle - gripWidth) / 2 }) {
		p.drawRoundedRect(
			QRectF(x, gripY, gripWidth, gripHeight),
			gripWidth / 2.,
			gripWidth / 2.);
	}
	p.setClipping(false);
}

void TrimTimeline::paintOverview(QPainter &p, const QRect &strip) {
	const auto shown = std::clamp(
		(_zoom - 1.) / kScrollIndicatorFadeZoom,
		0.,
		1.);
	if (shown <= 0.) {
		return;
	}
	const auto span = visibleSpan();
	const auto height = st::videoTimelineScrollHeight;
	const auto top = strip.y()
		+ strip.height()
		- st::videoTimelineScrollSkip
		- height;
	const auto radius = height / 2.;
	p.setPen(Qt::NoPen);

	auto track = st::videoTimelineFg->c;
	track.setAlphaF(track.alphaF() * kOverviewTrackOpacity * shown);
	p.setBrush(track);
	p.drawRoundedRect(
		QRectF(strip.x(), top, strip.width(), height),
		radius,
		radius);

	const auto minWidth = std::min(
		int(st::videoTimelineScrollMinWidth),
		strip.width());
	const auto width = std::clamp(
		strip.width() * span / _duration,
		float64(minWidth),
		float64(strip.width()));
	const auto ratio = _visibleFrom / std::max(_duration - span, 1.);
	const auto left = strip.x() + (strip.width() - width) * ratio;
	auto thumb = st::videoTimelineFg->c;
	thumb.setAlphaF(thumb.alphaF() * kScrollIndicatorOpacity * shown);
	p.setBrush(thumb);
	p.drawRoundedRect(QRectF(left, top, width, height), radius, radius);

	const auto scale = strip.width() / float64(_duration);
	const auto selectionHeight = st::videoTimelineOverviewSelectionHeight;
	const auto selectionLeft = strip.x() + _from * scale;
	const auto selectionRight = strip.x() + _till * scale;
	auto selection = st::videoTimelineOverviewFg->c;
	selection.setAlphaF(selection.alphaF() * shown);
	p.setBrush(selection);
	p.drawRoundedRect(
		QRectF(
			selectionLeft,
			top + (height - selectionHeight) / 2.,
			std::max(selectionRight - selectionLeft, 1. * selectionHeight),
			selectionHeight),
		selectionHeight / 2.,
		selectionHeight / 2.);
}

void TrimTimeline::paintHints(QPainter &p) {
	const auto paint = [&](bool left, float64 shown) {
		if (shown <= 0.) {
			return;
		}
		const auto hint = QRectF(hintRect(left));
		const auto radius = float64(st::videoTimelineHintRadius);
		auto bg = st::videoTimelineDimBg->c;
		bg.setAlphaF(std::min(bg.alphaF() * kHintBgOpacityBoost, 1.) * shown);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(hint, radius, radius);

		auto fg = st::videoTimelineFg->c;
		fg.setAlphaF(fg.alphaF() * shown);
		p.setPen(QPen(
			fg,
			st::videoTimelineHintStroke,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		const auto arrowWidth = st::videoTimelineHintArrowWidth;
		const auto arrowHeight = st::videoTimelineHintArrowHeight;
		const auto center = rect::center(hint);
		const auto tip = center.x() + (left ? -arrowWidth : arrowWidth) / 2.;
		const auto base = center.x() + (left ? arrowWidth : -arrowWidth) / 2.;
		auto path = QPainterPath();
		path.moveTo(base, center.y() - arrowHeight / 2.);
		path.lineTo(tip, center.y());
		path.lineTo(base, center.y() + arrowHeight / 2.);
		p.drawPath(path);
	};
	paint(true, _hintLeft.value(_hintLeftShown ? 1. : 0.));
	paint(false, _hintRight.value(_hintRightShown ? 1. : 0.));
}

void TrimTimeline::paintHead(QPainter &p, const QRect &strip) {
	const auto width = st::videoTimelinePlayheadWidth;
	const auto outline = st::videoTimelinePlayheadOutline;
	const auto overflow = st::videoTimelinePlayheadOverflow;
	const auto x = std::clamp(
		xAt((_playback >= 0) ? _playback : _cover) - width / 2.,
		1. * xAt(_from),
		1. * std::max(xAt(_till) - width, xAt(_from)));
	const auto head = QRectF(
		x,
		strip.y() - overflow,
		width,
		strip.height() + overflow * 2);
	const auto full = head.marginsAdded(
		{ 1. * outline, 1. * outline, 1. * outline, 1. * outline });
	p.setClipRect(
		strip.x() - outline,
		0,
		strip.width() + outline * 2,
		height());
	p.setPen(Qt::NoPen);
	p.setBrush(st::videoTimelineDimBg);
	p.drawRoundedRect(full, width / 2. + outline, width / 2. + outline);
	p.setBrush(st::videoTimelineFg);
	p.drawRoundedRect(head, width / 2., width / 2.);
	p.setClipping(false);
}

void TrimTimeline::paintDuration(QPainter &p) {
	const auto label = durationLabel();
	p.setFont(st::videoTimelineDurationStyle.font);
	if (label.sizeShown) {
		p.setPen(st::videoTimelineSizeFg);
		p.drawText(
			labelRect(),
			Qt::AlignVCenter | Qt::AlignRight,
			_sizeLabel);
	}
	if (_durationField) {
		return;
	}
	p.setPen(st::videoTimelineDurationFg);
	p.drawText(label.rect, Qt::AlignVCenter | Qt::AlignLeft, label.text);
}

} // namespace Editor
