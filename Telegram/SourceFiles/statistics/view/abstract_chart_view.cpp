/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_common.h"
#include "statistics/view/abstract_chart_view.h"

#include "data/data_statistics_chart.h"

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

} // namespace Statistic
