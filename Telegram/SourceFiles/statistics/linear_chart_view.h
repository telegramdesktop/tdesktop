/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <statistics/statistics_common.h>

namespace Data {
struct StatisticalChart;
} // namespace Data

namespace Statistic {

struct Limits;
struct DetailsPaintContext;
struct ChartLineViewContext;

class LinearChartView {
public:
	LinearChartView();

	void paint(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		ChartLineViewContext &lineViewContext,
		DetailsPaintContext &detailsPaintContext);

private:
	struct CacheToken final {
		explicit CacheToken() = default;
		explicit CacheToken(
			Limits xIndices,
			Limits xPercentageLimits,
			Limits heightLimits,
			QSize rectSize)
		: xIndices(std::move(xIndices))
		, xPercentageLimits(std::move(xPercentageLimits))
		, heightLimits(std::move(heightLimits))
		, rectSize(std::move(rectSize)) {
		}

		bool operator==(const CacheToken &other) const {
			return (rectSize == other.rectSize)
				&& (xIndices.min == other.xIndices.min)
				&& (xIndices.max == other.xIndices.max)
				&& (xPercentageLimits.min == other.xPercentageLimits.min)
				&& (xPercentageLimits.max == other.xPercentageLimits.max)
				&& (heightLimits.min == other.heightLimits.min)
				&& (heightLimits.max == other.heightLimits.max);
		}

		bool operator!=(const CacheToken &other) const {
			return !(*this == other);
		}

		Limits xIndices;
		Limits xPercentageLimits;
		Limits heightLimits;
		QSize rectSize;
	};

	struct Cache final {
		QImage image;
		CacheToken lastToken;
		bool hq = false;
	};

	base::flat_map<int, Cache> _caches;

};

} // namespace Statistic
