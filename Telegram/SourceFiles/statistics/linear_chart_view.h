/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct StatisticalChart;
} // namespace Data

namespace Statistic {

struct Limits;

void PaintLinearChartView(
	QPainter &p,
	const Data::StatisticalChart &chartData,
	const Limits &xPercentageLimitsNow,
	const Limits &xPercentageLimitsNowY,
	const QRect &rect);

} // namespace Statistic
