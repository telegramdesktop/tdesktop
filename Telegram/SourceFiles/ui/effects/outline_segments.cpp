/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/outline_segments.h"

namespace Ui {

void PaintOutlineSegments(
		QPainter &p,
		QRectF ellipse,
		const std::vector<OutlineSegment> &segments,
		float64 fromFullProgress) {
	Expects(!segments.empty());

	p.setBrush(Qt::NoBrush);
	const auto count = std::min(int(segments.size()), kOutlineSegmentsMax);
	if (count == 1) {
		p.setPen(QPen(segments.front().brush, segments.front().width));
		p.drawEllipse(ellipse);
		return;
	}
	const auto small = 160;
	const auto full = arc::kFullLength;
	const auto separator = (full > 1.1 * small * count)
		? small
		: (full / (count * 1.1));
	const auto left = full - (separator * count);
	const auto length = left / float64(count);
	const auto spin = separator * (1. - fromFullProgress);

	auto start = 0. + (arc::kQuarterLength + (separator / 2)) + (3. * spin);
	auto pen = QPen(
		segments.back().brush,
		segments.back().width,
		Qt::SolidLine,
		Qt::RoundCap);
	p.setPen(pen);
	for (auto i = 0; i != count;) {
		const auto &segment = segments[count - (++i)];
		if (!segment.width) {
			start += length + separator;
			continue;
		} else if (pen.brush() != segment.brush
			|| pen.widthF() != segment.width) {
			pen = QPen(
				segment.brush,
				segment.width,
				Qt::SolidLine,
				Qt::RoundCap);
			p.setPen(pen);
		}
		const auto from = int(base::SafeRound(start));
		const auto till = start + length;
		auto added = spin;
		for (; i != count;) {
			start += length + separator;
			const auto &next = segments[count - (++i)];
			if (next.width) {
				--i;
				break;
			}
			added += (separator + length) * (1. - fromFullProgress);
		}
		p.drawArc(ellipse, from, int(base::SafeRound(till + added)) - from);
	}
}

void PaintOutlineSegments(
		QPainter &p,
		QRectF rect,
		float64 radius,
		const std::vector<OutlineSegment> &segments) {
	Expects(!segments.empty());

	p.setBrush(Qt::NoBrush);
	const auto count = std::min(int(segments.size()), kOutlineSegmentsMax);
	if (count == 1 || true) {
		p.setPen(QPen(segments.back().brush, segments.back().width));
		p.drawRoundedRect(rect, radius, radius);
		return;
	}
}

QLinearGradient UnreadStoryOutlineGradient(QRectF rect) {
	auto result = QLinearGradient(rect.topRight(), rect.bottomLeft());
	result.setStops({
		{ 0., st::groupCallLive1->c },
		{ 1., st::groupCallMuted1->c },
	});
	return result;
}

} // namespace Ui
