/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_chart_view.h"

#include "data/data_statistics_chart.h"
#include "statistics/chart_lines_filter_controller.h"
#include "statistics/view/stack_chart_common.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"

namespace Statistic {

StackChartView::StackChartView() = default;
StackChartView::~StackChartView() = default;

void StackChartView::paint(QPainter &p, const PaintContext &c) {
	constexpr auto kOffset = float64(2);
	_lastPaintedXIndices = {
		float64(std::max(0., c.xIndices.min - kOffset)),
		float64(std::min(
			float64(c.chartData.xPercentage.size() - 1),
			c.xIndices.max + kOffset)),
	};

	StackChartView::paintChartAndSelected(p, c);
}

void StackChartView::paintChartAndSelected(
		QPainter &p,
		const PaintContext &c) {
	const auto &[localStart, localEnd] = _lastPaintedXIndices;
	const auto &[leftStart, w] = ComputeLeftStartAndStep(
		c.chartData,
		c.xPercentageLimits,
		c.rect,
		localStart);

	const auto opacity = p.opacity();
	auto hq = PainterHighQualityEnabler(p);

	auto bottoms = std::vector<float64>(
		localEnd - localStart + 1,
		-c.rect.y());
	auto selectedBottoms = std::vector<float64>();
	const auto hasSelectedXIndex = !c.footer && (_lastSelectedXIndex >= 0);
	if (hasSelectedXIndex) {
		selectedBottoms = std::vector<float64>(c.chartData.lines.size(), 0);
		constexpr auto kSelectedAlpha = 0.5;
		p.setOpacity(
			anim::interpolateF(1.0, kSelectedAlpha, _lastSelectedXProgress));
	}

	for (auto i = 0; i < c.chartData.lines.size(); i++) {
		const auto &line = c.chartData.lines[i];
		auto path = QPainterPath();
		for (auto x = localStart; x <= localEnd; x++) {
			if (line.y[x] <= 0) {
				continue;
			}
			const auto yPercentage = (line.y[x] - c.heightLimits.min)
				/ float64(c.heightLimits.max - c.heightLimits.min);
			const auto yPoint = yPercentage
				* c.rect.height()
				* linesFilterController()->alpha(line.id);

			const auto bottomIndex = x - localStart;
			const auto column = QRectF(
				leftStart + (x - localStart) * w,
				c.rect.height() - bottoms[bottomIndex] - yPoint,
				w,
				yPoint);
			if (hasSelectedXIndex && (x == _lastSelectedXIndex)) {
				selectedBottoms[i] = column.y();
			}
			path.addRect(column);
			bottoms[bottomIndex] += yPoint;
		}
		p.fillPath(path, line.color);
	}

	for (auto i = 0; i < selectedBottoms.size(); i++) {
		p.setOpacity(opacity);
		if (selectedBottoms[i] <= 0) {
			continue;
		}
		const auto &line = c.chartData.lines[i];
		const auto yPercentage = 0.
			+ (line.y[_lastSelectedXIndex] - c.heightLimits.min)
				/ float64(c.heightLimits.max - c.heightLimits.min);
		const auto yPoint = yPercentage
			* c.rect.height()
			* linesFilterController()->alpha(line.id);

		const auto column = QRectF(
			leftStart + (_lastSelectedXIndex - localStart) * w,
			selectedBottoms[i],
			w,
			yPoint);
		p.fillRect(column, line.color);
	}
}

void StackChartView::paintSelectedXIndex(
		QPainter &p,
		const PaintContext &c,
		int selectedXIndex,
		float64 progress) {
	const auto was = _lastSelectedXIndex;
	_lastSelectedXIndex = selectedXIndex;
	_lastSelectedXProgress = progress;
	if ((_lastSelectedXIndex >= 0) || (was >= 0)) {
		StackChartView::paintChartAndSelected(p, c);
	}
}

int StackChartView::findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 xPos) {
	if ((xPos < rect.x()) || (xPos > (rect.x() + rect.width()))) {
		return _lastSelectedXIndex = -1;
	}
	const auto &[localStart, localEnd] = _lastPaintedXIndices;
	const auto &[leftStart, w] = ComputeLeftStartAndStep(
		chartData,
		xPercentageLimits,
		rect,
		localStart);

	for (auto i = 0; i < chartData.lines.size(); i++) {
		for (auto x = localStart; x <= localEnd; x++) {
			const auto left = leftStart + (x - localStart) * w;
			if ((xPos >= left) && (xPos < (left + w))) {
				return _lastSelectedXIndex = x;
			}
		}
	}
	return _lastSelectedXIndex = -1;
}

AbstractChartView::HeightLimits StackChartView::heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) {
	_cachedHeightLimits = {};
	if (_cachedHeightLimits.ySum.empty()) {
		_cachedHeightLimits.ySum.reserve(chartData.x.size());

		auto maxValueFull = 0;
		for (auto i = 0; i < chartData.x.size(); i++) {
			auto sum = 0;
			for (const auto &line : chartData.lines) {
				if (linesFilterController()->isEnabled(line.id)) {
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
	const auto max = std::max(
		_cachedHeightLimits.ySumSegmentTree.rMaxQ(
			xIndices.min,
			xIndices.max),
		1);
	return {
		.full = _cachedHeightLimits.full,
		.ranged = { 0., float64(max) },
	};
}

} // namespace Statistic
