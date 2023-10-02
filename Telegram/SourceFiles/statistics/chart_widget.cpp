/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_widget.h"

#include "base/qt/qt_key_modifiers.h"
#include "lang/lang_keys.h"
#include "statistics/chart_header_widget.h"
#include "statistics/chart_lines_filter_controller.h"
#include "statistics/chart_lines_filter_widget.h"
#include "statistics/point_details_widget.h"
#include "statistics/view/abstract_chart_view.h"
#include "statistics/view/chart_view_factory.h"
#include "statistics/view/stack_chart_common.h"
#include "ui/abstract_button.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/show_animation.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/buttons.h"
#include "styles/style_layers.h"
#include "styles/style_statistics.h"

namespace Statistic {

namespace {

constexpr auto kHeightLimitsUpdateTimeout = crl::time(320);

inline float64 InterpolationRatio(float64 from, float64 to, float64 result) {
	return (result - from) / (to - from);
};

void FillLineColorsByKey(Data::StatisticalChart &chartData) {
	for (auto &line : chartData.lines) {
		if (line.colorKey == u"BLUE"_q) {
			line.color = st::statisticsChartLineBlue->c;
		} else if (line.colorKey == u"GREEN"_q) {
			line.color = st::statisticsChartLineGreen->c;
		} else if (line.colorKey == u"RED"_q) {
			line.color = st::statisticsChartLineRed->c;
		} else if (line.colorKey == u"GOLDEN"_q) {
			line.color = st::statisticsChartLineGolden->c;
		} else if (line.colorKey == u"LIGHTBLUE"_q) {
			line.color = st::statisticsChartLineLightblue->c;
		} else if (line.colorKey == u"LIGHTGREEN"_q) {
			line.color = st::statisticsChartLineLightgreen->c;
		} else if (line.colorKey == u"ORANGE"_q) {
			line.color = st::statisticsChartLineOrange->c;
		} else if (line.colorKey == u"INDIGO"_q) {
			line.color = st::statisticsChartLineIndigo->c;
		} else if (line.colorKey == u"PURPLE"_q) {
			line.color = st::statisticsChartLinePurple->c;
		} else if (line.colorKey == u"CYAN"_q) {
			line.color = st::statisticsChartLineCyan->c;
		}
	}
}

[[nodiscard]] QString HeaderRightInfo(
		const Data::StatisticalChart &chartData,
		const Limits &limits) {
	return (limits.min == limits.max)
		? chartData.getDayString(limits.min)
		: chartData.getDayString(limits.min)
			+ ' '
			+ QChar(8212)
			+ ' '
			+ chartData.getDayString(limits.max);
}

void PaintBottomLine(
		QPainter &p,
		const std::vector<ChartWidget::BottomCaptionLineData> &dates,
		Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		int fullWidth,
		int chartWidth,
		int y,
		int captionIndicesOffset) {
	p.setFont(st::statisticsDetailsBottomCaptionStyle.font);
	const auto opacity = p.opacity();

	const auto startXIndex = chartData.findStartIndex(
		xPercentageLimits.min);
	const auto endXIndex = chartData.findEndIndex(
		startXIndex,
		xPercentageLimits.max);

	const auto edgeAlphaSize = st::statisticsChartBottomCaptionMaxWidth / 4.;

	for (auto k = 0; k < dates.size(); k++) {
		const auto &date = dates[k];
		const auto isLast = (k == dates.size() - 1);
		const auto resultAlpha = date.alpha;
		const auto step = std::max(date.step, 1);

		auto start = startXIndex - captionIndicesOffset;
		while (start % step != 0) {
			start--;
		}

		auto end = endXIndex - captionIndicesOffset;
		while ((end % step != 0) || end < (chartData.x.size() - 1)) {
			end++;
		}

		start += captionIndicesOffset;
		end += captionIndicesOffset;

		const auto offset = fullWidth * xPercentageLimits.min;

		// 30 ms / 200 ms = 0.15.
		constexpr auto kFastAlphaSpeed = 0.85;
		const auto hasFastAlpha = (date.stepRaw < dates.back().stepMinFast);
		const auto fastAlpha = isLast
			? 1.
			: std::max(resultAlpha - kFastAlphaSpeed, 0.);

		for (auto i = start; i < end; i += step) {
			if ((i < 0) || (i >= (chartData.x.size() - 1))) {
				continue;
			}
			const auto xPercentage = (chartData.x[i] - chartData.x.front())
				/ float64(chartData.x.back() - chartData.x.front());
			const auto xPoint = xPercentage * fullWidth - offset;
			const auto r = QRectF(
				xPoint - st::statisticsChartBottomCaptionMaxWidth / 2.,
				y,
				st::statisticsChartBottomCaptionMaxWidth,
				st::statisticsChartBottomCaptionHeight);
			const auto edgeAlpha = (r.x() < 0)
				? std::max(
					0.,
					1. + (r.x() / edgeAlphaSize))
				: (rect::right(r) > chartWidth)
				? std::max(
					0.,
					1. + ((chartWidth - rect::right(r)) / edgeAlphaSize))
				: 1.;
			p.setOpacity(opacity
				* edgeAlpha
				* (hasFastAlpha ? fastAlpha : resultAlpha));
			p.drawText(r, chartData.getDayString(i), style::al_center);
		}
	}
}

} // namespace

class RpMouseWidget : public Ui::AbstractButton {
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

class ChartWidget::Footer final : public RpMouseWidget {
public:
	using PaintCallback = Fn<void(QPainter &, const QRect &)>;

	explicit Footer(not_null<Ui::RpWidget*> parent);

	void setXPercentageLimits(const Limits &xLimits);

	[[nodiscard]] Limits xPercentageLimits() const;
	[[nodiscard]] rpl::producer<Limits> xPercentageLimitsChange() const;

	void setPaintChartCallback(PaintCallback paintChartCallback);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	rpl::event_stream<Limits> _xPercentageLimitsChange;

	void prepareCache(int height);

	void moveSide(bool left, float64 x);
	void moveCenter(
		bool isDirectionToLeft,
		float64 x,
		float64 diffBetweenStartAndLeft);

	void fire() const;

	enum class DragArea {
		None,
		Middle,
		Left,
		Right,
	};
	DragArea _dragArea = DragArea::None;
	float64 _diffBetweenStartAndSide = 0;
	Ui::Animations::Simple _moveCenterAnimation;
	bool _draggedAfterPress = false;

	float64 _width = 0.;
	float64 _widthBetweenSides = 0.;

	PaintCallback _paintChartCallback;

	QImage _frame;
	QImage _mask;

	QImage _leftCache;
	QImage _rightCache;

	Limits _leftSide;
	Limits _rightSide;

};

ChartWidget::Footer::Footer(not_null<Ui::RpWidget*> parent)
: RpMouseWidget(parent) {
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		if (s.isNull()) {
			return;
		}
		const auto was = xPercentageLimits();
		const auto w = float64(st::statisticsChartFooterSideWidth);
		_width = s.width() - w;
		_widthBetweenSides = s.width() - w * 2.;
		_mask = Ui::RippleAnimation::RoundRectMask(
			s - QSize(0, st::statisticsChartLineWidth * 2),
			st::boxRadius);
		_frame = _mask;
		if (_widthBetweenSides && was.max) {
			setXPercentageLimits(was);
		}
		prepareCache(s.height());
	}, lifetime());

	sizeValue(
	) | rpl::take(2) | rpl::start_with_next([=](const QSize &s) {
		moveSide(false, s.width());
		moveSide(true, 0);
		update();
	}, lifetime());

	mouseStateChanged(
	) | rpl::start_with_next([=](const RpMouseWidget::State &state) {
		if (_moveCenterAnimation.animating()) {
			return;
		}

		const auto posX = state.point.x();
		const auto isLeftSide = (posX >= _leftSide.min)
			&& (posX <= _leftSide.max);
		const auto isRightSide = !isLeftSide
			&& (posX >= _rightSide.min)
			&& (posX <= _rightSide.max);
		switch (state.mouseState) {
		case QEvent::MouseMove: {
			_draggedAfterPress = true;
			if (_dragArea == DragArea::None) {
				return;
			}
			const auto resultX = posX - _diffBetweenStartAndSide;
			if (_dragArea == DragArea::Right) {
				moveSide(false, resultX);
			} else if (_dragArea == DragArea::Left) {
				moveSide(true, resultX);
			} else if (_dragArea == DragArea::Middle) {
				const auto toLeft = (posX
					- _diffBetweenStartAndSide
					- _leftSide.min) <= 0;
				moveCenter(toLeft, posX, _diffBetweenStartAndSide);
			}
			fire();
		} break;
		case QEvent::MouseButtonPress: {
			_draggedAfterPress = false;
			_dragArea = isLeftSide
				? DragArea::Left
				: isRightSide
				? DragArea::Right
				: ((posX < _leftSide.min) || (posX > _rightSide.max))
				? DragArea::None
				: DragArea::Middle;
			_diffBetweenStartAndSide = isRightSide
				? (start().x() - _rightSide.min)
				: (start().x() - _leftSide.min);
		} break;
		case QEvent::MouseButtonRelease: {
			const auto finish = [=] {
				_dragArea = DragArea::None;
				fire();
			};
			if ((_dragArea == DragArea::None) && !_draggedAfterPress) {
				const auto startX = _leftSide.min
					+ (_rightSide.max - _leftSide.min) / 2;
				const auto finishX = posX;
				const auto toLeft = (finishX <= startX);
				const auto diffBetweenStartAndLeft = startX - _leftSide.min;
				_moveCenterAnimation.stop();
				_moveCenterAnimation.start([=](float64 value) {
					moveCenter(toLeft, value, diffBetweenStartAndLeft);
					fire();
					update();
					if (value == finishX) {
						finish();
					}
				},
				startX,
				finishX,
				st::slideWrapDuration,
				anim::sineInOut);
			} else {
				finish();
			}
		} break;
		}
		update();
	}, lifetime());
}

Limits ChartWidget::Footer::xPercentageLimits() const {
	return {
		.min = _widthBetweenSides ? _leftSide.min / _widthBetweenSides : 0.,
		.max = _widthBetweenSides
			? (_rightSide.min - st::statisticsChartFooterSideWidth)
				/ _widthBetweenSides
			: 0.,
	};
}

void ChartWidget::Footer::fire() const {
	_xPercentageLimitsChange.fire(xPercentageLimits());
}

void ChartWidget::Footer::moveCenter(
		bool isDirectionToLeft,
		float64 x,
		float64 diffBetweenStartAndLeft) {
	const auto resultX = x - diffBetweenStartAndLeft;
	const auto diffBetweenSides = std::max(
		_rightSide.min - _leftSide.min,
		float64(st::statisticsChartFooterBetweenSide));
	if (isDirectionToLeft) {
		moveSide(true, resultX);
		moveSide(false, _leftSide.min + diffBetweenSides);
	} else {
		moveSide(false, resultX + diffBetweenSides);
		moveSide(true, _rightSide.min - diffBetweenSides);
	}
}

void ChartWidget::Footer::moveSide(bool left, float64 x) {
	const auto w = float64(st::statisticsChartFooterSideWidth);
	const auto mid = float64(st::statisticsChartFooterBetweenSide);
	if (_width < (2 * w + mid)) {
		return;
	} else if (left) {
		const auto min = std::clamp(x, 0., _rightSide.min - w - mid);
		_leftSide = Limits{ .min = min, .max = min + w };
	} else if (!left) {
		const auto min = std::clamp(x, _leftSide.max + mid, _width);
		_rightSide = Limits{ .min = min, .max = min + w };
	}
}

void ChartWidget::Footer::prepareCache(int height) {
	const auto s = QSize(st::statisticsChartFooterSideWidth, height);

	_leftCache = QImage(
		s * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	_leftCache.setDevicePixelRatio(style::DevicePixelRatio());

	_leftCache.fill(Qt::transparent);
	{
		auto p = QPainter(&_leftCache);

		auto path = QPainterPath();
		const auto halfArrow = st::statisticsChartFooterArrowSize
			/ style::DevicePixelRatio()
			/ 2.;
		const auto c = Rect(s).center();
		path.moveTo(c.x() + halfArrow.width(), c.y() - halfArrow.height());
		path.lineTo(c.x() - halfArrow.width(), c.y());
		path.lineTo(c.x() + halfArrow.width(), c.y() + halfArrow.height());
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(QPen(st::windowSubTextFg, st::statisticsChartLineWidth));
			p.drawPath(path);
		}

	}
	_rightCache = _leftCache.mirrored(true, false);
}

void ChartWidget::Footer::setPaintChartCallback(
		PaintCallback paintChartCallback) {
	_paintChartCallback = std::move(paintChartCallback);
}

void ChartWidget::Footer::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto hq = PainterHighQualityEnabler(p);

	const auto lineWidth = st::statisticsChartLineWidth;
	const auto innerMargins = QMargins{ 0, lineWidth, 0, lineWidth };
	const auto r = rect();
	const auto innerRect = r - innerMargins;
	const auto &inactiveColor = st::statisticsChartInactive;

	_frame.fill(Qt::transparent);
	if (_paintChartCallback) {
		auto q = QPainter(&_frame);

		{
			const auto opacity = q.opacity();
			_paintChartCallback(q, Rect(innerRect.size()));
			q.setOpacity(opacity);
		}

		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		q.drawImage(0, 0, _mask);
	}

	p.drawImage(0, lineWidth, _frame);

	auto inactivePath = QPainterPath();
	inactivePath.addRoundedRect(
		innerRect,
		st::boxRadius,
		st::boxRadius);

	auto sidesPath = QPainterPath();
	sidesPath.addRoundedRect(
		_leftSide.min,
		0,
		_rightSide.max - _leftSide.min,
		r.height(),
		st::boxRadius,
		st::boxRadius);
	inactivePath = inactivePath.subtracted(sidesPath);
	sidesPath.addRect(
		_leftSide.max,
		lineWidth,
		_rightSide.min - _leftSide.max,
		r.height() - lineWidth * 2);

	p.setBrush(st::statisticsChartActive);
	p.setPen(Qt::NoPen);
	p.drawPath(sidesPath);
	p.setBrush(inactiveColor);
	p.drawPath(inactivePath);

	p.drawImage(_leftSide.min, 0, _leftCache);
	p.drawImage(_rightSide.min, 0, _rightCache);
}

void ChartWidget::Footer::setXPercentageLimits(const Limits &xLimits) {
	const auto left = xLimits.min * _widthBetweenSides;
	const auto right = xLimits.max * _widthBetweenSides
		+ st::statisticsChartFooterSideWidth;
	moveSide(true, left);
	moveSide(false, right);
	fire();
	update();
}

rpl::producer<Limits> ChartWidget::Footer::xPercentageLimitsChange() const {
	return _xPercentageLimitsChange.events();
}

ChartWidget::ChartAnimationController::ChartAnimationController(
	Fn<void()> &&updateCallback)
: _animation(std::move(updateCallback)) {
}

void ChartWidget::ChartAnimationController::setXPercentageLimits(
		Data::StatisticalChart &chartData,
		Limits xPercentageLimits,
		const std::unique_ptr<AbstractChartView> &chartView,
		const std::shared_ptr<LinesFilterController> &linesFilter,
		crl::time now) {
	if ((_animationValueXMin.to() == xPercentageLimits.min)
		&& (_animationValueXMax.to() == xPercentageLimits.max)
		&& linesFilter->isFinished()) {
		return;
	}
	start();
	_animationValueXMin.start(xPercentageLimits.min);
	_animationValueXMax.start(xPercentageLimits.max);
	_lastUserInteracted = now;

	const auto startXIndex = chartData.findStartIndex(
		_animationValueXMin.to());
	const auto endXIndex = chartData.findEndIndex(
		startXIndex,
		_animationValueXMax.to());
	_currentXIndices = { float64(startXIndex), float64(endXIndex) };

	{
		const auto heightLimits = chartView->heightLimits(
			chartData,
			_currentXIndices);
		if (heightLimits.ranged.min == heightLimits.ranged.max) {
			return;
		}
		_previousFullHeightLimits = _finalHeightLimits;
		_finalHeightLimits = heightLimits.ranged;
		if (!_previousFullHeightLimits.max) {
			_previousFullHeightLimits = _finalHeightLimits;
		}
		if (!linesFilter->isFinished()) {
			_animationValueFooterHeightMin = anim::value(
				_animationValueFooterHeightMin.current(),
				heightLimits.full.min);
			_animationValueFooterHeightMax = anim::value(
				_animationValueFooterHeightMax.current(),
				heightLimits.full.max);
		} else if (!_animationValueFooterHeightMax.to()) {
			// Will be finished in setChartData.
			_animationValueFooterHeightMin = anim::value(
				0,
				heightLimits.full.min);
			_animationValueFooterHeightMax = anim::value(
				0,
				heightLimits.full.max);
		}
	}

	_animationValueHeightMin = anim::value(
		_animationValueHeightMin.current(),
		_finalHeightLimits.min);
	_animationValueHeightMax = anim::value(
		_animationValueHeightMax.current(),
		_finalHeightLimits.max);

	{
		const auto previousDelta = _previousFullHeightLimits.max
			- _previousFullHeightLimits.min;
		auto k = previousDelta
			/ float64(_finalHeightLimits.max - _finalHeightLimits.min);
		if (k > 1.) {
			k = 1. / k;
		}
		constexpr auto kDtHeightSpeed1 = 0.03 * 2;
		constexpr auto kDtHeightSpeed2 = 0.03 * 2;
		constexpr auto kDtHeightSpeed3 = 0.045 * 2;
		constexpr auto kDtHeightSpeedFilter = kDtHeightSpeed1 / 1.2;
		constexpr auto kDtHeightSpeedThreshold1 = 0.7;
		constexpr auto kDtHeightSpeedThreshold2 = 0.1;
		constexpr auto kDtHeightInstantThreshold = 0.97;
		if (k < 1.) {
			auto &alpha = _animationValueHeightAlpha;
			alpha = anim::value(
				(alpha.current() == alpha.to()) ? 0. : alpha.current(),
				1.);
			_dtHeight.currentAlpha = 0.;
			_addRulerRequests.fire({});
		}
		_dtHeight.speed = (!linesFilter->isFinished())
			? kDtHeightSpeedFilter
			: (k > kDtHeightSpeedThreshold1)
			? kDtHeightSpeed1
			: (k < kDtHeightSpeedThreshold2)
			? kDtHeightSpeed2
			: kDtHeightSpeed3;
		if (k < kDtHeightInstantThreshold) {
			_dtHeight.current = { 0., 0. };
		}
	}
}

auto ChartWidget::ChartAnimationController::addRulerRequests() const
-> rpl::producer<> {
	return _addRulerRequests.events();
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
	_animationValueFooterHeightMin.finish();
	_animationValueFooterHeightMax.finish();
	_animationValueHeightAlpha.finish();
	_benchmark = {};
}

void ChartWidget::ChartAnimationController::restartBottomLineAlpha() {
	_bottomLineAlphaAnimationStartedAt = crl::now();
	_animValueBottomLineAlpha = anim::value(0., 1.);
	start();
}

void ChartWidget::ChartAnimationController::tick(
		crl::time now,
		ChartRulersView &rulersView,
		std::vector<BottomCaptionLineData> &dateLines,
		const std::unique_ptr<AbstractChartView> &chartView,
		const std::shared_ptr<LinesFilterController> &linesFilter) {
	if (!_animation.animating()) {
		return;
	}
	constexpr auto kXExpandingDuration = 200.;
	constexpr auto kAlphaExpandingDuration = 200.;

	{
		constexpr auto kIdealFPS = float64(60);
		const auto currentFPS = _benchmark.lastTickedAt
			? (1000. / (now - _benchmark.lastTickedAt))
			: kIdealFPS;
		if (!_benchmark.lastFPSSlow) {
			constexpr auto kAcceptableFPS = int(30);
			_benchmark.lastFPSSlow = (currentFPS < kAcceptableFPS);
		}
		_benchmark.lastTickedAt = now;


		const auto k = (kIdealFPS / currentFPS)
			// Speed up to reduce ugly frames count.
			* (_benchmark.lastFPSSlow ? 2. : 1.);
		const auto speed = _dtHeight.speed * k;
		linesFilter->tick(speed);
		_dtHeight.current.min = std::min(_dtHeight.current.min + speed, 1.);
		_dtHeight.current.max = std::min(_dtHeight.current.max + speed, 1.);
		_dtHeight.currentAlpha = std::min(_dtHeight.currentAlpha + speed, 1.);
	}

	const auto dtX = std::min(
		(now - _animation.started()) / kXExpandingDuration,
		1.);
	const auto dtBottomLineAlpha = std::min(
		(now - _bottomLineAlphaAnimationStartedAt) / kAlphaExpandingDuration,
		1.);

	const auto isFinished = [](const anim::value &anim) {
		return anim.current() == anim.to();
	};

	const auto xFinished = isFinished(_animationValueXMin)
		&& isFinished(_animationValueXMax);
	const auto yFinished = isFinished(_animationValueHeightMin)
		&& isFinished(_animationValueHeightMax);
	const auto alphaFinished = isFinished(_animationValueHeightAlpha)
		&& isFinished(_animationValueHeightMax);
	const auto bottomLineAlphaFinished = isFinished(
		_animValueBottomLineAlpha);

	const auto footerMinFinished = isFinished(_animationValueFooterHeightMin);
	const auto footerMaxFinished = isFinished(_animationValueFooterHeightMax);

	if (xFinished
			&& yFinished
			&& alphaFinished
			&& bottomLineAlphaFinished
			&& footerMinFinished
			&& footerMaxFinished
			&& linesFilter->isFinished()) {
		if ((_finalHeightLimits.min == _animationValueHeightMin.to())
			&& _finalHeightLimits.max == _animationValueHeightMax.to()) {
			_animation.stop();
			_benchmark = {};
		}
	}
	if (xFinished) {
		_animationValueXMin.finish();
		_animationValueXMax.finish();
	} else {
		_animationValueXMin.update(dtX, anim::linear);
		_animationValueXMax.update(dtX, anim::linear);
	}
	if (bottomLineAlphaFinished) {
		_animValueBottomLineAlpha.finish();
		_bottomLineAlphaAnimationStartedAt = 0;
	} else {
		_animValueBottomLineAlpha.update(
			dtBottomLineAlpha,
			anim::easeInCubic);
	}
	if (!yFinished) {
		_animationValueHeightMin.update(
			_dtHeight.current.min,
			anim::easeInCubic);
		_animationValueHeightMax.update(
			_dtHeight.current.max,
			anim::easeInCubic);

		rulersView.computeRelative(
			_animationValueHeightMax.current(),
			_animationValueHeightMin.current());
	}
	if (!footerMinFinished) {
		_animationValueFooterHeightMin.update(
			_dtHeight.current.min,
			anim::easeInCubic);
	}
	if (!footerMaxFinished) {
		_animationValueFooterHeightMax.update(
			_dtHeight.current.max,
			anim::easeInCubic);
	}

	if (!alphaFinished) {
		_animationValueHeightAlpha.update(
			_dtHeight.currentAlpha,
			anim::easeInCubic);
		rulersView.setAlpha(_animationValueHeightAlpha.current());
	}

	if (!bottomLineAlphaFinished) {
		const auto value = _animValueBottomLineAlpha.current();
		for (auto &date : dateLines) {
			date.alpha = (1. - value) * date.fixedAlpha;
		}
		dateLines.back().alpha = value;
	} else {
		if (dateLines.size() > 1) {
			const auto data = dateLines.back();
			dateLines.clear();
			dateLines.push_back(data);
		}
	}
}

Limits ChartWidget::ChartAnimationController::currentXLimits() const {
	return { _animationValueXMin.current(), _animationValueXMax.current() };
}

Limits ChartWidget::ChartAnimationController::currentXIndices() const {
	return _currentXIndices;
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

auto ChartWidget::ChartAnimationController::currentFooterHeightLimits() const
-> Limits {
	return {
		_animationValueFooterHeightMin.current(),
		_animationValueFooterHeightMax.current(),
	};
}

Limits ChartWidget::ChartAnimationController::finalHeightLimits() const {
	return _finalHeightLimits;
}

bool ChartWidget::ChartAnimationController::animating() const {
	return _animation.animating();
}

bool ChartWidget::ChartAnimationController::footerAnimating() const {
	return (_animationValueFooterHeightMin.current()
			!= _animationValueFooterHeightMin.to())
		|| (_animationValueFooterHeightMax.current()
			!= _animationValueFooterHeightMax.to());
}

ChartWidget::ChartWidget(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
, _chartArea(base::make_unique_q<RpMouseWidget>(this))
, _header(std::make_unique<Header>(this))
, _footer(std::make_unique<Footer>(this))
, _linesFilterController(std::make_shared<LinesFilterController>())
, _animationController([=] {
	_chartArea->update();
	if (_animationController.footerAnimating()
		|| !_linesFilterController->isFinished()) {
		_footer->update();
	}
}) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		if (_chartData) {
			FillLineColorsByKey(_chartData);
		}
	}, lifetime());
	setupChartArea();
	setupFooter();
}

int ChartWidget::resizeGetHeight(int newWidth) {
	if (_filterButtons) {
		auto texts = std::vector<QString>();
		auto colors = std::vector<QColor>();
		auto ids = std::vector<int>();
		texts.reserve(_chartData.lines.size());
		colors.reserve(_chartData.lines.size());
		ids.reserve(_chartData.lines.size());
		for (const auto &line : _chartData.lines) {
			texts.push_back(line.name);
			colors.push_back(line.color);
			ids.push_back(line.id);
		}

		_filterButtons->fillButtons(texts, colors, ids, newWidth);
	}
	const auto filtersTopSkip = st::statisticsFilterButtonsPadding.top();
	const auto filtersHeight = _filterButtons
		? (_filterButtons->height()
			+ st::statisticsFilterButtonsPadding.bottom())
		: 0;
	const auto &headerPadding = st::statisticsChartHeaderPadding;
	{
		_header->moveToLeft(headerPadding.left(), headerPadding.top());
		_header->resizeToWidth(newWidth - rect::m::sum::h(headerPadding));
	}
	const auto headerHeight = rect::m::sum::v(headerPadding)
		+ _header->height();
	const auto resultHeight = headerHeight
		+ st::statisticsChartHeight
		+ st::statisticsChartFooterHeight
		+ st::statisticsChartFooterSkip
		+ filtersTopSkip
		+ filtersHeight;
	{
		_footer->setGeometry(
			0,
			resultHeight
				- st::statisticsChartFooterHeight
				- filtersTopSkip
				- filtersHeight,
			newWidth,
			st::statisticsChartFooterHeight);
		if (_filterButtons) {
			_filterButtons->moveToLeft(0, resultHeight - filtersHeight);
		}
		_chartArea->setGeometry(
			0,
			headerHeight,
			newWidth,
			resultHeight
				- headerHeight
				- st::statisticsChartFooterHeight
				- filtersTopSkip
				- filtersHeight
				- st::statisticsChartFooterSkip);

		{
			updateChartFullWidth(newWidth);
			updateBottomDates();
		}
	}
	return resultHeight;
}

void ChartWidget::updateChartFullWidth(int w) {
	const auto finalXLimits = _animationController.finalXLimits();
	_bottomLine.chartFullWidth = w / (finalXLimits.max - finalXLimits.min);
}

QRect ChartWidget::chartAreaRect() const {
	return _chartArea->rect()
		- QMargins(
			st::lineWidth,
			st::boxTextFont->height,
			st::lineWidth,
			st::lineWidth
				+ st::statisticsChartBottomCaptionHeight
				+ st::statisticsChartBottomCaptionSkip);
}

void ChartWidget::setupChartArea() {
	_chartArea->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(_chartArea.get());

		const auto now = crl::now();

		_animationController.tick(
			now,
			_rulersView,
			_bottomLine.dates,
			_chartView,
			_linesFilterController);

		const auto chartRect = chartAreaRect();

		p.fillRect(r, st::boxBg);

		if (!_chartData) {
			return;
		}

		_rulersView.paintRulers(p, chartRect);

		const auto context = PaintContext{
			_chartData,
			_animationController.currentXIndices(),
			_animationController.currentXLimits(),
			_animationController.currentHeightLimits(),
			chartRect,
			false,
		};

		{
			PainterHighQualityEnabler hp(p);
			_chartView->paint(p, context);
		}

		_rulersView.paintCaptionsToRulers(p, chartRect);
		{
			[[maybe_unused]] const auto o = ScopedPainterOpacity(
				p,
				p.opacity() * kRulerLineAlpha);
			const auto bottom = r
				- QMargins{ 0, rect::bottom(chartRect), 0, 0 };
			p.fillRect(bottom, st::boxBg);
			p.fillRect(
				QRect(bottom.x(), bottom.y(), bottom.width(), st::lineWidth),
				st::boxTextFg);
		}
		if (_details.widget) {
			const auto detailsAlpha = _details.widget->alpha();

			for (const auto &line : _chartData.lines) {
				_details.widget->setLineAlpha(
					line.id,
					_linesFilterController->alpha(line.id));
			}
			_chartView->paintSelectedXIndex(
				p,
				context,
				_details.widget->xIndex(),
				detailsAlpha);
		}

		p.setPen(st::windowSubTextFg);
		PaintBottomLine(
			p,
			_bottomLine.dates,
			_chartData,
			_animationController.finalXLimits(),
			_bottomLine.chartFullWidth,
			_chartArea->width(),
			rect::bottom(chartRect) + st::statisticsChartBottomCaptionSkip,
			_bottomLine.captionIndicesOffset);
	}, _footer->lifetime());
}

void ChartWidget::updateBottomDates() {
	if (!_chartData || !_bottomLine.chartFullWidth) {
		return;
	}
	const auto d = _bottomLine.chartFullWidth * _chartData.oneDayPercentage;
	const auto k = _chartArea->width() / d;
	const auto stepRaw = int(k / 6);

	_bottomLine.captionIndicesOffset = 0
		+ st::statisticsChartBottomCaptionMaxWidth
			/ int(_chartArea->width() / float64(_chartData.x.size()));

	const auto isCurrentNull = (_bottomLine.current.stepMinFast == 0);
	if (!isCurrentNull
		&& (stepRaw < _bottomLine.current.stepMax)
		&& (stepRaw > _bottomLine.current.stepMin)) {
		return;
	}
	const auto highestOneBit = [](unsigned int v) {
		if (!v) {
			return 0;
		}
		auto r = unsigned(1);

		while (v >>= 1) {
			r *= 2;
		}
		return int(r);
	};
	const auto step = highestOneBit(stepRaw) << 1;
	if (!isCurrentNull && (_bottomLine.current.step == step)) {
		return;
	}

	constexpr auto kStepRatio = 0.1;
	constexpr auto kFastStepOffset = 4;
	const auto stepMax = int(step + step * kStepRatio);
	const auto stepMin = int(step - step * kStepRatio);
	const auto stepMinFast = stepMin - kFastStepOffset;

	auto data = BottomCaptionLineData{
		.step = step,
		.stepMax = stepMax,
		.stepMin = stepMin,
		.stepMinFast = stepMinFast,
		.stepRaw = stepRaw,
		.alpha = 1.,
	};

	if (isCurrentNull) {
		_bottomLine.current = data;
		_bottomLine.dates.push_back(data);
		return;
	}

	_bottomLine.current = data;

	for (auto &date : _bottomLine.dates) {
		date.fixedAlpha = date.alpha;
	}

	_bottomLine.dates.push_back(data);
	if (_bottomLine.dates.size() > 2) {
		_bottomLine.dates.erase(begin(_bottomLine.dates));
	}

	_animationController.restartBottomLineAlpha();
}

void ChartWidget::updateHeader() {
	if (!_chartData) {
		return;
	}
	const auto indices = _animationController.currentXIndices();
	_header->setRightInfo(HeaderRightInfo(_chartData, indices));
	_header->update();
}

void ChartWidget::setupFooter() {
	_footer->setPaintChartCallback([=, fullXLimits = Limits{ 0., 1. }](
			QPainter &p,
			const QRect &r) {
		if (_chartData) {
			p.fillRect(r, st::boxBg);

			auto hp = PainterHighQualityEnabler(p);
			_chartView->paint(
				p,
				PaintContext{
					_chartData,
					{ 0., float64(_chartData.x.size() - 1) },
					fullXLimits,
					_animationController.currentFooterHeightLimits(),
					r,
					true,
				});
		}
	});

	_animationController.addRulerRequests(
	) | rpl::start_with_next([=] {
		_rulersView.add(
			_animationController.finalHeightLimits(),
			true);
		_animationController.start();
	}, _footer->lifetime());

	_footer->xPercentageLimitsChange(
	) | rpl::start_with_next([=](Limits xPercentageLimits) {
		if (!_chartView) {
			return;
		}
		const auto now = crl::now();
		if (_details.widget
			&& (_details.widget->xIndex() >= 0)
			&& !_details.animation.animating()) {
			_details.hideOnAnimationEnd = true;
			_details.animation.start();
		}
		_animationController.setXPercentageLimits(
			_chartData,
			xPercentageLimits,
			_chartView,
			_linesFilterController,
			now);
		updateChartFullWidth(_chartArea->width());
		updateBottomDates();
		updateHeader();
		if ((now - _lastHeightLimitsChanged) < kHeightLimitsUpdateTimeout) {
			return;
		}
		_lastHeightLimitsChanged = now;
		_rulersView.add(
			_animationController.finalHeightLimits(),
			true);
	}, _footer->lifetime());
}

void ChartWidget::setupDetails() {
	if (!_chartData) {
		_details.widget = nullptr;
		_chartArea->update();
		return;
	}
	const auto maxAbsoluteValue = [&] {
		auto maxValue = 0;
		for (const auto &l : _chartData.lines) {
			maxValue = std::max(l.maxValue, maxValue);
		}
		return maxValue;
	}();
	if (hasLocalZoom()) {
		_zoomEnabled = true;
	}
	_details.widget = base::make_unique_q<PointDetailsWidget>(
		this,
		_chartData,
		maxAbsoluteValue,
		_zoomEnabled);
	_details.widget->setClickedCallback([=] {
		const auto index = _details.widget->xIndex();
		if (index < 0) {
			return;
		}
		if (hasLocalZoom()) {
			processLocalZoom(index);
		} else {
			_zoomRequests.fire_copy(_chartData.x[index]);
		}
	});

	_details.widget->shownValue(
	) | rpl::start_with_next([=](bool shown) {
		if (shown && _details.widget->xIndex() < 0) {
			_details.widget->hide();
		}
	}, _details.widget->lifetime());

	_chartArea->mouseStateChanged(
	) | rpl::start_with_next([=](const RpMouseWidget::State &state) {
		if (_animationController.animating()) {
			return;
		}
		switch (state.mouseState) {
		case QEvent::MouseButtonPress:
		case QEvent::MouseMove: {
			const auto wasXIndex = _details.widget->xIndex();
			const auto chartRect = chartAreaRect();
			const auto currentXLimits = _animationController.finalXLimits();
			const auto nearestXIndex = _chartView->findXIndexByPosition(
				_chartData,
				currentXLimits,
				chartRect,
				state.point.x());
			if (nearestXIndex < 0) {
				_details.widget->setXIndex(nearestXIndex);
				_details.widget->hide();
				_chartArea->update();
				return;
			}
			const auto currentX = 0
				+ chartRect.width() * InterpolationRatio(
					currentXLimits.min,
					currentXLimits.max,
					_chartData.xPercentage[nearestXIndex]);
			const auto xLeft = currentX
				- _details.widget->width();
			const auto x = (xLeft >= 0)
				? xLeft
				: ((currentX
					+ _details.widget->width()
					- _chartArea->width()) > 0)
				? 0
				: currentX;
			_details.widget->moveToLeft(x, _chartArea->y());
			_details.widget->setXIndex(nearestXIndex);
			if (_details.widget->isHidden()) {
				_details.hideOnAnimationEnd = false;
				_details.animation.start();
			} else if ((state.mouseState == QEvent::MouseButtonPress)
				&& (wasXIndex == nearestXIndex)) {
				_details.hideOnAnimationEnd = true;
				_details.animation.start();
			}
			_details.widget->show();
			_chartArea->update();
		} break;
		case QEvent::MouseButtonRelease: {
		} break;
		}
	}, _details.widget->lifetime());

	_details.animation.init([=](crl::time now) {
		const auto value = std::clamp(
			(now - _details.animation.started()) / float64(200),
			0.,
			1.);
		const auto alpha = _details.hideOnAnimationEnd ? (1. - value) : value;
		if (_details.widget) {
			_details.widget->setAlpha(alpha);
			_details.widget->update();
		}
		if (value >= 1.) {
			if (_details.hideOnAnimationEnd && _details.widget) {
				_details.widget->hide();
				_details.widget->setXIndex(-1);
			}
			_details.animation.stop();
		}
		_chartArea->update();
	});
}

bool ChartWidget::hasLocalZoom() const {
	return _chartData
		&& _chartView->maybeLocalZoom({
			_chartData,
			AbstractChartView::LocalZoomArgs::Type::CheckAvailability,
		}).hasZoom;
}

void ChartWidget::processLocalZoom(int xIndex) {
	using Type = AbstractChartView::LocalZoomArgs::Type;
	constexpr auto kFooterZoomDuration = crl::time(400);
	const auto wasZoom = _footer->xPercentageLimits();

	const auto header = Ui::CreateChild<Header>(this);
	header->show();
	_header->geometryValue(
	) | rpl::start_with_next([=](const QRect &g) {
		header->setGeometry(g);
	}, header->lifetime());
	header->setRightInfo(_chartData.getDayString(xIndex));

	const auto enableMouse = [=](bool value) {
		setAttribute(Qt::WA_TransparentForMouseEvents, !value);
	};

	const auto mouseTrackingLifetime = std::make_shared<rpl::lifetime>();
	_chartView->setUpdateCallback([=] { _chartArea->update(); });
	const auto createMouseTracking = [=] {
		_chartArea->setMouseTracking(true);
		*mouseTrackingLifetime = _chartArea->events(
		) | rpl::filter([](not_null<QEvent*> event) {
			return (event->type() == QEvent::MouseMove)
				|| (event->type() == QEvent::Leave);
		}) | rpl::start_with_next([=](not_null<QEvent*> event) {
			auto pos = QPoint();
			if (event->type() == QEvent::MouseMove) {
				const auto e = static_cast<QMouseEvent*>(event.get());
				pos = e->pos();
			}
			_chartView->handleMouseMove(_chartData, _chartArea->rect(), pos);
		});
		mouseTrackingLifetime->add(crl::guard(_chartArea.get(), [=] {
			_chartArea->setMouseTracking(false);
		}));
	};

	const auto zoomOutButton = Ui::CreateChild<Ui::RoundButton>(
		header,
		tr::lng_stats_zoom_out(),
		st::statisticsHeaderButton);
	zoomOutButton->show();
	zoomOutButton->setTextTransform(
		Ui::RoundButton::TextTransform::NoTransform);
	zoomOutButton->setClickedCallback([=] {
		auto lifetime = std::make_shared<rpl::lifetime>();
		const auto animation = lifetime->make_state<Ui::Animations::Simple>();
		const auto currentXPercentage = _footer->xPercentageLimits();
		animation->start([=](float64 value) {
			_chartView->maybeLocalZoom({
				_chartData,
				Type::SkipCalculation,
				value,
			});
			const auto p = value;
			_footer->setXPercentageLimits({
				anim::interpolateF(wasZoom.min, currentXPercentage.min, p),
				anim::interpolateF(wasZoom.max, currentXPercentage.max, p),
			});
			if (value == 0.) {
				if (lifetime) {
					lifetime->destroy();
				}
				mouseTrackingLifetime->destroy();
				enableMouse(true);
			}
		}, 1., 0., kFooterZoomDuration, anim::easeOutCirc);
		enableMouse(false);

		Ui::Animations::HideWidgets({ header });
	});

	Ui::Animations::ShowWidgets({ header });

	zoomOutButton->moveToLeft(0, 0);

	const auto finish = [=](const Limits &zoomLimitIndices) {
		createMouseTracking();
		_footer->xPercentageLimitsChange(
		) | rpl::start_with_next([=](const Limits &l) {
			const auto result = FindStackXIndicesFromRawXPercentages(
				_chartData,
				l,
				zoomLimitIndices);
			header->setRightInfo(HeaderRightInfo(_chartData, result));
			header->update();
		}, header->lifetime());
	};

	{
		auto lifetime = std::make_shared<rpl::lifetime>();
		const auto animation = lifetime->make_state<Ui::Animations::Simple>();
		_chartView->maybeLocalZoom({ _chartData, Type::Prepare });
		animation->start([=](float64 value) {
			const auto zoom = _chartView->maybeLocalZoom({
				_chartData,
				Type::Process,
				value,
				xIndex,
			});
			const auto result = Limits{
				anim::interpolateF(wasZoom.min, zoom.range.min, value),
				anim::interpolateF(wasZoom.max, zoom.range.max, value),
			};
			_footer->setXPercentageLimits(result);
			if (value == 1.) {
				if (lifetime) {
					lifetime->destroy();
				}
				finish(zoom.limitIndices);
				enableMouse(true);
			}
		}, 0., 1., kFooterZoomDuration, anim::easeOutCirc);
		enableMouse(false);
	}
}

void ChartWidget::setupFilterButtons() {
	if (!_chartData || (_chartData.lines.size() <= 1)) {
		_filterButtons = nullptr;
		_chartArea->update();
		return;
	}
	_filterButtons = base::make_unique_q<ChartLinesFilterWidget>(this);

	_filterButtons->buttonEnabledChanges(
	) | rpl::start_with_next([=](const ChartLinesFilterWidget::Entry &e) {
		const auto now = crl::now();
		_linesFilterController->setEnabled(e.id, e.enabled, now);

		_animationController.setXPercentageLimits(
			_chartData,
			_animationController.currentXLimits(),
			_chartView,
			_linesFilterController,
			now);
	}, _filterButtons->lifetime());
}

void ChartWidget::setChartData(
		Data::StatisticalChart chartData,
		ChartViewType type) {
	if (width() < st::statisticsChartHeight) {
		sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			if (s.width() > st::statisticsChartHeight) {
				setChartData(chartData, type);
				_waitingSizeLifetime.destroy();
			}
		}, _waitingSizeLifetime);
		return;
	}
	_chartData = std::move(chartData);
	FillLineColorsByKey(_chartData);

	_chartView = CreateChartView(type);
	_chartView->setLinesFilterController(_linesFilterController);
	_rulersView.setChartData(_chartData, type);

	setupDetails();
	setupFilterButtons();

	const auto defaultZoom = Limits{
		_chartData.xPercentage[_chartData.defaultZoomXIndex.min],
		_chartData.xPercentage[_chartData.defaultZoomXIndex.max],
	};
	_footer->setXPercentageLimits(defaultZoom);
	_animationController.setXPercentageLimits(
		_chartData,
		defaultZoom,
		_chartView,
		_linesFilterController,
		0);
	updateChartFullWidth(_chartArea->width());
	updateHeader();
	updateBottomDates();
	_animationController.finish();
	_rulersView.add(_animationController.finalHeightLimits(), false);

	RpWidget::showChildren();
	_chartArea->update();
	_footer->update();
	RpWidget::resizeToWidth(width());
}

void ChartWidget::setTitle(rpl::producer<QString> &&title) {
	std::move(
		title
	) | rpl::start_with_next([=](QString t) {
		_header->setTitle(std::move(t));
		_header->update();
	}, _header->lifetime());
}

void ChartWidget::setZoomedChartData(
		Data::StatisticalChart chartData,
		float64 x,
		ChartViewType type) {
	_zoomedChartWidget = base::make_unique_q<ChartWidget>(
		dynamic_cast<Ui::RpWidget*>(parentWidget()));
	geometryValue(
	) | rpl::start_with_next([=](const QRect &geometry) {
		_zoomedChartWidget->moveToLeft(geometry.x(), geometry.y());
	}, _zoomedChartWidget->lifetime());
	_zoomedChartWidget->show();
	_zoomedChartWidget->resizeToWidth(width());
	_zoomedChartWidget->setChartData(std::move(chartData), type);

	const auto customHeader = Ui::CreateChild<Header>(
		_zoomedChartWidget.get());
	const auto xIndex = std::distance(
		begin(_chartData.x),
		ranges::find(_chartData.x, x));
	if ((xIndex >= 0) && (xIndex < _chartData.x.size())) {
		customHeader->setRightInfo(_chartData.getDayString(xIndex));
	}

	const auto zoomOutButton = Ui::CreateChild<Ui::RoundButton>(
		customHeader,
		tr::lng_stats_zoom_out(),
		st::statisticsHeaderButton);
	zoomOutButton->setTextTransform(
		Ui::RoundButton::TextTransform::NoTransform);
	zoomOutButton->setClickedCallback([=] {
		shownValue(
		) | rpl::start_with_next([=](bool shown) {
			if (shown) {
				_zoomedChartWidget = nullptr;
			}
		}, _zoomedChartWidget->lifetime());
		Ui::Animations::ShowWidgets({ this });
		Ui::Animations::HideWidgets({ _zoomedChartWidget.get() });
	});

	Ui::Animations::ShowWidgets({ _zoomedChartWidget.get(), customHeader });
	Ui::Animations::HideWidgets({ this });

	{
		const auto &headerPadding = st::statisticsChartHeaderPadding;
		customHeader->moveToLeft(headerPadding.left(), headerPadding.top());
		customHeader->resizeToWidth(width() - rect::m::sum::h(headerPadding));
	}
	zoomOutButton->moveToLeft(0, 0);
}

rpl::producer<float64> ChartWidget::zoomRequests() {
	_zoomEnabled = true;
	setupDetails();
	return _zoomRequests.events();
}

} // namespace Statistic
