/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "statistics/segment_tree.h"
#include "statistics/statistics_common.h"
#include "statistics/view/abstract_chart_view.h"
#include "ui/effects/animation_value.h"

namespace Data {
struct StatisticalChart;
} // namespace Data

namespace Statistic {

struct Limits;

class StackLinearChartView final : public AbstractChartView {
public:
	StackLinearChartView();
	~StackLinearChartView() override final;

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

	int findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 x) override;

	void setEnabled(int id, bool enabled, crl::time now) override;
	[[nodiscard]] bool isEnabled(int id) const override;
	[[nodiscard]] bool isFinished() const override;
	[[nodiscard]] float64 alpha(int id) const override;

	[[nodiscard]] HeightLimits heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) override;

	void tick(crl::time now) override;
	void update(float64 dt) override;

private:
	void paint(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		bool footer);

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
		anim::value anim;
		bool disabled = false;
	};

	base::flat_map<int, Entry> _entries;
	bool _isFinished = true;

	float64 _transitionProgress = 0;
	struct Transition {
		struct TransitionLine {
			QPointF start;
			QPointF end;
			float64 angle = 0.;
			float64 sum = 0.;
		};
		std::vector<TransitionLine> lines;
	} _cachedTransition;

	Limits _lastPaintedXIndices;

	std::vector<bool> _skipPoints;

};

} // namespace Statistic
