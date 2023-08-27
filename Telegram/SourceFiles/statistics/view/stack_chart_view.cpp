/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_chart_view.h"

#include "data/data_statistics.h"

namespace Statistic {
namespace {
} // namespace

StackChartView::StackChartView() = default;

StackChartView::~StackChartView() = default;

void StackChartView::paint(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		bool footer) {
}

void StackChartView::paintSelectedXIndex(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		int selectedXIndex) {
}

void StackChartView::setEnabled(int id, bool enabled, crl::time now) {
}

bool StackChartView::isFinished() const {
	return true;
}

bool StackChartView::isEnabled(int id) const {
	return true;
}

float64 StackChartView::alpha(int id) const {
	return 1.0;
}

AbstractChartView::HeightLimits StackChartView::heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) {
	if (_cachedHeightLimits.ySum.empty()) {
		_cachedHeightLimits.ySum.reserve(chartData.x.size());

		auto maxValueFull = 0;
		for (auto i = 0; i < chartData.x.size(); i++) {
			auto sum = 0;
			for (const auto &line : chartData.lines) {
				if (isEnabled(line.id)) {
					sum += line.y[i];
				}
			}
			_cachedHeightLimits.ySum.push_back(sum);
			maxValueFull = std::max(sum, maxValueFull);
		}

		_cachedHeightLimits.ySumSegmentTree = SegmentTree(
			_cachedHeightLimits.ySum);
		_cachedHeightLimits.full = { 0., float64(maxValueFull) };
	}
	const auto max = _cachedHeightLimits.ySumSegmentTree.rMaxQ(
		xIndices.min,
		xIndices.max);
	return {
		.full = _cachedHeightLimits.full,
		.ranged = { 0., float64(max) },
	};
}

void StackChartView::tick(crl::time now) {
}

} // namespace Statistic
