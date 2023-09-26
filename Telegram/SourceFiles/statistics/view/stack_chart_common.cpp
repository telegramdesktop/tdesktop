/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_chart_common.h"

#include "data/data_statistics.h"
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
	const auto offset = (zoomLimit.max == 1.) ? 0 : -1;
	const auto minIt = ranges::upper_bound(
		chartData.xPercentage,
		anim::interpolateF(
			zoomLimit.min,
			zoomLimit.max,
			rawXPercentageLimits.min));
	const auto maxIt = ranges::upper_bound(
		chartData.xPercentage,
		anim::interpolateF(
			zoomLimit.min,
			zoomLimit.max,
			rawXPercentageLimits.max));
	const auto start = begin(chartData.xPercentage);
	return {
		.min = std::clamp(
			float64(std::distance(start, minIt) + offset),
			zoomedInLimitXIndices.min,
			zoomedInLimitXIndices.max),
		.max = std::clamp(
			float64(std::distance(start, maxIt) + offset),
			zoomedInLimitXIndices.min,
			zoomedInLimitXIndices.max),
	};
}

} // namespace Statistic
