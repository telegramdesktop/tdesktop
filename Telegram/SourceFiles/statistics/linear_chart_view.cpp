/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/linear_chart_view.h"

#include "data/data_statistics.h"
#include "statistics/statistics_common.h"
#include "styles/style_boxes.h"

namespace Statistic {

void PaintLinearChartView(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect) {
	const auto offset = 0;
	const auto currentMinHeight = rect.y(); //
	const auto currentMaxHeight = rect.height() + rect.y(); //

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

		for (auto i = localStart; i <= localEnd; i++) {
			if (line.y[i] < 0) {
				continue;
			}
			const auto xPoint = chartData.xPercentage[i] * rect.width()
				- offset;
			const auto yPercentage = (line.y[i] - line.minValue)
				/ float64(line.maxValue - line.minValue);
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
