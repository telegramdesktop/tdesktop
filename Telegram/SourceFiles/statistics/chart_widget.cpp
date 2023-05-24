/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_widget.h"

#include "statistics/linear_chart_view.h"
#include "ui/rect.h"
#include "styles/style_boxes.h"

namespace Statistic {

namespace {

constexpr auto kHeightLimitsUpdateTimeout = crl::time(320);

[[nodiscard]] int FindMaxValue(
		Data::StatisticalChart &chartData,
		int startXIndex,
		int endXIndex) {
	auto maxValue = 0;
	for (auto &l : chartData.lines) {
		const auto lineMax = l.segmentTree.rMaxQ(startXIndex, endXIndex);
		maxValue = std::max(lineMax, maxValue);
	}
	return maxValue;
}

[[nodiscard]] int FindMinValue(
		Data::StatisticalChart &chartData,
		int startXIndex,
		int endXIndex) {
	auto minValue = std::numeric_limits<int>::max();
	for (auto &l : chartData.lines) {
		const auto lineMin = l.segmentTree.rMinQ(startXIndex, endXIndex);
		minValue = std::min(lineMin, minValue);
	}
	return minValue;
}

void PaintHorizontalLines(
		QPainter &p,
		const ChartHorizontalLinesData &horizontalLine,
		const QRect &r) {
	for (const auto &line : horizontalLine.lines) {
		const auto lineRect = QRect(
			0,
			r.y() + r.height() * line.relativeValue,
			r.x() + r.width(),
			st::lineWidth);
		p.fillRect(lineRect, st::boxTextFg);
	}
}

void PaintCaptionsToHorizontalLines(
		QPainter &p,
		const ChartHorizontalLinesData &horizontalLine,
		const QRect &r) {
	p.setFont(st::boxTextFont->f);
	p.setPen(st::boxTextFg);
	for (const auto &line : horizontalLine.lines) {
		p.drawText(10, r.y() + r.height() * line.relativeValue, line.caption);
	}
}

} // namespace

ChartWidget::ChartWidget(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent) {
	resize(width(), st::confirmMaxHeight);
}

void ChartWidget::setChartData(Data::StatisticalChart chartData) {
	_chartData = chartData;

	{
		const auto startXIndex = _chartData.findStartIndex(
			_chartData.xPercentage.front());
		const auto endXIndex = _chartData.findEndIndex(
			startXIndex,
			_chartData.xPercentage.back());
		setHeightLimits(
			{
				float64(FindMinValue(_chartData, startXIndex, endXIndex)),
				float64(FindMaxValue(_chartData, startXIndex, endXIndex)),
			},
			true);
		update();
	}
}

void ChartWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto r = rect();
	const auto captionRect = r;
	const auto chartRect = r
		- QMargins{ 0, st::boxTextFont->height, 0, st::lineWidth };

	for (const auto &horizontalLine : _horizontalLines) {
		PaintHorizontalLines(p, horizontalLine, chartRect);
	}

	if (_chartData) {
		Statistic::PaintLinearChartView(
			p,
			_chartData,
			chartRect);
	}

	for (const auto &horizontalLine : _horizontalLines) {
		PaintCaptionsToHorizontalLines(p, horizontalLine, chartRect);
	}
}

void ChartWidget::setHeightLimits(Limits newHeight, bool animated) {
	{
		const auto lineMaxHeight = ChartHorizontalLinesData::LookupHeight(
			newHeight.max);
		const auto diff = std::abs(lineMaxHeight - _animateToHeight.max);
		const auto heightChanged = (!newHeight.max)
			|| (diff < _thresholdHeight.max);
		if (!heightChanged && (newHeight.max == _animateToHeight.min)) {
			return;
		}
	}

	const auto newLinesData = ChartHorizontalLinesData(
		newHeight.max,
		newHeight.min,
		true);
	newHeight = Limits{
		.min = newLinesData.lines.front().absoluteValue,
		.max = newLinesData.lines.back().absoluteValue,
	};

	{
		auto k = (_currentHeight.max - _currentHeight.min)
			/ float64(newHeight.max - newHeight.min);
		if (k > 1.) {
			k = 1. / k;
		}
		constexpr auto kUpdateStep1 = 0.1;
		constexpr auto kUpdateStep2 = 0.03;
		constexpr auto kUpdateStep3 = 0.045;
		constexpr auto kUpdateStepThreshold1 = 0.7;
		constexpr auto kUpdateStepThreshold2 = 0.1;
		const auto s = (k > kUpdateStepThreshold1)
			? kUpdateStep1
			: (k < kUpdateStepThreshold2)
			? kUpdateStep2
			: kUpdateStep3;

		const auto refresh = (newHeight.max != _animateToHeight.max)
			|| (_useMinHeight && (newHeight.min != _animateToHeight.min));
		if (refresh) {
			_startFromH = _currentHeight;
			_startFrom = {};
			_minMaxUpdateStep = s;
		}
	}

	_animateToHeight = newHeight;
	measureHeightThreshold();

	{
		const auto now = crl::now();
		if ((now - _lastHeightLimitsChanged) < kHeightLimitsUpdateTimeout) {
			return;
		}
		_lastHeightLimitsChanged = now;
	}

	if (!animated) {
		_currentHeight = newHeight;
		_horizontalLines.clear();
		_horizontalLines.push_back(newLinesData);
		_horizontalLines.back().alpha = 1.;
		return;
	}

	for (auto &horizontalLine : _horizontalLines) {
		horizontalLine.fixedAlpha = horizontalLine.alpha;
	}
	_horizontalLines.push_back(newLinesData);

	const auto callback = [=](float64 value) {
		_horizontalLines.back().alpha = value;

		const auto startIt = begin(_horizontalLines);
		const auto endIt = end(_horizontalLines);
		for (auto it = startIt; it != (endIt - 1); it++) {
			it->alpha = it->fixedAlpha * (1. - (endIt - 1)->alpha);
		}
		update();
	};
	_heightLimitsAnimation.start(callback, 0., 1., st::slideDuration);

}

void ChartWidget::measureHeightThreshold() {
	const auto chartHeight = height();
	if (!_animateToHeight.max || !chartHeight) {
		return;
	}
	_thresholdHeight.max = (_animateToHeight.max / float64(chartHeight))
		* st::boxTextFont->height;
}

} // namespace Statistic
