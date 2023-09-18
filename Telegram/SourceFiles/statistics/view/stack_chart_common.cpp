/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_chart_common.h"

#include "data/data_statistics.h"
#include "statistics/statistics_common.h"

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

} // namespace Statistic
