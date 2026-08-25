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

constexpr auto kShowDuration = crl::time(200);
constexpr auto kTrackOpacity = 0.2;

} // namespace

VideoMessageSeek::VideoMessageSeek(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
}

void VideoMessageSeek::setProgress(float64 progress) {
	_progress = progress;
}

void VideoMessageSeek::toggleShown(bool shown) {
	_shownAnimation.start(
		_repaint,
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		kShowDuration);
}

void VideoMessageSeek::paint(
		Painter &p,
		const Ui::ChatPaintContext &context,
		QRect rthumb,
		bool seeking,
		float64 playback,
		bool inTTLViewer) {
	const auto st = context.st;
	const auto amount = _shownAnimation.value(seeking ? 1. : 0.);
	const auto value = (seeking || (playback < 0. && amount > 0.))
		? _progress
		: (playback < 0.)
		? 0.
		: playback;
	if (value <= 0. && amount <= 0.) {
		return;
	}
	auto pen = st->historyVideoMessageProgressFg()->p;
	const auto was = p.pen();
	pen.setWidth(st::radialLine);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);

	const auto from = arc::kQuarterLength;
	const auto normalInset = 1.5 * st::radialLine;
	const auto seekInset = st::historyVideoMessageSeekInset;
	const auto stepInside = normalInset
		+ (seekInset - normalInset) * amount;
	const auto arcRect = QRectF(rthumb) - Margins(stepInside);
	auto hq = PainterHighQualityEnabler(p);
	if (amount > 0.) {
		p.setOpacity(kTrackOpacity * amount);
		p.drawArc(arcRect, 0, arc::kFullLength);
	}
	p.setOpacity(st::historyVideoMessageProgressOpacity);
	const auto len = std::round(arc::kFullLength
		* (inTTLViewer ? (1. - value) : -value));
	p.drawArc(arcRect, from, len);
	if (amount > 0.) {
		const auto dotSize = float64(st::historyVideoMessageSeekDotSize);
		const auto angle = M_PI / 2. - value * 2. * M_PI;
		const auto radius = arcRect.width() / 2.;
		const auto center = arcRect.center();
		const auto cx = center.x() + radius * cos(angle);
		const auto cy = center.y() - radius * sin(angle);
		p.setOpacity(amount);
		p.setPen(Qt::NoPen);
		p.setBrush(st->historyVideoMessageProgressFg());
		p.drawEllipse(QPointF(cx, cy), dotSize / 2., dotSize / 2.);
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(was);
	p.setOpacity(1.);
}

} // namespace HistoryView
