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
#include "ui/effects/animations.h"
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

	void setEnabled(int id, bool enabled, crl::time now) override;
	[[nodiscard]] bool isEnabled(int id) const override;
	[[nodiscard]] bool isFinished() const override;
	[[nodiscard]] float64 alpha(int id) const override;

	[[nodiscard]] HeightLimits heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) override;

	void tick(crl::time now) override;
	void update(float64 dt) override;

	void setUpdateCallback(Fn<void()> callback);
	void handleMouseMove(
		const Data::StatisticalChart &chartData,
		const QPoint &center,
		const QPoint &p);

private:
	void paintChartOrZoomAnimation(QPainter &p, const PaintContext &c);

	void paintZoomed(QPainter &p, const PaintContext &c);
	void paintZoomedFooter(QPainter &p, const PaintContext &c);
	void paintPieText(QPainter &p, const PaintContext &c);

	[[nodiscard]] bool skipSelectedTranslation() const;

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

	class PiePartController final {
	public:
		using LineId = int;
		bool set(LineId id);
		[[nodiscard]] float64 progress(LineId id) const;
		[[nodiscard]] QPointF offset(LineId id, float64 angle) const;
		[[nodiscard]] LineId selected() const;
		[[nodiscard]] bool isFinished() const;

	private:
		void update(LineId id);

		base::flat_map<LineId, crl::time> _startedAt;
		LineId _selected = -1;

	};
	PiePartController _piePartController;
	Ui::Animations::Basic _piePartAnimation;

};

} // namespace Statistic
