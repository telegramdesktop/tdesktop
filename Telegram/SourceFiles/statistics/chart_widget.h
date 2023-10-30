/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_statistics_chart.h"
#include "statistics/view/chart_rulers_view.h"
#include "statistics/statistics_common.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Statistic {

class RpMouseWidget;
class PointDetailsWidget;
class ChartLinesFilterWidget;
class AbstractChartView;
class Header;
class LinesFilterController;

class ChartWidget : public Ui::RpWidget {
public:
	ChartWidget(not_null<Ui::RpWidget*> parent);

	void setChartData(Data::StatisticalChart chartData, ChartViewType type);
	void setTitle(rpl::producer<QString> &&title);
	void setZoomedChartData(
		Data::StatisticalChart chartData,
		float64 x,
		ChartViewType type);
	void addRuler(Limits newHeight, bool animated);

	[[nodiscard]] rpl::producer<float64> zoomRequests();

	struct BottomCaptionLineData final {
		int step = 0;
		int stepMax = 0;
		int stepMin = 0;
		int stepMinFast = 0;
		int stepRaw = 0;

		float64 alpha = 0.;
		float64 fixedAlpha = 0.;
	};

protected:
	int resizeGetHeight(int newWidth) override;

private:
	class Footer;

	class ChartAnimationController final {
	public:
		ChartAnimationController(Fn<void()> &&updateCallback);

		void setXPercentageLimits(
			Data::StatisticalChart &chartData,
			Limits xPercentageLimits,
			const std::unique_ptr<AbstractChartView> &chartView,
			const std::shared_ptr<LinesFilterController> &linesFilter,
			crl::time now);
		void start();
		void finish();
		void resetAlpha();
		void restartBottomLineAlpha();
		void tick(
			crl::time now,
			ChartRulersView &rulersView,
			std::vector<BottomCaptionLineData> &dateLines,
			const std::unique_ptr<AbstractChartView> &chartView,
			const std::shared_ptr<LinesFilterController> &linesFilter);

		[[nodiscard]] Limits currentXLimits() const;
		[[nodiscard]] Limits currentXIndices() const;
		[[nodiscard]] Limits finalXLimits() const;
		[[nodiscard]] Limits currentHeightLimits() const;
		[[nodiscard]] Limits currentFooterHeightLimits() const;
		[[nodiscard]] Limits finalHeightLimits() const;
		[[nodiscard]] bool animating() const;
		[[nodiscard]] bool footerAnimating() const;

		[[nodiscard]] rpl::producer<> addRulerRequests() const;

	private:
		Ui::Animations::Basic _animation;

		crl::time _lastUserInteracted = 0;
		crl::time _bottomLineAlphaAnimationStartedAt = 0;

		anim::value _animationValueXMin;
		anim::value _animationValueXMax;
		anim::value _animationValueHeightMin;
		anim::value _animationValueHeightMax;

		anim::value _animationValueFooterHeightMin;
		anim::value _animationValueFooterHeightMax;

		anim::value _animationValueHeightAlpha;

		anim::value _animValueBottomLineAlpha;

		Limits _finalHeightLimits;
		Limits _currentXIndices;

		struct {
			float speed = 0.;
			Limits current;

			float64 currentAlpha = 0.;
		} _dtHeight;
		Limits _previousFullHeightLimits;

		struct {
			crl::time lastTickedAt = 0;
			bool lastFPSSlow = false;
		} _benchmark;

		rpl::event_stream<> _addRulerRequests;

	};

	[[nodiscard]] QRect chartAreaRect() const;

	void setupChartArea();
	void setupFooter();
	void setupDetails();
	void setupFilterButtons();

	void updateBottomDates();
	void updateHeader();

	void updateChartFullWidth(int w);

	[[nodiscard]] bool hasLocalZoom() const;
	void processLocalZoom(int xIndex);

	const base::unique_qptr<RpMouseWidget> _chartArea;
	const std::unique_ptr<Header> _header;
	const std::unique_ptr<Footer> _footer;
	base::unique_qptr<ChartLinesFilterWidget> _filterButtons;
	Data::StatisticalChart _chartData;

	base::unique_qptr<ChartWidget> _zoomedChartWidget;

	std::unique_ptr<AbstractChartView> _chartView;

	struct {
		base::unique_qptr<PointDetailsWidget> widget;
		Ui::Animations::Basic animation;
		bool hideOnAnimationEnd = false;
	} _details;

	struct {
		BottomCaptionLineData current;
		std::vector<BottomCaptionLineData> dates;
		int chartFullWidth = 0;
		int captionIndicesOffset = 0;
	} _bottomLine;

	bool _areRulersAbove = false;

	std::shared_ptr<LinesFilterController> _linesFilterController;

	ChartAnimationController _animationController;
	crl::time _lastHeightLimitsChanged = 0;

	ChartRulersView _rulersView;

	bool _zoomEnabled = false;
	rpl::event_stream<float64> _zoomRequests;

	rpl::lifetime _waitingSizeLifetime;

};

} // namespace Statistic
