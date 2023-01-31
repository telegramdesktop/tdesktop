/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/toggle_arrow.h"

#include <QtCore/QtMath>

namespace Ui {

[[nodiscard]] QPainterPath ToggleUpDownArrowPath(
		float64 x,
		float64 y,
		float64 size,
		float64 fourStrokes,
		float64 progress) {
	const auto size2 = size / 2.;
	const auto stroke = (fourStrokes / 4.) / M_SQRT2;
	const auto left = x - size;
	const auto right = x + size;
	const auto bottom = y + size2;
	constexpr auto kPointCount = 6;
	auto points = std::array<QPointF, kPointCount>{ {
		{ left - stroke, bottom - stroke },
		{ x, bottom - stroke - size - stroke },
		{ right + stroke, bottom - stroke },
		{ right - stroke, bottom + stroke },
		{ x, bottom + stroke - size + stroke },
		{ left + stroke, bottom + stroke }
	} };
	const auto alpha = (progress - 1.) * M_PI;
	const auto cosalpha = cos(alpha);
	const auto sinalpha = sin(alpha);
	for (auto &point : points) {
		auto px = point.x() - x;
		auto py = point.y() - y;
		point.setX(x + px * cosalpha - py * sinalpha);
		point.setY(y + py * cosalpha + px * sinalpha);
	}
	auto path = QPainterPath();
	path.moveTo(points.front());
	for (int i = 1; i != kPointCount; ++i) {
		path.lineTo(points[i]);
	}
	path.lineTo(points.front());

	return path;
}

} // namespace Ui
