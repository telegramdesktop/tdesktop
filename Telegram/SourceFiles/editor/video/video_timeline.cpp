/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_timeline.h"

#include "media/media_video_frames.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "styles/style_editor.h"

#include <QtGui/QPainterPath>
#include <QtGui/QtEvents>

namespace Editor {
namespace {

constexpr auto kMaxFrames = 24;

} // namespace

VideoTimeline::VideoTimeline(
	not_null<Ui::RpWidget*> parent,
	VideoTimelineDescriptor descriptor)
: RpWidget(parent)
, _descriptor(std::move(descriptor))
, _duration(std::max(_descriptor.duration, crl::time(1)))
, _maxDuration((_descriptor.maxDuration > 0)
	? std::min(_descriptor.maxDuration, _duration)
	: _duration)
, _minDuration(std::min(_descriptor.minDuration, _maxDuration))
, _from(0)
, _till(_maxDuration)
, _cover(0) {
	setMouseTracking(true);

	sizeValue(
	) | rpl::filter([=](QSize size) {
		return !size.isEmpty();
	}) | rpl::on_next([=] {
		reloadFrames();
	}, lifetime());
}

VideoTimeline::~VideoTimeline() {
	if (_framesCancel) {
		_framesCancel->store(true);
	}
}

int VideoTimeline::resizeGetHeight(int newWidth) {
	return st::videoTimelineHeight
		+ st::videoTimelinePlayheadOverflow
		+ st::videoTimelinePlayheadOutline;
}

QRect VideoTimeline::stripRect() const {
	return rect();
}

crl::time VideoTimeline::timeAt(int x) const {
	const auto strip = stripRect();
	if (strip.width() <= 0) {
		return 0;
	}
	const auto shift = std::clamp(x - strip.x(), 0, strip.width());
	return crl::time(
		base::SafeRound(shift * float64(_duration) / strip.width()));
}

int VideoTimeline::xAt(crl::time time) const {
	const auto strip = stripRect();
	const auto clamped = std::clamp(time, crl::time(0), _duration);
	return strip.x() + int(base::SafeRound(
		clamped * float64(strip.width()) / _duration));
}

void VideoTimeline::setPlaybackPosition(crl::time position) {
	const auto clamped = std::clamp(position, _from, _till);
	if (_playback == clamped) {
		return;
	}
	_playback = clamped;
	update();
}

void VideoTimeline::reloadFrames() {
	const auto strip = stripRect();
	const auto height = strip.height();
	const auto dimensions = _descriptor.dimensions;
	if (strip.isEmpty() || dimensions.isEmpty() || height <= 0) {
		return;
	}
	const auto aspectWidth = std::max(
		int(base::SafeRound(
			height * dimensions.width() / float64(dimensions.height()))),
		1);
	const auto count = std::clamp(
		(strip.width() + aspectWidth - 1) / aspectWidth,
		1,
		kMaxFrames);
	const auto frameWidth = (strip.width() + count - 1) / count;
	if (int(_frames.size()) == count && _frameWidth == frameWidth) {
		return;
	}
	if (_framesCancel) {
		_framesCancel->store(true);
	}
	_frameWidth = frameWidth;
	_frames = std::vector<QImage>(count);

	auto positions = std::vector<crl::time>();
	positions.reserve(count);
	for (auto i = 0; i != count; ++i) {
		positions.push_back(crl::time(
			base::SafeRound((i + 0.5) * _duration / count)));
	}
	const auto cancel = std::make_shared<std::atomic<bool>>(false);
	_framesCancel = cancel;

	const auto path = _descriptor.path;
	const auto box = QSize(_frameWidth, height) * style::DevicePixelRatio();
	crl::async([=, weak = base::make_weak(this)] {
		Media::Video::ExtractFrames(path, {
			.positions = positions,
			.box = box,
			.cover = true,
		}, [&](int index, QImage &&frame) {
			if (cancel->load()) {
				return false;
			}
			frame.setDevicePixelRatio(style::DevicePixelRatio());
			crl::on_main(weak, [=, frame = std::move(frame)]() mutable {
				if (cancel->load() || index >= int(_frames.size())) {
					return;
				}
				_frames[index] = std::move(frame);
				update();
			});
			return true;
		});
	});
}

VideoTimeline::Grab VideoTimeline::grabAt(QPoint position) const {
	const auto slop = st::videoTimelineHandleHitSlop;
	const auto handle = st::videoTimelineHandleWidth;
	const auto x = position.x();
	const auto reach = handle / 2 + slop;
	const auto candidates = {
		std::pair{ Grab::Left, std::abs(x - (xAt(_from) + handle / 2)) },
		std::pair{ Grab::Right, std::abs(x - (xAt(_till) - handle / 2)) },
	};
	auto best = Grab::None;
	auto bestDistance = reach + 1;
	for (const auto &[grab, distance] : candidates) {
		if (distance <= reach && distance < bestDistance) {
			best = grab;
			bestDistance = distance;
		}
	}
	if (best != Grab::None) {
		return best;
	}
	return (std::abs(x - xAt(_cover)) <= slop) ? Grab::Head : Grab::None;
}

crl::time VideoTimeline::minSelection() const {
	const auto strip = stripRect();
	const auto pixels = st::videoTimelineHandleWidth * 2
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

void VideoTimeline::updateCursor(Grab grab) {
	setCursor((grab == Grab::None) ? style::cur_default : style::cur_sizehor);
}

void VideoTimeline::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto position = e->pos();
	_grab = grabAt(position);
	if (_grab == Grab::None) {
		_grab = Grab::Head;
		_grabShift = 0;
	} else {
		const auto anchor = (_grab == Grab::Left)
			? xAt(_from)
			: (_grab == Grab::Right)
			? xAt(_till)
			: xAt(_cover);
		_grabShift = position.x() - anchor;
	}
	updateCursor(_grab);
	_draggingChanges.fire(true);
	applyGrab(position);
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *e) {
	if (_grab == Grab::None) {
		updateCursor(grabAt(e->pos()));
		return;
	}
	applyGrab(e->pos());
}

void VideoTimeline::mouseReleaseEvent(QMouseEvent *e) {
	if (_grab == Grab::None) {
		return;
	}
	_grab = Grab::None;
	_grabShift = 0;
	updateCursor(grabAt(e->pos()));
	_draggingChanges.fire(false);
}

void VideoTimeline::leaveEventHook(QEvent *e) {
	if (_grab == Grab::None) {
		setCursor(style::cur_default);
	}
}

void VideoTimeline::applyGrab(QPoint position) {
	const auto at = timeAt(position.x() - _grabShift);
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
	case Grab::None: return;
	}
	update();
}

void VideoTimeline::setCover(crl::time cover, bool notify) {
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

void VideoTimeline::paintEvent(QPaintEvent *e) {
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
	paintFrames(p, strip);
	p.setClipping(false);

	paintSelection(p, strip);
	paintHead(p, strip);
	paintDuration(p, strip);
}

void VideoTimeline::paintFrames(QPainter &p, const QRect &strip) {
	p.fillRect(strip, st::videoTimelinePlaceholderBg);
	if (_frameWidth <= 0) {
		return;
	}
	const auto count = int(_frames.size());
	for (auto i = 0; i != count; ++i) {
		const auto &frame = _frames[i];
		if (frame.isNull()) {
			continue;
		}
		const auto x = strip.x() + i * strip.width() / count;
		p.drawImage(QRect(x, strip.y(), _frameWidth, strip.height()), frame);
	}
}

void VideoTimeline::paintSelection(QPainter &p, const QRect &strip) {
	const auto left = xAt(_from);
	const auto right = xAt(_till);
	const auto radius = st::videoTimelineRadius;

	if (left > strip.x()) {
		p.fillRect(
			QRect(strip.x(), strip.y(), left - strip.x(), strip.height()),
			st::videoTimelineDimBg);
	}
	const auto stripRight = strip.x() + strip.width();
	if (right < stripRight) {
		p.fillRect(
			QRect(right, strip.y(), stripRight - right, strip.height()),
			st::videoTimelineDimBg);
	}

	const auto handle = st::videoTimelineHandleWidth;
	const auto grip = st::videoTimelineHandleGripWidth;
	const auto selected = QRectF(
		left,
		strip.y(),
		std::max(right - left, handle * 2),
		strip.height());
	auto frame = QPainterPath();
	frame.addRoundedRect(selected, radius, radius);
	auto inner = QPainterPath();
	inner.addRect(QRectF(
		selected.x() + handle,
		selected.y() + grip,
		std::max(selected.width() - handle * 2, 0.),
		std::max(selected.height() - grip * 2, 0.)));

	p.setPen(Qt::NoPen);
	p.setBrush(st::videoTimelineFg);
	p.drawPath(frame.subtracted(inner));

	const auto gripWidth = st::videoTimelineHandleGripWidth;
	const auto gripHeight = std::min(
		int(st::videoTimelineHandleGripHeight),
		strip.height() / 2);
	const auto gripY = strip.y() + (strip.height() - gripHeight) / 2;
	p.setBrush(st::videoTimelineDimBg);
	for (const auto x : { left + (handle - gripWidth) / 2,
			right - handle + (handle - gripWidth) / 2 }) {
		p.drawRoundedRect(
			QRectF(x, gripY, gripWidth, gripHeight),
			gripWidth / 2.,
			gripWidth / 2.);
	}
}

void VideoTimeline::paintHead(QPainter &p, const QRect &strip) {
	const auto width = st::videoTimelinePlayheadWidth;
	const auto outline = st::videoTimelinePlayheadOutline;
	const auto overflow = st::videoTimelinePlayheadOverflow;
	const auto handle = st::videoTimelineHandleWidth;
	const auto x = std::clamp(
		xAt(_playback ? _playback : _cover) - width / 2.,
		1. * (xAt(_from) + handle),
		1. * std::max(xAt(_till) - handle - width, xAt(_from) + handle));
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

void VideoTimeline::paintDuration(QPainter &p, const QRect &strip) {
	const auto span = _till - _from;
	const auto text = (span < 60 * crl::time(1000))
		? (Ui::FormatDurationText(int(span / 1000))
			+ '.'
			+ QString::number((span % 1000) / 100))
		: Ui::FormatDurationText(int(span / 1000));
	const auto &font = st::videoTimelineDurationStyle.font;
	const auto padding = st::videoTimelineDurationPadding;
	const auto width = font->width(text)
		+ padding.left()
		+ padding.right();
	const auto height = font->height + padding.top() + padding.bottom();
	const auto center = (xAt(_from) + xAt(_till)) / 2;
	const auto x = std::clamp(
		center - width / 2,
		strip.x(),
		strip.x() + std::max(strip.width() - width, 0));
	const auto y = strip.y() + (strip.height() - height) / 2;
	const auto box = QRectF(x, y, width, height);
	p.setPen(Qt::NoPen);
	p.setBrush(st::videoTimelineDimBg);
	p.drawRoundedRect(box, height / 2., height / 2.);
	p.setPen(st::videoTimelineDurationFg);
	p.setFont(font);
	p.drawText(box, Qt::AlignCenter, text);
}

} // namespace Editor
