/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_video_message_seek.h"

#include "ui/arc_angles.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kShowDuration = crl::time(220);
constexpr auto kHideDuration = crl::time(150);
constexpr auto kGrabDuration = crl::time(150);
constexpr auto kGhostDuration = crl::time(150);
constexpr auto kTrackOpacity = 0.2;
constexpr auto kShadowInnerStop = 0.7;
constexpr auto kGhostJump = 0.9;

} // namespace

VideoMessageSeek::VideoMessageSeek(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
}

void VideoMessageSeek::setProgress(float64 progress) {
	_progress = progress;
}

void VideoMessageSeek::setDraggedProgress(float64 progress) {
	if (_progress == progress) {
		return;
	} else if (std::abs(progress - _progress) > kGhostJump) {
		_ghostProgress = _progress;
		_ghostAnimation.start(_repaint, 1., 0., kGhostDuration);
	}
	_progress = progress;
}

void VideoMessageSeek::setGrabbed(bool grabbed) {
	_grabAnimation.start(
		_repaint,
		grabbed ? 0. : 1.,
		grabbed ? 1. : 0.,
		kGrabDuration);
}

bool VideoMessageSeek::grabPoint(QRect rthumb, QPoint point) const {
	const auto radius = rthumb.width() / 2.;
	const auto band = float64(st::historyVideoMessageSeekGrabBand);
	if (radius <= band) {
		return false;
	}
	const auto center = rthumb.center();
	const auto dx = float64(point.x() - center.x());
	const auto dy = float64(point.y() - center.y());
	const auto distance = sqrt(dx * dx + dy * dy);
	return (distance >= radius - band) && (distance <= radius);
}

void VideoMessageSeek::paint(
		Painter &p,
		const Ui::ChatPaintContext &context,
		QRect rthumb,
		bool shown,
		bool seeking,
		float64 playback,
		bool inTTLViewer) {
	const auto st = context.st;
	if (_shown != shown) {
		_shown = shown;
		_shownAnimation.start(
			_repaint,
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			shown ? kShowDuration : kHideDuration,
			shown ? anim::easeOutBack : anim::linear);
	}
	const auto amount = _shownAnimation.value(shown ? 1. : 0.);
	const auto value = (seeking || (playback < 0. && amount > 0.))
		? _progress
		: (playback < 0.)
		? 0.
		: playback;
	if (value <= 0. && amount <= 0.) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto opacity = std::clamp(amount, 0., 1.);
	if (opacity > 0.) {
		paintShadow(p, rthumb, opacity);
	}
	auto pen = st->historyVideoMessageProgressFg()->p;
	const auto was = p.pen();
	const auto normalLine = float64(st::radialLine);
	const auto seekLine = float64(st::historyVideoMessageSeekLine);
	pen.setWidthF(normalLine + (seekLine - normalLine) * opacity);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);

	const auto from = arc::kQuarterLength;
	const auto normalInset = 1.5 * st::radialLine;
	const auto seekInset = st::historyVideoMessageSeekInset;
	const auto stepInside = normalInset
		+ (seekInset - normalInset) * amount;
	const auto arcRect = QRectF(rthumb) - Margins(stepInside);
	if (opacity > 0.) {
		p.setOpacity(kTrackOpacity * opacity);
		p.drawArc(arcRect, 0, arc::kFullLength);
	}
	const auto paintArc = [&](float64 progress, float64 alpha) {
		p.setOpacity(st::historyVideoMessageProgressOpacity * alpha);
		const auto len = std::round(arc::kFullLength
			* (inTTLViewer ? (1. - progress) : -progress));
		p.drawArc(arcRect, from, len);
	};
	const auto ghost = _ghostAnimation.value(0.);
	if (ghost > 0.) {
		paintArc(value, 1. - ghost);
		paintArc(_ghostProgress, ghost);
	} else {
		paintArc(value, 1.);
	}
	if (opacity > 0.) {
		const auto grabbed = _grabAnimation.value(seeking ? 1. : 0.);
		const auto minSize = float64(st::historyVideoMessageSeekDotSizeMin);
		const auto fullSize = float64(st::historyVideoMessageSeekDotSize);
		const auto grabSize = float64(
			st::historyVideoMessageSeekDotSizeGrabbed);
		const auto dotSize = minSize
			+ (fullSize - minSize) * amount
			+ (grabSize - fullSize) * grabbed;
		const auto angle = M_PI / 2. - value * 2. * M_PI;
		const auto radius = arcRect.width() / 2.;
		const auto center = arcRect.center();
		const auto cx = center.x() + radius * cos(angle);
		const auto cy = center.y() - radius * sin(angle);
		p.setOpacity(opacity);
		p.setPen(Qt::NoPen);
		p.setBrush(st->historyVideoMessageProgressFg());
		p.drawEllipse(QPointF(cx, cy), dotSize / 2., dotSize / 2.);
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(was);
	p.setOpacity(1.);
}

void VideoMessageSeek::validateShadow(QSize size) const {
	const auto full = size * style::DevicePixelRatio();
	if (_shadow.size() == full) {
		return;
	}
	_shadow = QImage(full, QImage::Format_ARGB32_Premultiplied);
	_shadow.fill(Qt::transparent);

	auto q = QPainter(&_shadow);
	auto hq = PainterHighQualityEnabler(q);
	const auto rect = QRectF(QPointF(), QSizeF(full));
	const auto edge = anim::with_alpha(
		QColor(0, 0, 0),
		st::historyVideoMessageSeekShadowOpacity);
	auto gradient = QRadialGradient(rect.center(), full.width() / 2.);
	gradient.setColorAt(0., Qt::transparent);
	gradient.setColorAt(kShadowInnerStop, Qt::transparent);
	gradient.setColorAt(1., edge);
	q.setPen(Qt::NoPen);
	q.setBrush(gradient);
	q.drawEllipse(rect);
}

void VideoMessageSeek::paintShadow(
		Painter &p,
		QRect rthumb,
		float64 shown) const {
	validateShadow(rthumb.size());
	p.setOpacity(shown);
	p.drawImage(rthumb, _shadow);
	p.setOpacity(1.);
}

void VideoMessageSeek::unloadHeavyPart() {
	_shadow = QImage();
}

} // namespace HistoryView
