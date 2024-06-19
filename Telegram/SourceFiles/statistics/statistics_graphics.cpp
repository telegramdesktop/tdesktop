/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_graphics.h"

#include "data/data_statistics_chart.h"
#include "ui/effects/credits_graphics.h" // GenerateStars.
#include "ui/painter.h"
#include "styles/style_basic.h"
#include "styles/style_statistics.h"

namespace Statistic {

QImage ChartCurrencyIcon(
		const Data::StatisticalChart &chartData,
		std::optional<QColor> color) {
	auto result = QImage();
	const auto iconSize = st::statisticsCurrencyIcon.size();
	if (chartData.currency == Data::StatisticalCurrency::Ton) {
		result = QImage(
			iconSize * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		result.fill(Qt::transparent);
		{
			auto p = Painter(&result);
			if (const auto w = iconSize.width(); w && color) {
				st::statisticsCurrencyIcon.paint(p, 0, 0, w, *color);
			} else {
				st::statisticsCurrencyIcon.paint(p, 0, 0, iconSize.width());
			}
		}
	} else if (chartData.currency == Data::StatisticalCurrency::Credits) {
		return Ui::GenerateStars(iconSize.height(), 1);
	}
	return result;
}

} // namespace Statistic
