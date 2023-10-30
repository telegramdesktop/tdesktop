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
#include "statistics/view/stack_linear_chart_common.h"
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

	[[nodiscard]] HeightLimits heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) override;

	LocalZoomResult maybeLocalZoom(const LocalZoomArgs &args) override final;

	void handleMouseMove(
		const Data::StatisticalChart &chartData,
		const QRect &rect,
		const QPoint &p) override;

private:
	enum class TransitionStep {
		PrepareToZoomIn,
		PrepareToZoomOut,
		ZoomedOut,
	};
	void paintChartOrZoomAnimation(QPainter &p, const PaintContext &c);

	void paintZoomed(QPainter &p, const PaintContext &c);
	void paintZoomedFooter(QPainter &p, const PaintContext &c);
	void paintPieText(QPainter &p, const PaintContext &c);

	[[nodiscard]] bool skipSelectedTranslation() const;

	void prepareZoom(const PaintContext &c, TransitionStep step);

	void saveZoomRange(const PaintContext &c);
	void savePieTextParts(const PaintContext &c);
	void applyParts(const std::vector<PiePartData::Part> &parts);

	struct SelectedPoints final {
		int lastXIndex = -1;
		Limits lastHeightLimits;
		Limits lastXLimits;
		float64 xPoint = 0.;
	};
	SelectedPoints _selectedPoints;

	struct Transition {
		struct TransitionLine {
			QPointF start;
			QPointF end;
			float64 angle = 0.;
			float64 sum = 0.;
		};
		std::vector<TransitionLine> lines;
		float64 progress = 0;

		bool pendingPrepareToZoomIn = false;

		Limits zoomedOutXIndices;
		Limits zoomedOutXIndicesAdditional;
		Limits zoomedOutXPercentage;
		Limits zoomedInLimit;
		Limits zoomedInLimitXIndices;
		Limits zoomedInRange;
		Limits zoomedInRangeXIndices;

		std::vector<PiePartData::Part> textParts;
	} _transition;

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

	class ChangingPiePartController final {
	public:
		void setParts(
			const std::vector<PiePartData::Part> &was,
			const std::vector<PiePartData::Part> &now);
		void update();
		[[nodiscard]] PiePartData current() const;
		[[nodiscard]] bool isFinished() const;

	private:
		crl::time _startedAt = 0;;
		std::vector<anim::value> _animValues;
		PiePartData _current;
		bool _isFinished = true;

	};

	PiePartController _piePartController;
	ChangingPiePartController _changingPieController;
	Ui::Animations::Basic _piePartAnimation;

	bool _pieHasSinglePart = false;

};

} // namespace Statistic
