/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

inline constexpr auto kOutlineSegmentsMax = 50;

struct OutlineSegment {
	QBrush brush;
	float64 width = 0.;
};

void PaintOutlineSegments(
	QPainter &p,
	QRectF ellipse,
	const std::vector<OutlineSegment> &segments,
	float64 fromFullProgress = 1.);

void PaintOutlineSegments(
	QPainter &p,
	QRectF rect,
	float64 radius,
	const std::vector<OutlineSegment> &segments);

[[nodiscard]] QLinearGradient UnreadStoryOutlineGradient(QRectF rect = {});

} // namespace Ui
