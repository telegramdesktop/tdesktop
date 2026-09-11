/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_trim_timeline.h"

#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "styles/style_editor.h"

#include <QtGui/QPainterPath>
#include <QtGui/QtEvents>

namespace Editor {
namespace {

[[nodiscard]] QString Stamp(crl::time value) {
	return Ui::FormatDurationText(int(value / 1000))
		+ '.'
		+ QString::number((value % 1000) / 100);
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
, _cover(0) {
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
	if (_trimOnly) {
		const auto widest = Stamp(_duration)
			+ QString::fromUtf8(" – ")
			+ Stamp(_duration);
		_labelWidth = st::videoTimelineDurationStyle.font->width(widest);
	}
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
		const auto label = _labelWidth + st::videoTimelineTrimLabelSkip;
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
}

crl::time TrimTimeline::timeAt(int x) const {
	const auto strip = stripRect();
	if (strip.width() <= 0) {
		return 0;
	}
	const auto shift = std::clamp(x - strip.x(), 0, strip.width());
	return crl::time(
		base::SafeRound(shift * float64(_duration) / strip.width()));
}

int TrimTimeline::xAt(crl::time time) const {
	const auto strip = stripRect();
	const auto clamped = std::clamp(time, crl::time(0), _duration);
	return strip.x() + int(base::SafeRound(
		clamped * float64(strip.width()) / _duration));
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

TrimTimeline::Grab TrimTimeline::grabAt(
		QPoint position,
		Qt::KeyboardModifiers modifiers) const {
	if (modifiers & (Qt::ShiftModifier | Qt::AltModifier)) {
		return Grab::Window;
	}
	const auto slop = st::videoTimelineHandleHitSlop;
	const auto handle = st::videoTimelineHandleWidth;
	const auto x = position.x();
	const auto left = xAt(_from);
	const auto right = xAt(_till);

	const auto span = std::max(right - left, 1);
	const auto inside = std::min(int(slop), span / 3);
	if (x >= left - handle - slop && x <= left + inside) {
		return Grab::Left;
	} else if (x <= right + handle + slop && x >= right - inside) {
		return Grab::Right;
	} else if (x > left && x < right) {
		return Grab::Head;
	}
	return Grab::None;
}

crl::time TrimTimeline::minSelection() const {
	const auto strip = stripRect();
	// Keeps the head reachable when a long clip squeezes the window.
	const auto pixels = st::videoTimelinePlayheadWidth
		+ st::videoTimelineHandleHitSlop;
	const auto byPixels = (strip.width() > pixels)
		? crl::time(base::SafeRound(
			pixels * float64(_duration) / strip.width()))
		: _duration;
	return std::clamp(
		std::max(_minDuration, byPixels),
		crl::time(0),
		_maxDuration);
}

void TrimTimeline::updateCursor(Grab grab) {
	setCursor((grab == Grab::None) ? style::cur_default : style::cur_sizehor);
}

void TrimTimeline::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto position = e->pos();
	_grab = grabAt(position, e->modifiers());
	if (_grab == Grab::None) {
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
	if (_grab == Grab::None) {
		updateCursor(grabAt(e->pos(), e->modifiers()));
		return;
	}
	applyGrab(e->pos());
}

void TrimTimeline::mouseReleaseEvent(QMouseEvent *e) {
	if (_grab == Grab::None) {
		return;
	}
	const auto wasHead = (_grab == Grab::Head);
	_grab = Grab::None;
	_grabShift = 0;
	if (wasHead) {
		headGrabChanged(false);
	}
	updateCursor(grabAt(e->pos(), e->modifiers()));
	_draggingChanges.fire(false);
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
	} break;
	case Grab::Right: {
		const auto lowest = std::min(_from + minimum, _duration);
		_till = std::clamp(at, lowest, _duration);
		if (_till - _from > _maxDuration) {
			_from = _till - _maxDuration;
		}
		setCover(std::clamp(_cover, _from, _till), true);
		_trimChanges.fire_copy(_till);
	} break;
	case Grab::Head: {
		setCover(std::clamp(at, _from, _till), true);
	} break;
	case Grab::Window: {
		moveWindowTo(at);
	} break;
	case Grab::None: return;
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
	paintHead(p, strip);
	paintDuration(p, strip);
	paintOverlay(p);
}

void TrimTimeline::paintSelection(QPainter &p, const QRect &strip) {
	const auto left = xAt(_from);
	const auto right = xAt(_till);
	const auto radius = st::videoTimelineRadius;

	if (left > strip.x()) {
		p.fillRect(
			QRect(strip.x(), strip.y(), left - strip.x(), strip.height()),
			st::videoTimelineDimBg);
	}
	const auto stripRight = rect::right(strip);
	if (right < stripRight) {
		p.fillRect(
			QRect(right, strip.y(), stripRight - right, strip.height()),
			st::videoTimelineDimBg);
	}

	const auto handle = st::videoTimelineHandleWidth;
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
	p.setPen(Qt::NoPen);
	p.setBrush(st::videoTimelineDimBg);
	p.drawRoundedRect(full, width / 2. + outline, width / 2. + outline);
	p.setBrush(st::videoTimelineFg);
	p.drawRoundedRect(head, width / 2., width / 2.);
}

void TrimTimeline::paintDuration(QPainter &p, const QRect &strip) {
	const auto text = (_till - _from >= _duration)
		? Stamp(_till - _from)
		: (Stamp(_from) + QString::fromUtf8(" – ") + Stamp(_till));
	const auto label = labelRect();
	const auto &font = st::videoTimelineDurationStyle.font;
	const auto width = font->width(text);
	p.setFont(font);
	if (_trimOnly) {
		p.setPen(st::videoTimelineDurationFg);
		p.drawText(label, Qt::AlignVCenter | Qt::AlignRight, text);
		return;
	}

	const auto sizeWidth = _sizeLabel.isEmpty()
		? 0
		: font->width(_sizeLabel);
	const auto skip = st::videoTimelineSizeSkip;
	const auto sizeShown = sizeWidth
		&& (width + skip + sizeWidth <= label.width());
	if (sizeShown) {
		p.setPen(st::videoTimelineSizeFg);
		p.drawText(label, Qt::AlignVCenter | Qt::AlignRight, _sizeLabel);
	}
	const auto available = sizeShown
		? (label.width() - sizeWidth - skip)
		: label.width();
	const auto shown = (width <= available)
		? text
		: font->elided(text, available);
	const auto shownWidth = std::min(width, available);
	const auto center = (xAt(_from) + xAt(_till)) / 2;
	const auto x = std::clamp(
		center - shownWidth / 2,
		label.x(),
		label.x() + std::max(available - shownWidth, 0));
	p.setPen(st::videoTimelineDurationFg);
	p.drawText(
		QRect(x, label.y(), shownWidth, label.height()),
		Qt::AlignVCenter | Qt::AlignLeft,
		shown);
}

} // namespace Editor
