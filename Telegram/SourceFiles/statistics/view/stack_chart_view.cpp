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
	const auto localStart = std::max(0, int(xIndices.min) - 2);
	const auto localEnd = std::min(
		int(chartData.xPercentage.size() - 1),
		int(xIndices.max) + 2);

	const auto fullWidth = rect.width() / (xPercentageLimits.max - xPercentageLimits.min);
	const auto offset = fullWidth * xPercentageLimits.min;
	const auto pp = (chartData.xPercentage.size() < 2)
		? 1.
		: chartData.xPercentage[1] * fullWidth;
	const auto w = chartData.xPercentage[1] * (fullWidth - pp);
	// const auto w = rect.width() / float64(localEnd - localStart);
	const auto r = w / 2.;
	const auto leftStart = chartData.xPercentage[localStart] * (fullWidth - pp) - offset + rect.x();

	auto paths = std::vector<QPainterPath>();
	paths.resize(chartData.lines.size());

	for (auto i = localStart; i <= localEnd; i++) {
		auto chartPoints = QPolygonF();

		const auto xPoint = rect.width()
			* ((chartData.xPercentage[i] - xPercentageLimits.min)
				/ (xPercentageLimits.max - xPercentageLimits.min));
		auto bottom = float64(-rect.y());
		const auto left = leftStart + (i - localStart) * w;

		for (auto j = 0; j < chartData.lines.size(); j++) {
			const auto &line = chartData.lines[j];
			if (line.y[i] <= 0) {
				continue;
			}
			const auto yPercentage = (line.y[i] - heightLimits.min)
				/ float64(heightLimits.max - heightLimits.min);
			const auto yPoint = yPercentage * rect.height() * alpha(line.id);
			// const auto column = QRectF(
			// 	xPoint - r,
			// 	rect.height() - bottom - yPoint,
			// 	w,
			// 	yPoint);
			const auto column = QRectF(
				left,
				rect.height() - bottom - yPoint,
				w,
				yPoint);
			paths[j].addRect(column);

			// p.setPen(Qt::NoPen);
			// p.setBrush(line.color);
			// p.setOpacity(0.3);
			// // p.setOpacity(1.);
			// p.drawRect(column);
			// p.setOpacity(1.);
			bottom += yPoint;
		}
	}

	p.setPen(Qt::NoPen);
	for (auto i = 0; i < paths.size(); i++) {
		p.fillPath(paths[i], chartData.lines[i].color);
	}
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
