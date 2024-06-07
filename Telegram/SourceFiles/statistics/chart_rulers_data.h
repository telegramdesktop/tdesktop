/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "statistics/statistics_types.h"

namespace Statistic {

struct ChartRulersData final {
public:
	ChartRulersData(
		ChartValue newMaxHeight,
		ChartValue newMinHeight,
		bool useMinHeight,
		float64 rightRatio,
		Fn<QString(float64)> leftCustomCaption = nullptr,
		Fn<QString(float64)> rightCustomCaption = nullptr);

	void computeRelative(
		ChartValue newMaxHeight,
		ChartValue newMinHeight);

	[[nodiscard]] static ChartValue LookupHeight(ChartValue maxValue);

	struct Line final {
		float64 absoluteValue = 0.;
		float64 relativeValue = 0.;
		QString caption;
		QString scaledLineCaption;
		float64 rightCaptionWidth = 0.;
	};

	std::vector<Line> lines;
	float64 alpha = 0.;
	float64 fixedAlpha = 1.;

};

} // namespace Statistic
