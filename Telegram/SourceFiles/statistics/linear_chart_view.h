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
struct DetailsPaintContext;
struct ChartLineViewContext;

void PaintLinearChartView(
	QPainter &p,
	const Data::StatisticalChart &chartData,
	const Limits &xIndices,
	const Limits &xPercentageLimits,
	const Limits &heightLimits,
	const QRect &rect,
	const ChartLineViewContext &lineViewContext,
	DetailsPaintContext &detailsPaintContext);

} // namespace Statistic
