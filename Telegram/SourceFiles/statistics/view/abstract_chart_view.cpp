/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_common.h"
#include "statistics/view/abstract_chart_view.h"

#include "data/data_statistics_chart.h"
#include "statistics/chart_lines_filter_controller.h"

namespace Statistic {

bool CachedSelectedPoints::isSame(int x, const PaintContext &c) const {
	return (lastXIndex == x)
		&& (lastHeightLimits.min == c.heightLimits.min)
		&& (lastHeightLimits.max == c.heightLimits.max)
		&& (lastXLimits.min == c.xPercentageLimits.min)
		&& (lastXLimits.max == c.xPercentageLimits.max);
}

DoubleLineRatios::DoubleLineRatios(bool isDouble) {
	first = second = (isDouble ? 0 : 1);
}

void DoubleLineRatios::init(const Data::StatisticalChart &chartData) {
	if (chartData.lines.size() != 2) {
		first = 1.;
		second = 1.;
	} else {
		const auto firstMax = chartData.lines.front().maxValue;
		const auto secondMax = chartData.lines.back().maxValue;
		if (firstMax > secondMax) {
			first = 1.;
			second = firstMax / float64(secondMax);
		} else {
			first = secondMax / float64(firstMax);
			second = 1.;
		}
	}
}

float64 DoubleLineRatios::ratio(int lineId) const {
	return (lineId == 1) ? first : second;
}

void AbstractChartView::setUpdateCallback(Fn<void()> callback) {
	_updateCallback = std::move(callback);
}

void AbstractChartView::update() {
	if (_updateCallback) {
		_updateCallback();
	}
}

void AbstractChartView::setLinesFilterController(
		std::shared_ptr<LinesFilterController> c) {
	_linesFilterController = std::move(c);
}

auto AbstractChartView::linesFilterController() const
-> std::shared_ptr<LinesFilterController> {
	return _linesFilterController;
}

AbstractChartView::HeightLimits DefaultHeightLimits(
		const DoubleLineRatios &ratios,
		const std::shared_ptr<LinesFilterController> &linesFilter,
		Data::StatisticalChart &chartData,
		Limits xIndices) {
	auto minValue = std::numeric_limits<int>::max();
	auto maxValue = 0;

	auto minValueFull = std::numeric_limits<int>::max();
	auto maxValueFull = 0;
	for (auto &l : chartData.lines) {
		if (!linesFilter->isEnabled(l.id)) {
			continue;
		}
		const auto r = ratios.ratio(l.id);
		const auto lineMax = l.segmentTree.rMaxQ(xIndices.min, xIndices.max);
		const auto lineMin = l.segmentTree.rMinQ(xIndices.min, xIndices.max);
		maxValue = std::max(int(lineMax * r), maxValue);
		minValue = std::min(int(lineMin * r), minValue);

		maxValueFull = std::max(int(l.maxValue * r), maxValueFull);
		minValueFull = std::min(int(l.minValue * r), minValueFull);
	}
	if (maxValue == minValue) {
		maxValue = chartData.maxValue;
		minValue = chartData.minValue;
	}
	return {
		.full = Limits{ float64(minValueFull), float64(maxValueFull) },
		.ranged = Limits{ float64(minValue), float64(maxValue) },
	};
}

} // namespace Statistic
