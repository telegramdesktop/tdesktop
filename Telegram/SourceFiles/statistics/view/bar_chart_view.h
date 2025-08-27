/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "statistics/segment_tree.h"
#include "statistics/statistics_common.h"
#include "statistics/statistics_types.h"
#include "statistics/view/abstract_chart_view.h"
#include "ui/effects/animation_value.h"

namespace Data {
struct StatisticalChart;
} // namespace Data

namespace Statistic {

struct Limits;

class BarChartView final : public AbstractChartView {
public:
	BarChartView(bool isStack);
	~BarChartView() override final;

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
	void paintChartAndSelected(QPainter &p, const PaintContext &c);

	struct {
		Limits full;
		std::vector<ChartValue> ySum;
		SegmentTree ySumSegmentTree;
	} _cachedHeightLimits;

	const bool _isStack;
	DoubleLineRatios _cachedLineRatios; // Non-stack.
	Limits _lastPaintedXIndices;
	int _lastSelectedXIndex = -1;
	float64 _lastSelectedXProgress = 0;

	CachedSelectedPoints _selectedPoints; // Non-stack.

};

} // namespace Statistic
