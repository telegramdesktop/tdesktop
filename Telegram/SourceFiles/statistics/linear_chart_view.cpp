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
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect) {
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
			const auto xPoint = rect.width()
				* ((chartData.xPercentage[i] - xPercentageLimits.min)
					/ (xPercentageLimits.max - xPercentageLimits.min));
			const auto yPercentage = (line.y[i] - heightLimits.min)
				/ float64(heightLimits.max - heightLimits.min);
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
