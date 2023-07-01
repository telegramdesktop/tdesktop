/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/linear_chart_view.h"

#include "data/data_statistics.h"
#include "statistics/statistics_common.h"
#include "ui/effects/animation_value_f.h"
#include "styles/style_boxes.h"

namespace Statistic {

void PaintLinearChartView(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimitsNow,
		const Limits &xPercentageLimitsNowY,
		const QRect &rect) {
	const auto offset = 0;
	const auto currentMinHeight = rect.y(); //
	const auto currentMaxHeight = rect.height() + rect.y(); //

	const auto xPercentageLimits = xPercentageLimitsNow;

	for (const auto &line : chartData.lines) {
		const auto additionalP = (chartData.xPercentage.size() < 2)
			? 0.
			: (chartData.xPercentage.front() * rect.width());
		const auto additionalPoints = 0;

		auto first = true;
		auto chartPath = QPainterPath();

		const auto startXIndex = chartData.findStartIndex(
			xPercentageLimits.min);
		const auto endXIndex = chartData.findEndIndex(
			startXIndex,
			xPercentageLimits.max);

		const auto localStart = std::max(0, startXIndex - additionalPoints);
		const auto localEnd = std::min(
			int(chartData.xPercentage.size() - 1),
			endXIndex + additionalPoints);

		auto minY = std::numeric_limits<float64>::max();
		auto maxY = 0.;
		minY = xPercentageLimitsNowY.min;
		maxY = xPercentageLimitsNowY.max;

		for (auto i = localStart; i <= localEnd; i++) {
			if (line.y[i] < 0) {
				continue;
			}
			const auto xPoint = ((chartData.xPercentage[i] - xPercentageLimits.min) / (xPercentageLimits.max - xPercentageLimits.min)) * rect.width()
				- offset;
			const auto yPercentage = (line.y[i] - minY)
				/ float64(maxY - minY);
			const auto yPoint = rect.y() + (1. - yPercentage) * rect.height();
			if (first) {
				first = false;
				chartPath.moveTo(xPoint, yPoint);
			}
			chartPath.lineTo(xPoint, yPoint);
		}
		p.setPen(line.color);
		p.setBrush(Qt::NoBrush);
		p.drawPath(chartPath);
	}
	p.setPen(st::boxTextFg);
}

} // namespace Statistic
