/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/linear_chart_view.h"

#include "data/data_statistics.h"
#include "statistics/chart_line_view_context.h"
#include "statistics/statistics_common.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_statistics.h"

namespace Statistic {

void PaintLinearChartView(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		ChartLineViewContext &lineViewContext,
		DetailsPaintContext &detailsPaintContext) {
	const auto currentMinHeight = rect.y(); //
	const auto currentMaxHeight = rect.height() + rect.y(); //

	const auto cacheToken = ChartLineViewContext::CacheToken(
		xIndices,
		xPercentageLimits,
		heightLimits,
		rect.size());

	for (const auto &line : chartData.lines) {
		p.setOpacity(lineViewContext.alpha(line.id));
		if (!p.opacity()) {
			continue;
		}
		const auto additionalP = (chartData.xPercentage.size() < 2)
			? 0.
			: (chartData.xPercentage.front() * rect.width());

		////
		const auto &cache = lineViewContext.cache(line.id);

		const auto isSameToken = (cache.lastToken == cacheToken);
		if (isSameToken && cache.hq) {
			p.drawImage(rect.topLeft(), cache.image);
			continue;
		}
		const auto kRatio = lineViewContext.factor;//0.5;
		lineViewContext.setCacheHQ(line.id, isSameToken);
		auto image = QImage();
		image = QImage(
			rect.size() * style::DevicePixelRatio() * (isSameToken ? 1. : kRatio),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(style::DevicePixelRatio());
		image.fill(Qt::transparent);
		// image.fill(Qt::darkRed);
		auto imagePainter = QPainter(&image);
		imagePainter.setRenderHint(QPainter::Antialiasing, true);
		if (isSameToken) {
			// PainterHighQualityEnabler hp(imagePainter);
		} else {
			imagePainter.scale(kRatio, kRatio);
		}
		////

		auto first = true;
		auto chartPath = QPainterPath();

		const auto localStart = std::max(0, int(xIndices.min));
		const auto localEnd = std::min(
			int(chartData.xPercentage.size() - 1),
			int(xIndices.max));

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
			if (i == detailsPaintContext.xIndex) {
				detailsPaintContext.dots.push_back({
					QPointF(xPoint, yPoint),
					line.color,
					p.opacity(),
				});
			}
			if (first) {
				first = false;
				chartPath.moveTo(xPoint, yPoint);
			}
			chartPath.lineTo(xPoint, yPoint);
		}
		imagePainter.translate(-rect.topLeft());
		imagePainter.setPen(QPen(line.color, st::statisticsChartLineWidth));
		imagePainter.setBrush(Qt::NoBrush);
		imagePainter.drawPath(chartPath);

		if (!isSameToken) {
			image = image.scaled(rect.size() * style::DevicePixelRatio(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
		}
		p.drawImage(rect.topLeft(), image);
		lineViewContext.setCacheImage(line.id, std::move(image));
		lineViewContext.setCacheLastToken(line.id, cacheToken);
	}
	p.setPen(st::boxTextFg);
	p.setOpacity(1.);
}

} // namespace Statistic
