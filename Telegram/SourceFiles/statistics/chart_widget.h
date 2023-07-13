/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_statistics.h"
#include "statistics/chart_horizontal_lines_data.h"
#include "statistics/statistics_common.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Statistic {

class RpMouseWidget;
class PointDetailsWidget;

class ChartWidget : public Ui::RpWidget {
public:
	ChartWidget(not_null<Ui::RpWidget*> parent);

	void setChartData(Data::StatisticalChart chartData);
	void addHorizontalLine(Limits newHeight, bool animated);

	struct BottomCaptionLineData final {
		int step = 0;
		int stepMax = 0;
		int stepMin = 0;
		int stepMinFast = 0;
		int stepRaw = 0;

		float64 alpha = 0.;
		float64 fixedAlpha = 0.;
	};

private:
	class Footer;

	class ChartAnimationController final {
	public:
		ChartAnimationController(Fn<void()> &&updateCallback);

		void setXPercentageLimits(
			Data::StatisticalChart &chartData,
			Limits xPercentageLimits,
			crl::time now);
		void start();
		void finish();
		void resetAlpha();
		void restartBottomLineAlpha();
		void tick(
			crl::time now,
			std::vector<ChartHorizontalLinesData> &horizontalLines,
			std::vector<BottomCaptionLineData> &dateLines);

		[[nodiscard]] Limits currentXLimits() const;
		[[nodiscard]] Limits currentXIndices() const;
		[[nodiscard]] Limits finalXLimits() const;
		[[nodiscard]] Limits currentHeightLimits() const;
		[[nodiscard]] Limits finalHeightLimits() const;
		[[nodiscard]] float64 detailsProgress(crl::time now) const;
		[[nodiscard]] bool animating() const;
		[[nodiscard]] bool isFPSSlow() const;

		[[nodiscard]] rpl::producer<> heightAnimationStarts() const;

	private:
		Ui::Animations::Basic _animation;

		crl::time _lastUserInteracted = 0;
		crl::time _alphaAnimationStartedAt = 0;
		crl::time _bottomLineAlphaAnimationStartedAt = 0;
		bool _heightAnimationStarted = false;

		anim::value _animationValueXMin;
		anim::value _animationValueXMax;
		anim::value _animationValueHeightMin;
		anim::value _animationValueHeightMax;

		anim::value _animValueYAlpha;

		anim::value _animValueBottomLineAlpha;

		Limits _finalHeightLimits;
		Limits _currentXIndices;

		float _dtHeightSpeed = 0.;
		Limits _dtCurrent;

		struct {
			crl::time lastTickedAt = 0;
			bool lastFPSSlow = false;
		} _benchmark;

		rpl::event_stream<> _heightAnimationStarts;

	};

	[[nodiscard]] QRect chartAreaRect() const;

	void setupChartArea();
	void setupFooter();
	void setupDetails();

	void updateBottomDates();

	const base::unique_qptr<RpMouseWidget> _chartArea;
	const std::unique_ptr<Footer> _footer;
	Data::StatisticalChart _chartData;

	struct {
		base::unique_qptr<PointDetailsWidget> widget;
		float64 currentX = 0;
	} _details;

	struct {
		BottomCaptionLineData current;
		std::vector<BottomCaptionLineData> dates;
		int chartFullWidth = 0;
		int captionIndicesOffset = 0;
	} _bottomLine;

	bool _useMinHeight = false;

	ChartAnimationController _animationController;
	crl::time _lastHeightLimitsChanged = 0;

	std::vector<ChartHorizontalLinesData> _horizontalLines;

};

} // namespace Statistic
