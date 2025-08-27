/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/bar_chart_view.h"

#include "data/data_statistics_chart.h"
#include "statistics/chart_lines_filter_controller.h"
#include "statistics/view/stack_chart_common.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_statistics.h"

namespace Statistic {

BarChartView::BarChartView(bool isStack)
: _isStack(isStack)
, _cachedLineRatios(false) {
}

BarChartView::~BarChartView() = default;

void BarChartView::paint(QPainter &p, const PaintContext &c) {
	constexpr auto kOffset = float64(2);
	_lastPaintedXIndices = {
		float64(std::max(0., c.xIndices.min - kOffset)),
		float64(std::min(
			float64(c.chartData.xPercentage.size() - 1),
			c.xIndices.max + kOffset)),
	};

	BarChartView::paintChartAndSelected(p, c);
}

void BarChartView::paintChartAndSelected(
		QPainter &p,
		const PaintContext &c) {
	const auto &[localStart, localEnd] = _lastPaintedXIndices;
	const auto &[leftStart, w] = ComputeLeftStartAndStep(
		c.chartData,
		c.xPercentageLimits,
		c.rect,
		localStart);

	p.setClipRect(0, 0, c.rect.width() * 2, rect::bottom(c.rect));

	const auto opacity = p.opacity();
	auto hq = PainterHighQualityEnabler(p);

	auto bottoms = std::vector<float64>(
		localEnd - localStart + 1,
		-c.rect.y());
	auto selectedBottoms = std::vector<float64>();
	const auto hasSelectedXIndex = _isStack
		&& !c.footer
		&& (_lastSelectedXIndex >= 0);
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
			if (line.y[x] <= 0 && _isStack) {
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
			if (_isStack) {
				path.addRect(column);
				bottoms[bottomIndex] += yPoint;
			} else {
				if (path.isEmpty()) {
					path.moveTo(column.topLeft());
				} else {
					path.lineTo(column.topLeft());
				}
				if (x == localEnd) {
					path.lineTo(c.rect.width(), column.y());
				} else {
					path.lineTo(rect::right(column), column.y());
				}
			}
		}
		if (_isStack) {
			p.fillPath(path, line.color);
		} else {
			p.strokePath(path, line.color);
		}
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

	p.setClipping(false);
}

void BarChartView::paintSelectedXIndex(
		QPainter &p,
		const PaintContext &c,
		int selectedXIndex,
		float64 progress) {
	const auto was = _lastSelectedXIndex;
	_lastSelectedXIndex = selectedXIndex;
	_lastSelectedXProgress = progress;

	if ((_lastSelectedXIndex < 0) && (was < 0)) {
		return;
	}

	if (_isStack) {
		BarChartView::paintChartAndSelected(p, c);
	} else if (selectedXIndex >= 0) {
		const auto linesFilter = linesFilterController();
		auto hq = PainterHighQualityEnabler(p);
		auto o = ScopedPainterOpacity(p, progress);
		p.setBrush(st::boxBg);
		const auto r = st::statisticsDetailsDotRadius;
		const auto isSameToken = _selectedPoints.isSame(selectedXIndex, c);
		auto linePainted = false;

		const auto &[localStart, localEnd] = _lastPaintedXIndices;
		const auto &[leftStart, w] = ComputeLeftStartAndStep(
			c.chartData,
			c.xPercentageLimits,
			c.rect,
			localStart);

		for (auto i = 0; i < c.chartData.lines.size(); i++) {
			const auto &line = c.chartData.lines[i];
			const auto lineAlpha = linesFilter->alpha(line.id);
			const auto useCache = isSameToken
				|| (lineAlpha < 1. && !linesFilter->isEnabled(line.id));
			if (!useCache) {
				// Calculate.
				const auto x = _lastSelectedXIndex;
				const auto yPercentage = (line.y[x] - c.heightLimits.min)
					/ float64(c.heightLimits.max - c.heightLimits.min);
				const auto yPoint = (1. - yPercentage) * c.rect.height();

				const auto column = QRectF(
					leftStart + (x - localStart) * w,
					c.rect.height() - 0 - yPoint,
					w,
					yPoint);
				const auto xPoint = column.left() + column.width() / 2.;
				_selectedPoints.points[line.id] = QPointF(xPoint, yPoint)
					+ c.rect.topLeft();
			}

			if (!linePainted && lineAlpha) {
				[[maybe_unused]] const auto o = ScopedPainterOpacity(
					p,
					p.opacity() * progress * kRulerLineAlpha);
				const auto lineRect = QRectF(
					begin(_selectedPoints.points)->second.x()
						- (st::lineWidth / 2.),
					c.rect.y(),
					st::lineWidth,
					c.rect.height());
				p.fillRect(lineRect, st::boxTextFg);
				linePainted = true;
			}

			// Paint.
			auto o = ScopedPainterOpacity(p, lineAlpha * p.opacity());
			p.setPen(QPen(line.color, st::statisticsChartLineWidth));
			p.drawEllipse(_selectedPoints.points[line.id], r, r);
		}
		_selectedPoints.lastXIndex = selectedXIndex;
		_selectedPoints.lastHeightLimits = c.heightLimits;
		_selectedPoints.lastXLimits = c.xPercentageLimits;
	}
}

int BarChartView::findXIndexByPosition(
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

AbstractChartView::HeightLimits BarChartView::heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) {
	if (!_isStack) {
		if (!_cachedLineRatios) {
			_cachedLineRatios.init(chartData);
		}

		return DefaultHeightLimits(
			_cachedLineRatios,
			linesFilterController(),
			chartData,
			xIndices);
	}
	_cachedHeightLimits = {};
	if (_cachedHeightLimits.ySum.empty()) {
		_cachedHeightLimits.ySum.reserve(chartData.x.size());

		auto maxValueFull = ChartValue(0);
		for (auto i = 0; i < chartData.x.size(); i++) {
			auto sum = ChartValue(0);
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
		ChartValue(1));
	return {
		.full = _cachedHeightLimits.full,
		.ranged = { 0., float64(max) },
	};
}

} // namespace Statistic
