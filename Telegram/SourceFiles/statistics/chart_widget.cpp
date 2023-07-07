/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_widget.h"

#include "base/qt/qt_key_modifiers.h"
#include "statistics/linear_chart_view.h"
#include "statistics/point_details_widget.h"
#include "ui/abstract_button.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_boxes.h"

namespace Statistic {

namespace {

constexpr auto kHeightLimitsUpdateTimeout = crl::time(320);
constexpr auto kExpandingDelay = crl::time(100);

inline float64 InterpolationRatio(float64 from, float64 to, float64 result) {
	return (result - from) / (to - from);
};

[[nodiscard]] int FindMaxValue(
		Data::StatisticalChart &chartData,
		int startXIndex,
		int endXIndex) {
	auto maxValue = 0;
	for (auto &l : chartData.lines) {
		const auto lineMax = l.segmentTree.rMaxQ(startXIndex, endXIndex);
		maxValue = std::max(lineMax, maxValue);
	}
	return maxValue;
}

[[nodiscard]] int FindMinValue(
		Data::StatisticalChart &chartData,
		int startXIndex,
		int endXIndex) {
	auto minValue = std::numeric_limits<int>::max();
	for (auto &l : chartData.lines) {
		const auto lineMin = l.segmentTree.rMinQ(startXIndex, endXIndex);
		minValue = std::min(lineMin, minValue);
	}
	return minValue;
}

[[nodiscard]] Limits FindHeightLimitsBetweenXLimits(
		Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits) {
	const auto startXIndex = chartData.findStartIndex(xPercentageLimits.min);
	const auto endXIndex = chartData.findEndIndex(
		startXIndex,
		xPercentageLimits.max);
	return Limits{
		float64(FindMinValue(chartData, startXIndex, endXIndex)),
		float64(FindMaxValue(chartData, startXIndex, endXIndex)),
	};
}

void PaintHorizontalLines(
		QPainter &p,
		const ChartHorizontalLinesData &horizontalLine,
		const QRect &r) {
	const auto alpha = p.opacity();
	p.setOpacity(horizontalLine.alpha);
	for (const auto &line : horizontalLine.lines) {
		const auto lineRect = QRect(
			0,
			r.y() + r.height() * line.relativeValue,
			r.x() + r.width(),
			st::lineWidth);
		p.fillRect(lineRect, st::boxTextFg);
	}
	p.setOpacity(alpha);
}

void PaintCaptionsToHorizontalLines(
		QPainter &p,
		const ChartHorizontalLinesData &horizontalLine,
		const QRect &r) {
	const auto alpha = p.opacity();
	p.setOpacity(horizontalLine.alpha);
	p.setFont(st::boxTextFont->f);
	p.setPen(st::boxTextFg);
	for (const auto &line : horizontalLine.lines) {
		p.drawText(10, r.y() + r.height() * line.relativeValue, line.caption);
	}
	p.setOpacity(alpha);
}

} // namespace

class RpMouseWidget final : public Ui::AbstractButton {
public:
	using Ui::AbstractButton::AbstractButton;

	struct State {
		QPoint point;
		QEvent::Type mouseState;
	};

	[[nodiscard]] const QPoint &start() const;
	[[nodiscard]] rpl::producer<State> mouseStateChanged() const;

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	QPoint _start = QPoint(-1, -1);

	rpl::event_stream<State> _mouseStateChanged;

};

const QPoint &RpMouseWidget::start() const {
	return _start;
}

rpl::producer<RpMouseWidget::State> RpMouseWidget::mouseStateChanged() const {
	return _mouseStateChanged.events();
}

void RpMouseWidget::mousePressEvent(QMouseEvent *e) {
	_start = e->pos();
	_mouseStateChanged.fire({ e->pos(), QEvent::MouseButtonPress });
}

void RpMouseWidget::mouseMoveEvent(QMouseEvent *e) {
	if (_start.x() >= 0 || _start.y() >= 0) {
		_mouseStateChanged.fire({ e->pos(), QEvent::MouseMove });
	}
}

void RpMouseWidget::mouseReleaseEvent(QMouseEvent *e) {
	_start = { -1, -1 };
	_mouseStateChanged.fire({ e->pos(), QEvent::MouseButtonRelease });
}

class ChartWidget::Footer final : public Ui::AbstractButton {
public:
	Footer(not_null<Ui::RpWidget*> parent);

	[[nodiscard]] rpl::producer<Limits> xPercentageLimitsChange() const;
	[[nodiscard]] rpl::producer<> userInteractionFinished() const;

	void setFullHeightLimits(Limits limits);
	[[nodiscard]] const Limits &fullHeightLimits() const;

private:
	not_null<RpMouseWidget*> _left;
	not_null<RpMouseWidget*> _right;

	rpl::event_stream<Limits> _xPercentageLimitsChange;
	rpl::event_stream<> _userInteractionFinished;

	Limits _fullHeightLimits;

	struct {
		int left = 0;
		int right = 0;
	} _limits;

};

ChartWidget::Footer::Footer(not_null<Ui::RpWidget*> parent)
: Ui::AbstractButton(parent)
, _left(Ui::CreateChild<RpMouseWidget>(this))
, _right(Ui::CreateChild<RpMouseWidget>(this)) {
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_left->resize(st::colorSliderWidth, s.height());
		_right->resize(st::colorSliderWidth, s.height());
	}, _left->lifetime());
	_left->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_left);
		p.setOpacity(0.3);
		p.fillRect(_left->rect(), st::boxTextFg);
	}, _left->lifetime());
	_right->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_right);
		p.setOpacity(0.3);
		p.fillRect(_right->rect(), st::boxTextFg);
	}, _right->lifetime());

	sizeValue(
	) | rpl::take(2) | rpl::start_with_next([=] {
		_left->moveToLeft(0, 0);
		_right->moveToRight(0, 0);
	}, _left->lifetime());

	const auto handleDrag = [&](
			not_null<RpMouseWidget*> side,
			Fn<int()> leftLimit,
			Fn<int()> rightLimit) {
		side->mouseStateChanged(
		) | rpl::start_with_next([=](const RpMouseWidget::State &state) {
			const auto posX = state.point.x();
			switch (state.mouseState) {
			case QEvent::MouseMove: {
				if (base::IsCtrlPressed()) {
					const auto diff = (posX - side->start().x());
					_left->move(_left->x() + diff, side->y());
					_right->move(_right->x() + diff, side->y());
				} else {
					const auto nextX = std::clamp(
						side->x() + (posX - side->start().x()),
						_limits.left,
						_limits.right);
					side->move(nextX, side->y());
				}
				_xPercentageLimitsChange.fire({
					.min = _left->x() / float64(width()),
					.max = rect::right(_right) / float64(width()),
				});
			} break;
			case QEvent::MouseButtonPress: {
				_limits = { .left = leftLimit(), .right = rightLimit() };
			} break;
			case QEvent::MouseButtonRelease: {
				_userInteractionFinished.fire({});
				_xPercentageLimitsChange.fire({
					.min = _left->x() / float64(width()),
					.max = rect::right(_right) / float64(width()),
				});
				_limits = {};
			} break;
			}
		}, side->lifetime());
	};
	handleDrag(
		_left,
		[=] { return 0; },
		[=] { return _right->x() - _left->width(); });
	handleDrag(
		_right,
		[=] { return rect::right(_left); },
		[=] { return width() - _right->width(); });
}

rpl::producer<Limits> ChartWidget::Footer::xPercentageLimitsChange() const {
	return _xPercentageLimitsChange.events();
}

rpl::producer<> ChartWidget::Footer::userInteractionFinished() const {
	return _userInteractionFinished.events();
}

void ChartWidget::Footer::setFullHeightLimits(Limits limits) {
	_fullHeightLimits = std::move(limits);
}

const Limits &ChartWidget::Footer::fullHeightLimits() const {
	return _fullHeightLimits;
}

ChartWidget::ChartAnimationController::ChartAnimationController(
	Fn<void()> &&updateCallback)
: _animation(std::move(updateCallback)) {
}

void ChartWidget::ChartAnimationController::setXPercentageLimits(
		Data::StatisticalChart &chartData,
		Limits xPercentageLimits,
		crl::time now) {
	if ((_animationValueXMin.to() == xPercentageLimits.min)
		&& (_animationValueXMax.to() == xPercentageLimits.max)) {
		return;
	}
	start();
	_animationValueXMin.start(xPercentageLimits.min);
	_animationValueXMax.start(xPercentageLimits.max);
	_lastUserInteracted = now;

	_finalHeightLimits = FindHeightLimitsBetweenXLimits(
		chartData,
		{ _animationValueXMin.to(), _animationValueXMax.to() });
	_animationValueHeightMin = anim::value(
		_animationValueHeightMin.current(),
		_finalHeightLimits.min);
	_animationValueHeightMax = anim::value(
		_animationValueHeightMax.current(),
		_finalHeightLimits.max);

	{
		const auto currentDelta = _animationValueHeightMax.current()
			- _animationValueHeightMin.current();
		auto k = currentDelta
			/ float64(_finalHeightLimits.max - _finalHeightLimits.min);
		if (k > 1.) {
			k = 1. / k;
		}
		constexpr auto kDtHeightSpeed1 = 0.03 / 2;
		constexpr auto kDtHeightSpeed2 = 0.03 / 2;
		constexpr auto kDtHeightSpeed3 = 0.045 / 2;
		constexpr auto kDtHeightSpeedThreshold1 = 0.7;
		constexpr auto kDtHeightSpeedThreshold2 = 0.1;
		constexpr auto kDtHeightInstantThreshold = 0.97;
		_dtHeightSpeed = (k > kDtHeightSpeedThreshold1)
			? kDtHeightSpeed1
			: (k < kDtHeightSpeedThreshold2)
			? kDtHeightSpeed2
			: kDtHeightSpeed3;
		if (k < kDtHeightInstantThreshold) {
			_dtCurrent = { 0., 0. };
		}
	}
}

void ChartWidget::ChartAnimationController::start() {
	if (!_animation.animating()) {
		_animation.start();
	}
}

void ChartWidget::ChartAnimationController::finish() {
	_animation.stop();
	_animationValueXMin.finish();
	_animationValueXMax.finish();
	_animationValueHeightMin.finish();
	_animationValueHeightMax.finish();
	_animValueYAlpha.finish();
}

void ChartWidget::ChartAnimationController::resetAlpha() {
	_alphaAnimationStartedAt = 0;
	_animValueYAlpha = anim::value(0., 1.);
}

void ChartWidget::ChartAnimationController::tick(
		crl::time now,
		std::vector<ChartHorizontalLinesData> &horizontalLines) {
	if (!_animation.animating()) {
		return;
	}
	constexpr auto kXExpandingDuration = 200.;
	constexpr auto kAlphaExpandingDuration = 200.;

	if (!_heightAnimationStarted
			&& ((now - _lastUserInteracted) >= kExpandingDelay)) {
		_heightAnimationStarts.fire({});
		_heightAnimationStarted = true;
	}
	if (!_alphaAnimationStartedAt) {
		_alphaAnimationStartedAt = now;
	}

	_dtCurrent.min = std::min(_dtCurrent.min + _dtHeightSpeed, 1.);
	_dtCurrent.max = std::min(_dtCurrent.max + _dtHeightSpeed, 1.);

	const auto dtX = std::min(
		(now - _animation.started()) / kXExpandingDuration,
		1.);
	const auto dtAlpha = std::min(
		(now - _alphaAnimationStartedAt) / kAlphaExpandingDuration,
		1.);

	const auto isFinished = [](const anim::value &anim) {
		return anim.current() == anim.to();
	};

	const auto xFinished = isFinished(_animationValueXMin)
		&& isFinished(_animationValueXMax);
	const auto yFinished = isFinished(_animationValueHeightMin)
		&& isFinished(_animationValueHeightMax);
	const auto alphaFinished = isFinished(_animValueYAlpha);

	if (xFinished && yFinished && alphaFinished) {
		const auto &lines = horizontalLines.back().lines;
		if ((lines.front().absoluteValue == _animationValueHeightMin.to())
			&& lines.back().absoluteValue == _animationValueHeightMax.to()) {
			_animation.stop();
		}
	}
	if (xFinished) {
		_animationValueXMin.finish();
		_animationValueXMax.finish();
	} else {
		_animationValueXMin.update(dtX, anim::linear);
		_animationValueXMax.update(dtX, anim::linear);
	}
	if (_heightAnimationStarted) {
		_animationValueHeightMin.update(_dtCurrent.min, anim::easeInCubic);
		_animationValueHeightMax.update(_dtCurrent.max, anim::easeInCubic);
		_animValueYAlpha.update(dtAlpha, anim::easeInCubic);

		for (auto &horizontalLine : horizontalLines) {
			horizontalLine.computeRelative(
				_animationValueHeightMax.current(),
				_animationValueHeightMin.current());
		}
	}

	if (dtAlpha >= 0. && dtAlpha <= 1.) {
		const auto value = _animValueYAlpha.current();

		for (auto &horizontalLine : horizontalLines) {
			horizontalLine.alpha = horizontalLine.fixedAlpha * (1. - value);
		}
		horizontalLines.back().alpha = value;
		if (value == 1.) {
			while (horizontalLines.size() > 1) {
				const auto startIt = begin(horizontalLines);
				if (!startIt->alpha) {
					horizontalLines.erase(startIt);
				} else {
					break;
				}
			}
		}
	}

	if (yFinished && alphaFinished) {
		_alphaAnimationStartedAt = 0;
		_heightAnimationStarted = false;
	}
}

Limits ChartWidget::ChartAnimationController::currentXLimits() const {
	return { _animationValueXMin.current(), _animationValueXMax.current() };
}

Limits ChartWidget::ChartAnimationController::finalXLimits() const {
	return { _animationValueXMin.to(), _animationValueXMax.to() };
}

Limits ChartWidget::ChartAnimationController::currentHeightLimits() const {
	return {
		_animationValueHeightMin.current(),
		_animationValueHeightMax.current(),
	};
}

Limits ChartWidget::ChartAnimationController::finalHeightLimits() const {
	return _finalHeightLimits;
}

float64 ChartWidget::ChartAnimationController::detailsProgress(
		crl::time now) const {
	return _animation.animating()
		? std::clamp(
			(now - _animation.started()) / float64(kExpandingDelay),
			0.,
			1.)
		: 0.;
}

bool ChartWidget::ChartAnimationController::animating() const {
	return _animation.animating();
}

auto ChartWidget::ChartAnimationController::heightAnimationStarts() const
-> rpl::producer<> {
	return _heightAnimationStarts.events();
}

ChartWidget::ChartWidget(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
, _chartArea(base::make_unique_q<RpMouseWidget>(this))
, _footer(std::make_unique<Footer>(this))
, _animationController([=] { _chartArea->update(); }) {
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_footer->setGeometry(
			0,
			s.height() - st::countryRowHeight,
			s.width(),
			st::countryRowHeight);
		_chartArea->setGeometry(
			0,
			0,
			s.width(),
			s.height() - st::countryRowHeight * 2);
	}, lifetime());

	setupChartArea();
	setupFooter();

	resize(width(), st::confirmMaxHeight + st::countryRowHeight * 2);
}

QRect ChartWidget::chartAreaRect() const {
	return _chartArea->rect()
		- QMargins(
			st::lineWidth,
			st::boxTextFont->height,
			st::lineWidth,
			st::lineWidth);
}

void ChartWidget::setupChartArea() {
	_chartArea->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(_chartArea.get());

		const auto now = crl::now();

		_animationController.tick(now, _horizontalLines);

		const auto chartRect = chartAreaRect();

		p.fillRect(r, st::boxBg);

		for (auto &horizontalLine : _horizontalLines) {
			PaintHorizontalLines(p, horizontalLine, chartRect);
		}

		const auto detailsAlpha = 1.
			- _animationController.detailsProgress(now);

		if (_details.widget) {
			if (!detailsAlpha && _details.currentX) {
				_details.widget->hide();
				_details.widget->setXIndex(-1);
				_details.currentX = 0;
			}
			if (_details.currentX) {
				const auto lineRect = QRectF(
					_details.currentX - (st::lineWidth / 2.),
					0,
					st::lineWidth,
					_chartArea->height());
				const auto opacity = ScopedPainterOpacity(p, detailsAlpha);
				p.fillRect(lineRect, st::windowSubTextFg);
				_details.widget->setAlpha(detailsAlpha);
			}
		}

		if (_chartData) {
			Statistic::PaintLinearChartView(
				p,
				_chartData,
				_animationController.currentXLimits(),
				_animationController.currentHeightLimits(),
				chartRect,
				{
					_details.widget ? _details.widget->xIndex() : -1,
					detailsAlpha,
				});
		}

		for (auto &horizontalLine : _horizontalLines) {
			PaintCaptionsToHorizontalLines(p, horizontalLine, chartRect);
		}
	}, _footer->lifetime());
}

void ChartWidget::setupFooter() {
	_footer->paintRequest(
	) | rpl::start_with_next([=, fullXLimits = Limits{ 0., 1. }] {
		auto p = QPainter(_footer.get());

		if (_chartData) {
			p.fillRect(_footer->rect(), st::boxBg);
			Statistic::PaintLinearChartView(
				p,
				_chartData,
				fullXLimits,
				_footer->fullHeightLimits(),
				_footer->rect(),
				{});
		}
	}, _footer->lifetime());

	rpl::merge(
		_animationController.heightAnimationStarts(),
		_footer->userInteractionFinished()
	) | rpl::start_with_next([=] {
		_animationController.resetAlpha();
		addHorizontalLine(_animationController.finalHeightLimits(), true);
		_animationController.start();
	}, _footer->lifetime());

	_footer->xPercentageLimitsChange(
	) | rpl::start_with_next([=](Limits xPercentageLimits) {
		const auto now = crl::now();
		_animationController.setXPercentageLimits(
			_chartData,
			xPercentageLimits,
			now);
		if ((now - _lastHeightLimitsChanged) < kHeightLimitsUpdateTimeout) {
			return;
		}
		_lastHeightLimitsChanged = now;
		_animationController.resetAlpha();
		addHorizontalLine(_animationController.finalHeightLimits(), true);
	}, _footer->lifetime());
}

void ChartWidget::setupDetails() {
	if (!_chartData) {
		_details = {};
		_chartArea->update();
		return;
	}
	_details.widget = base::make_unique_q<PointDetailsWidget>(
		this,
		_chartData);

	_chartArea->mouseStateChanged(
	) | rpl::start_with_next([=](const RpMouseWidget::State &state) {
		if (_animationController.animating()) {
			return;
		}
		switch (state.mouseState) {
		case QEvent::MouseButtonPress:
		case QEvent::MouseMove: {
			const auto chartRect = chartAreaRect();
			const auto pointerRatio = std::clamp(
				state.point.x() / float64(chartRect.width()),
				0.,
				1.);
			const auto currentXLimits = _animationController.finalXLimits();
			const auto rawXPercentage = anim::interpolateF(
				currentXLimits.min,
				currentXLimits.max,
				pointerRatio);
			const auto nearestXPercentageIt = ranges::lower_bound(
				_chartData.xPercentage,
				rawXPercentage);
			const auto nearestXIndex = std::distance(
				begin(_chartData.xPercentage),
				nearestXPercentageIt);
			_details.currentX = 0
				+ chartRect.width() * InterpolationRatio(
					currentXLimits.min,
					currentXLimits.max,
					*nearestXPercentageIt);
			const auto xLeft = _details.currentX
				- _details.widget->width();
			const auto x = (xLeft < 0)
				? (_details.currentX)
				: xLeft;
			_details.widget->moveToLeft(x, _chartArea->y());
			_details.widget->setXIndex(nearestXIndex);
			_details.widget->show();
			_chartArea->update();
		} break;
		case QEvent::MouseButtonRelease: {
		} break;
		}
	}, _details.widget->lifetime());
}

void ChartWidget::setChartData(Data::StatisticalChart chartData) {
	_chartData = std::move(chartData);

	setupDetails();

	_footer->setFullHeightLimits(FindHeightLimitsBetweenXLimits(
		_chartData,
		{ _chartData.xPercentage.front(), _chartData.xPercentage.back() }));

	_animationController.setXPercentageLimits(
		_chartData,
		{ _chartData.xPercentage.front(), _chartData.xPercentage.back() },
		0);
	_animationController.finish();
	addHorizontalLine(_animationController.finalHeightLimits(), false);
	_chartArea->update();
	_footer->update();
}

void ChartWidget::addHorizontalLine(Limits newHeight, bool animated) {
	const auto newLinesData = ChartHorizontalLinesData(
		newHeight.max,
		newHeight.min,
		true);
	if (!animated) {
		_horizontalLines.clear();
	}
	for (auto &horizontalLine : _horizontalLines) {
		horizontalLine.fixedAlpha = horizontalLine.alpha;
	}
	_horizontalLines.push_back(newLinesData);
	if (!animated) {
		_horizontalLines.back().alpha = 1.;
	}
}

} // namespace Statistic
