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

class ChartWidget : public Ui::RpWidget {
public:
	ChartWidget(not_null<Ui::RpWidget*> parent);

	void setChartData(Data::StatisticalChart chartData);
	void addHorizontalLine(Limits newHeight, bool animated);

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
		crl::time _alphaAnimationStartedAt = 0;
		bool _heightAnimationStarted = false;

		anim::value _animationValueXMin;
		anim::value _animationValueXMax;
		anim::value _animationValueHeightMin;
		anim::value _animationValueHeightMax;

		anim::value _animValueYAlpha;

		Limits _finalHeightLimits;

		float _dtHeightSpeed = 0.;
		Limits _dtCurrent;

		rpl::event_stream<> _heightAnimationStarts;

	};

	const base::unique_qptr<Ui::RpWidget> _chartArea;
	std::unique_ptr<Footer> _footer;
	Data::StatisticalChart _chartData;

	bool _useMinHeight = false;

	ChartAnimationController _animationController;
	crl::time _lastHeightLimitsChanged = 0;

	std::vector<ChartHorizontalLinesData> _horizontalLines;

};

} // namespace Statistic
