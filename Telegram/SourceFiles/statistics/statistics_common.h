/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Statistic {

struct Limits final {
	float64 min = 0;
	float64 max = 0;
};

// Dot on line charts.
struct DetailsPaintContext final {
	int xIndex = -1;

	struct Dot {
		QPointF point;
		QColor color;
		float64 alpha = 0.;
	};
	std::vector<Dot> dots;
};

} // namespace Statistic
