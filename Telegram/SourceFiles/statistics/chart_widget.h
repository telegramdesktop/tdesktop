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

class ChartWidget : public Ui::RpWidget {
public:
	ChartWidget(not_null<Ui::RpWidget*> parent);

	void setChartData(Data::StatisticalChart chartData);
	void addHorizontalLine(Limits newHeight, bool animated);

protected:
	void paintEvent(QPaintEvent *e) override;

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
		void tick(
			crl::time now,
			std::vector<ChartHorizontalLinesData> &horizontalLines);

		[[nodiscard]] Limits currentXLimits() const;
		[[nodiscard]] Limits currentHeightLimits() const;
		[[nodiscard]] Limits finalHeightLimits() const;

		[[nodiscard]] rpl::producer<> heightAnimationStarts() const;

	private:
		Ui::Animations::Basic _animation;

		crl::time _lastUserInteracted = 0;
		crl::time _yAnimationStartedAt = 0;
		crl::time _alphaAnimationStartedAt = 0;

		anim::value _animValueXMin;
		anim::value _animValueXMax;
		anim::value _animValueYMin;
		anim::value _animValueYMax;

		anim::value _animValueYAlpha;

		Limits _finalHeightLimits;

		float _dtYSpeed = 0.;
		Limits _dtCurrent;

		rpl::event_stream<> _heightAnimationStarts;

	};

	std::unique_ptr<Footer> _footer;
	Data::StatisticalChart _chartData;

	bool _useMinHeight = false;

	Limits _currentHeight;
	Limits _animateToHeight;
	Limits _thresholdHeight = { -1, 0 };
	Limits _startFrom;
	Limits _startFromH;

	Limits _xPercentageLimits;
	// struct {
	// 	Ui::Animations::Basic animation;

	// 	crl::time lastUserInteracted = 0;
	// 	crl::time yAnimationStartedAt = 0;
	// 	crl::time alphaAnimationStartedAt = 0;

	// 	anim::value animValueXMin;
	// 	anim::value animValueXMax;
	// 	anim::value animValueYMin;
	// 	anim::value animValueYMax;

	// 	anim::value animValueYAlpha;

	// 	float dtYSpeed = 0.;
	// 	Limits dtCurrent;
	// } _xPercentage;
	ChartAnimationController _animationController;

	float64 _minMaxUpdateStep = 0.;

	crl::time _lastHeightLimitsChanged = 0;

	std::vector<ChartHorizontalLinesData> _horizontalLines;

	Ui::Animations::Simple _heightLimitsAnimation;

};

} // namespace Statistic
