/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Statistic {

struct ChartHorizontalLinesData final {
public:
	ChartHorizontalLinesData(
		int newMaxHeight,
		int newMinHeight,
		bool useMinHeight);

	void computeRelative(
		int newMaxHeight,
		int newMinHeight);

	[[nodiscard]] static int LookupHeight(int maxValue);

	struct Line final {
		float64 absoluteValue = 0.;
		float64 relativeValue = 0.;
		QString caption;
	};

	std::vector<Line> lines;
	float64 alpha = 0.;
	float64 fixedAlpha = 1.;

};

} // namespace Statistic
