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
	LinearChartView();
	~LinearChartView() override final;

	void paint(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		bool footer) override;

	void paintSelectedXIndex(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		int selectedXIndex,
		float64 progress) override;

	void setEnabled(int id, bool enabled, crl::time now) override;
	[[nodiscard]] bool isEnabled(int id) const override;
	[[nodiscard]] bool isFinished() const override;
	[[nodiscard]] float64 alpha(int id) const override;

	[[nodiscard]] HeightLimits heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) override;

	void tick(crl::time now) override;

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

	base::flat_map<int, Cache> _mainCaches;
	base::flat_map<int, Cache> _footerCaches;

	struct SelectedPoints final {
		int lastXIndex = -1;
		Limits lastHeightLimits;
		Limits lastXLimits;
		base::flat_map<int, QPointF> points;
	};
	SelectedPoints _selectedPoints;

	struct Entry final {
		bool enabled = false;
		crl::time startedAt = 0;
		float64 alpha = 1.;
	};

	base::flat_map<int, Entry> _entries;
	bool _isFinished = true;

};

} // namespace Statistic
