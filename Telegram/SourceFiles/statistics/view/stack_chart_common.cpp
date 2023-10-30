/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_chart_common.h"

#include "data/data_statistics_chart.h"
#include "statistics/statistics_common.h"
#include "ui/effects/animation_value_f.h"

namespace Statistic {

LeftStartAndStep ComputeLeftStartAndStep(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 xIndexStart) {
	const auto fullWidth = rect.width()
		/ (xPercentageLimits.max - xPercentageLimits.min);
	const auto offset = fullWidth * xPercentageLimits.min;
	const auto p = (chartData.xPercentage.size() < 2)
		? 1.
		: chartData.xPercentage[1] * fullWidth;
	const auto w = chartData.xPercentage[1] * (fullWidth - p);
	const auto leftStart = rect.x()
		+ chartData.xPercentage[xIndexStart] * (fullWidth - p)
		- offset;
	return { leftStart, w };
}

Limits FindStackXIndicesFromRawXPercentages(
		const Data::StatisticalChart &chartData,
		const Limits &rawXPercentageLimits,
		const Limits &zoomedInLimitXIndices) {
	const auto zoomLimit = Limits{
		chartData.xPercentage[zoomedInLimitXIndices.min],
		chartData.xPercentage[zoomedInLimitXIndices.max],
	};
	// Due to a specificity of the stack chart plotting,
	// the right edge has a special offset to the left.
	// This reduces the number of displayed points by 1,
	// but allows the last point to be displayed.
	const auto offset = (zoomLimit.max == 1.) ? 0 : -1;
	const auto rightShrink = (rawXPercentageLimits.max == 1.)
		? ((zoomLimit.max == 1.) ? 0 : 1)
		: 0;
	const auto n = chartData.xPercentage.size();
	auto minIt = -1;
	auto maxIt = n;
	const auto zoomedIn = Limits{
		anim::interpolateF(
			zoomLimit.min,
			zoomLimit.max,
			rawXPercentageLimits.min),
		anim::interpolateF(
			zoomLimit.min,
			zoomLimit.max,
			rawXPercentageLimits.max),
	};
	for (auto i = int(0); i < n; i++) {
		if (minIt < 0) {
			if (chartData.xPercentage[i] > zoomedIn.min) {
				minIt = i;
			}
		}
		if (maxIt >= n) {
			if (chartData.xPercentage[i] > zoomedIn.max) {
				maxIt = i;
			}
		}
	}
	return {
		.min = std::clamp(
			float64(minIt + offset),
			zoomedInLimitXIndices.min,
			zoomedInLimitXIndices.max - rightShrink),
		.max = std::clamp(
			float64(maxIt + offset),
			zoomedInLimitXIndices.min,
			zoomedInLimitXIndices.max - rightShrink),
	};
}

} // namespace Statistic
