/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "statistics/statistics_common.h"
#include "statistics/view/abstract_chart_view.h"

namespace Data {
struct StatisticalChart;
} // namespace Data

namespace Statistic {

struct Limits;

class LinearChartView final : public AbstractChartView {
public:
	using CachedLineRatios = std::pair<float64, float64>;

	LinearChartView(bool isDouble);
	~LinearChartView() override final;

	void paint(QPainter &p, const PaintContext &c) override;

	void paintSelectedXIndex(
		QPainter &p,
		const PaintContext &c,
		int selectedXIndex,
		float64 progress) override;

	int findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 x) override;

	[[nodiscard]] HeightLimits heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) override;

private:
	CachedLineRatios _cachedLineRatios;

	[[nodiscard]] float64 lineRatio() const;

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

	base::flat_map<int, Cache> _mainCaches;
	base::flat_map<int, Cache> _footerCaches;

	struct SelectedPoints final {
		int lastXIndex = -1;
		Limits lastHeightLimits;
		Limits lastXLimits;
		base::flat_map<int, QPointF> points;
	};
	SelectedPoints _selectedPoints;

};

} // namespace Statistic
