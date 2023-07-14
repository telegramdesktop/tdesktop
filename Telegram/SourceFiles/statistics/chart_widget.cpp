/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_widget.h"

#include "base/qt/qt_key_modifiers.h"
#include "statistics/chart_lines_filter_widget.h"
#include "statistics/linear_chart_view.h"
#include "statistics/point_details_widget.h"
#include "ui/abstract_button.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/round_rect.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image_prepare.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_statistics.h"

namespace Statistic {

namespace {

constexpr auto kHeightLimitsUpdateTimeout = crl::time(320);
constexpr auto kExpandingDelay = crl::time(100);

inline float64 InterpolationRatio(float64 from, float64 to, float64 result) {
	return (result - from) / (to - from);
};

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
		p.fillRect(lineRect, st::windowSubTextFg);
	}
	p.setOpacity(alpha);
}

void PaintCaptionsToHorizontalLines(
		QPainter &p,
		const ChartHorizontalLinesData &horizontalLine,
		const QRect &r) {
	const auto alpha = p.opacity();
	p.setOpacity(horizontalLine.alpha);
	p.setFont(st::statisticsDetailsBottomCaptionStyle.font);
	p.setPen(st::windowSubTextFg);
	const auto offset = r.y() - st::statisticsChartHorizontalLineCaptionSkip;
	for (const auto &line : horizontalLine.lines) {
		p.drawText(0, offset + r.height() * line.relativeValue, line.caption);
	}
	p.setOpacity(alpha);
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
				st::statisticsChartBottomCaptionSkip);
			const auto edgeAlpha = (r.x() < 0)
				? std::max(
					0.,
					1. + (r.x() / edgeAlphaSize))
				: (rect::right(r) > chartWidth)
				? std::max(
					0.,
					1. + ((chartWidth - rect::right(r)) / edgeAlphaSize))
				: 1.;
			p.setOpacity(edgeAlpha
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

	Footer(not_null<Ui::RpWidget*> parent);

	[[nodiscard]] rpl::producer<Limits> xPercentageLimitsChange() const;
	[[nodiscard]] rpl::producer<> userInteractionFinished() const;

	void setPaintChartCallback(PaintCallback paintChartCallback);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	rpl::event_stream<Limits> _xPercentageLimitsChange;
	rpl::event_stream<> _userInteractionFinished;

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
		_mask = Ui::RippleAnimation::RoundRectMask(
			s - QSize(0, st::statisticsChartLineWidth * 2),
			st::boxRadius);
		_frame = _mask;
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
				const auto toLeft = posX <= start().x();
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
				_userInteractionFinished.fire({});
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

void ChartWidget::Footer::fire() const {
	_xPercentageLimitsChange.fire({
		.min = _leftSide.min / float64(width()),
		.max = _rightSide.max / float64(width()),
	});
}

void ChartWidget::Footer::moveCenter(
		bool isDirectionToLeft,
		float64 x,
		float64 diffBetweenStartAndLeft) {
	const auto resultX = x - diffBetweenStartAndLeft;
	const auto diffBetweenSides = _rightSide.min - _leftSide.min;
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
	if (left) {
		const auto min = std::clamp(x, 0., _rightSide.min - w);
		_leftSide = Limits{ .min = min, .max = min + w };
	} else if (!left) {
		const auto min = std::clamp(x, _leftSide.max, width() - w);
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
	const auto inactiveLeftRect = Rect(QSizeF(_leftSide.max, r.height()))
		- innerMargins;
	const auto inactiveRightRect = r
		- QMarginsF{ _rightSide.min, 0, 0, 0 }
		- innerMargins;
	const auto &inactiveColor = st::statisticsChartInactive;

	_frame.fill(Qt::transparent);
	if (_paintChartCallback) {
		auto q = QPainter(&_frame);

		_paintChartCallback(q, Rect(innerRect.size()));

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

rpl::producer<Limits> ChartWidget::Footer::xPercentageLimitsChange() const {
	return _xPercentageLimitsChange.events();
}

rpl::producer<> ChartWidget::Footer::userInteractionFinished() const {
	return _userInteractionFinished.events();
}

ChartWidget::ChartAnimationController::ChartAnimationController(
	Fn<void()> &&updateCallback)
: _animation(std::move(updateCallback)) {
}

void ChartWidget::ChartAnimationController::setXPercentageLimits(
		Data::StatisticalChart &chartData,
		Limits xPercentageLimits,
		const ChartLineViewContext &chartLinesViewContext,
		crl::time now) {
	if ((_animationValueXMin.to() == xPercentageLimits.min)
		&& (_animationValueXMax.to() == xPercentageLimits.max)
		&& chartLinesViewContext.isFinished()) {
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
		auto minValue = std::numeric_limits<int>::max();
		auto maxValue = 0;

		auto minValueFull = std::numeric_limits<int>::max();
		auto maxValueFull = 0;
		for (auto &l : chartData.lines) {
			if (!chartLinesViewContext.isEnabled(l.id)) {
				continue;
			}
			const auto lineMax = l.segmentTree.rMaxQ(startXIndex, endXIndex);
			const auto lineMin = l.segmentTree.rMinQ(startXIndex, endXIndex);
			maxValue = std::max(lineMax, maxValue);
			minValue = std::min(lineMin, minValue);

			maxValueFull = std::max(l.maxValue, maxValueFull);
			minValueFull = std::min(l.minValue, minValueFull);
		}
		_finalHeightLimits = { float64(minValue), float64(maxValue) };
		if (!chartLinesViewContext.isFinished()) {
			_animationValueFooterHeightMin = anim::value(
				_animationValueFooterHeightMin.current(),
				minValueFull);
			_animationValueFooterHeightMax = anim::value(
				_animationValueFooterHeightMax.current(),
				maxValueFull);
		} else if (!_animationValueFooterHeightMax.to()) {
			// Will be finished in setChartData.
			_animationValueFooterHeightMin = anim::value(0, minValueFull);
			_animationValueFooterHeightMax = anim::value(0, maxValueFull);
		}
	}

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
	_animationValueFooterHeightMin.finish();
	_animationValueFooterHeightMax.finish();
	_animValueYAlpha.finish();
	_benchmark = {};
}

void ChartWidget::ChartAnimationController::resetAlpha() {
	_alphaAnimationStartedAt = 0;
	_animValueYAlpha = anim::value(0., 1.);
}

void ChartWidget::ChartAnimationController::restartBottomLineAlpha() {
	_bottomLineAlphaAnimationStartedAt = crl::now();
	_animValueBottomLineAlpha = anim::value(0., 1.);
	start();
}

void ChartWidget::ChartAnimationController::tick(
		crl::time now,
		std::vector<ChartHorizontalLinesData> &horizontalLines,
		std::vector<BottomCaptionLineData> &dateLines,
		ChartLineViewContext &chartLinesViewContext) {
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
		_dtCurrent.min = std::min(_dtCurrent.min + _dtHeightSpeed * k, 1.);
		_dtCurrent.max = std::min(_dtCurrent.max + _dtHeightSpeed * k, 1.);
	}

	const auto dtX = std::min(
		(now - _animation.started()) / kXExpandingDuration,
		1.);
	const auto dtAlpha = std::min(
		(now - _alphaAnimationStartedAt) / kAlphaExpandingDuration,
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
	const auto alphaFinished = isFinished(_animValueYAlpha);
	const auto bottomLineAlphaFinished = isFinished(
		_animValueBottomLineAlpha);

	chartLinesViewContext.tick(now);

	if (xFinished
			&& yFinished
			&& alphaFinished
			&& bottomLineAlphaFinished
			&& chartLinesViewContext.isFinished()) {
		const auto &lines = horizontalLines.back().lines;
		if ((lines.front().absoluteValue == _animationValueHeightMin.to())
			&& lines.back().absoluteValue == _animationValueHeightMax.to()) {
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
	if (_heightAnimationStarted) {
		_animationValueHeightMin.update(_dtCurrent.min, anim::easeInCubic);
		_animationValueHeightMax.update(_dtCurrent.max, anim::easeInCubic);
		_animValueYAlpha.update(dtAlpha, anim::easeInCubic);
		_animationValueFooterHeightMin.update(
			_dtCurrent.min,
			anim::easeInCubic);
		_animationValueFooterHeightMax.update(
			_dtCurrent.max,
			anim::easeInCubic);

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

	if (yFinished && alphaFinished) {
		_alphaAnimationStartedAt = 0;
		_heightAnimationStarted = false;
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

float64 ChartWidget::ChartAnimationController::detailsProgress(
		crl::time now,
		const Limits &appearedOnXLimits) const {
	const auto xLimitsChanged = false
		|| (appearedOnXLimits.min != _animationValueXMin.to())
		|| (appearedOnXLimits.max != _animationValueXMax.to());
	return (_animation.animating() && xLimitsChanged)
		? std::clamp(
			(now - _animation.started()) / float64(kExpandingDelay),
			0.,
			1.)
		: 0.;
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

bool ChartWidget::ChartAnimationController::isFPSSlow() const {
	return _benchmark.lastFPSSlow;
}

auto ChartWidget::ChartAnimationController::heightAnimationStarts() const
-> rpl::producer<> {
	return _heightAnimationStarts.events();
}

ChartWidget::ChartWidget(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
, _chartArea(base::make_unique_q<RpMouseWidget>(this))
, _footer(std::make_unique<Footer>(this))
, _animationController([=] {
	_chartArea->update();
	if (_animationController.footerAnimating()
		|| !_animatedChartLines.isFinished()) {
		_footer->update();
	}
}) {
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto filtersHeight = _filterButtons
			? _filterButtons->height()
			: 0;
		_footer->setGeometry(
			0,
			s.height() - st::statisticsChartFooterHeight - filtersHeight,
			s.width(),
			st::statisticsChartFooterHeight);
		if (_filterButtons) {
			_filterButtons->setGeometry(
				0,
				s.height() - filtersHeight,
				s.width(),
				filtersHeight);
		}
		_chartArea->setGeometry(
			0,
			0,
			s.width(),
			s.height()
				- st::statisticsChartFooterHeight
				- filtersHeight
				- st::statisticsChartFooterSkip);
	}, lifetime());

	setupChartArea();
	setupFooter();

	resizeHeight();
}

void ChartWidget::resizeHeight() {
	resize(
		width(),
		st::statisticsChartHeight
			+ st::statisticsChartFooterHeight
			+ st::statisticsChartFooterSkip
			+ (_filterButtons ? _filterButtons->height() : 0));
}

QRect ChartWidget::chartAreaRect() const {
	return _chartArea->rect()
		- QMargins(
			st::lineWidth,
			st::boxTextFont->height,
			st::lineWidth,
			st::lineWidth + st::statisticsChartBottomCaptionHeight);
}

void ChartWidget::setupChartArea() {
	_chartArea->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(_chartArea.get());

		const auto now = crl::now();

		_animationController.tick(
			now,
			_horizontalLines,
			_bottomLine.dates,
			_animatedChartLines);

		const auto chartRect = chartAreaRect();

		p.fillRect(r, st::boxBg);

		for (auto &horizontalLine : _horizontalLines) {
			PaintHorizontalLines(p, horizontalLine, chartRect);
		}

		const auto detailsAlpha = 1.
			- _animationController.detailsProgress(
				now,
				_details.appearedOnXLimits);

		if (_details.widget) {
			if (!detailsAlpha && _details.currentX) {
				_details.widget->hide();
				_details.widget->setXIndex(-1);
				_details.currentX = 0;
				_details.appearedOnXLimits = {};
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
				for (const auto &line : _chartData.lines) {
					_details.widget->setLineAlpha(
						line.id,
						_animatedChartLines.alpha(line.id));
				}
			}
		}

		auto detailsPaintContext = DetailsPaintContext{
			.xIndex = (_details.widget && (detailsAlpha > 0.))
				? _details.widget->xIndex()
				: -1,
		};
		if (_chartData) {
			p.setRenderHint(
				QPainter::Antialiasing,
				!_animationController.isFPSSlow()
					|| !_animationController.animating());
			Statistic::PaintLinearChartView(
				p,
				_chartData,
				_animationController.currentXIndices(),
				_animationController.currentXLimits(),
				_animationController.currentHeightLimits(),
				chartRect,
				_animatedChartLines,
				detailsPaintContext);
		}

		for (auto &horizontalLine : _horizontalLines) {
			PaintCaptionsToHorizontalLines(p, horizontalLine, chartRect);
		}
		{
			const auto bottom = r
				- QMargins{ 0, rect::bottom(chartRect), 0, 0 };
			p.fillRect(bottom, st::boxBg);
			p.fillRect(
				QRect(bottom.x(), bottom.y(), bottom.width(), st::lineWidth),
				st::windowSubTextFg);
		}
		for (const auto &dot : detailsPaintContext.dots) {
			p.setBrush(st::boxBg);
			p.setPen(QPen(dot.color, st::statisticsChartLineWidth));
			const auto r = st::statisticsDetailsDotRadius;
			auto hq = PainterHighQualityEnabler(p);
			auto o = ScopedPainterOpacity(p, dot.alpha * detailsAlpha);
			p.drawEllipse(dot.point, r, r);
		}

		p.setPen(st::windowSubTextFg);
		PaintBottomLine(
			p,
			_bottomLine.dates,
			_chartData,
			_animationController.finalXLimits(),
			_bottomLine.chartFullWidth,
			_chartArea->width(),
			rect::bottom(chartRect),
			_bottomLine.captionIndicesOffset);
	}, _footer->lifetime());
}

void ChartWidget::updateBottomDates() {
	if (!_chartData) {
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

void ChartWidget::setupFooter() {
	_footer->setPaintChartCallback([=, fullXLimits = Limits{ 0., 1. }](
			QPainter &p,
			const QRect &r) {
		if (_chartData) {
			auto detailsPaintContext = DetailsPaintContext{ .xIndex = -1 };
			p.fillRect(r, st::boxBg);
			p.setRenderHint(
				QPainter::Antialiasing,
				!_animationController.isFPSSlow()
					|| !_animationController.animating());
			Statistic::PaintLinearChartView(
				p,
				_chartData,
				{ 0., float64(_chartData.x.size() - 1) },
				fullXLimits,
				_animationController.currentFooterHeightLimits(),
				r,
				_animatedChartLines,
				detailsPaintContext);
		}
	});

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
			_animatedChartLines,
			now);
		{
			const auto finalXLimits = _animationController.finalXLimits();
			_bottomLine.chartFullWidth = _chartArea->width()
				/ (finalXLimits.max - finalXLimits.min);
		}
		updateBottomDates();
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
	const auto maxAbsoluteValue = [&] {
		auto maxValue = 0;
		for (const auto &l : _chartData.lines) {
			maxValue = std::max(l.maxValue, maxValue);
		}
		return maxValue;
	}();
	_details.widget = base::make_unique_q<PointDetailsWidget>(
		this,
		_chartData,
		maxAbsoluteValue);

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
			_details.appearedOnXLimits = currentXLimits;
			const auto xLeft = _details.currentX
				- _details.widget->width();
			const auto x = (xLeft >= 0)
				? xLeft
				: ((_details.currentX
					+ _details.widget->width()
					- _chartArea->width()) > 0)
				? 0
				: _details.currentX;
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

void ChartWidget::setupFilterButtons() {
	if (!_chartData) {
		_filterButtons = nullptr;
		_chartArea->update();
		return;
	}
	_filterButtons = base::make_unique_q<ChartLinesFilterWidget>(this);

	sizeValue(
	) | rpl::filter([=](const QSize &s) {
		return s.width() > 0;
	}) | rpl::take(1) | rpl::start_with_next([=](const QSize &s) {
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

		_filterButtons->fillButtons(texts, colors, ids, s.width());
		resizeHeight();
	}, _filterButtons->lifetime());

	_filterButtons->buttonEnabledChanges(
	) | rpl::start_with_next([=](const ChartLinesFilterWidget::Entry &e) {
		const auto now = crl::now();
		_animatedChartLines.setEnabled(e.id, e.enabled, now);

		_animationController.setXPercentageLimits(
			_chartData,
			_animationController.currentXLimits(),
			_animatedChartLines,
			now);
	}, _filterButtons->lifetime());
}

void ChartWidget::setChartData(Data::StatisticalChart chartData) {
	_chartData = std::move(chartData);

	setupDetails();
	setupFilterButtons();

	_animationController.setXPercentageLimits(
		_chartData,
		{ _chartData.xPercentage.front(), _chartData.xPercentage.back() },
		_animatedChartLines,
		0);
	{
		const auto finalXLimits = _animationController.finalXLimits();
		_bottomLine.chartFullWidth = _chartArea->width()
			/ (finalXLimits.max - finalXLimits.min);
	}
	updateBottomDates();
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
