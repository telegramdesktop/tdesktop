/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_widget.h"

#include "ui/effects/show_animation.h"
#include "base/qt/qt_key_modifiers.h"
#include "lang/lang_keys.h"
#include "statistics/chart_lines_filter_widget.h"
#include "statistics/point_details_widget.h"
#include "statistics/view/abstract_chart_view.h"
#include "statistics/view/chart_view_factory.h"
#include "ui/abstract_button.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/round_rect.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/buttons.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_statistics.h"

namespace Statistic {

namespace {

constexpr auto kHeightLimitsUpdateTimeout = crl::time(320);
constexpr auto kExpandingDelay = crl::time(1);

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

class ChartWidget::Header final : public RpWidget {
public:
	using RpWidget::RpWidget;

	void setTitle(QString title);
	void setRightInfo(QString rightInfo);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::Text::String _title;
	Ui::Text::String _rightInfo;
	int _titleWidth = 0;

};

void ChartWidget::Header::setTitle(QString title) {
	_titleWidth = st::statisticsHeaderTitleTextStyle.font->width(title);
	_title.setText(st::statisticsHeaderTitleTextStyle, std::move(title));
}

void ChartWidget::Header::setRightInfo(QString rightInfo) {
	_rightInfo.setText(
		st::statisticsHeaderDatesTextStyle,
		std::move(rightInfo));
}

void ChartWidget::Header::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	p.fillRect(rect(), st::boxBg);

	p.setPen(st::boxTextFg);
	const auto top = (height()
		- st::statisticsHeaderTitleTextStyle.font->height) / 2;
	_title.drawLeftElided(p, 0, top, width(), width());
	_rightInfo.drawRightElided(
		p,
		0,
		top,
		width() - _titleWidth,
		width(),
		1,
		style::al_right);
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
		const std::unique_ptr<AbstractChartView> &chartView,
		crl::time now) {
	if ((_animationValueXMin.to() == xPercentageLimits.min)
		&& (_animationValueXMax.to() == xPercentageLimits.max)
		&& chartView->isFinished()) {
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
		if (!chartView->isFinished()) {
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
			_addHorizontalLineRequests.fire({});
		}
		_dtHeight.speed = (!chartView->isFinished())
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

auto ChartWidget::ChartAnimationController::addHorizontalLineRequests() const
-> rpl::producer<> {
	return _addHorizontalLineRequests.events();
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
		std::vector<ChartHorizontalLinesData> &horizontalLines,
		std::vector<BottomCaptionLineData> &dateLines,
		const std::unique_ptr<AbstractChartView> &chartView) {
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

	// chartView->tick(now);
	{
		constexpr auto kDtHeightSpeed1 = 0.03 * 2;
		constexpr auto kDtHeightSpeed2 = 0.03 * 2;
		constexpr auto kDtHeightSpeed3 = 0.045 * 2;
		if (_dtHeight.current.max > 0 && _dtHeight.current.max < 1) {
			chartView->update(_dtHeight.current.max);
		} else {
			chartView->tick(now);
		}
	}

	if (xFinished
			&& yFinished
			&& alphaFinished
			&& bottomLineAlphaFinished
			&& footerMinFinished
			&& footerMaxFinished
			&& chartView->isFinished()) {
		const auto &lines = horizontalLines.back().lines;
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

		for (auto &horizontalLine : horizontalLines) {
			horizontalLine.computeRelative(
				_animationValueHeightMax.current(),
				_animationValueHeightMin.current());
		}
	}
	if (!footerMinFinished) {
		_animationValueFooterHeightMin.update(
			_dtHeight.current.min,
			anim::sineInOut);
	}
	if (!footerMaxFinished) {
		_animationValueFooterHeightMax.update(
			_dtHeight.current.max,
			anim::sineInOut);
	}

	if (!alphaFinished) {
		_animationValueHeightAlpha.update(
			_dtHeight.currentAlpha,
			anim::easeInCubic);
		const auto value = _animationValueHeightAlpha.current();

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

bool ChartWidget::ChartAnimationController::isFPSSlow() const {
	return _benchmark.lastFPSSlow;
}

ChartWidget::ChartWidget(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
, _chartArea(base::make_unique_q<RpMouseWidget>(this))
, _header(std::make_unique<Header>(this))
, _footer(std::make_unique<Footer>(this))
, _animationController([=] {
	_chartArea->update();
	if (_animationController.footerAnimating() || !_chartView->isFinished()) {
		_footer->update();
	}
}) {
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
	const auto filtersHeight = _filterButtons
		? _filterButtons->height()
		: 0;
	const auto resultHeight = st::statisticsChartHeight
		+ st::statisticsChartFooterHeight
		+ st::statisticsChartFooterSkip
		+ filtersHeight
		+ st::statisticsChartHeaderHeight;
	{
		_header->setGeometry(
			0,
			0,
			newWidth,
			st::statisticsChartHeaderHeight);
		_footer->setGeometry(
			0,
			resultHeight - st::statisticsChartFooterHeight - filtersHeight,
			newWidth,
			st::statisticsChartFooterHeight);
		if (_filterButtons) {
			_filterButtons->moveToLeft(0, resultHeight - filtersHeight);
		}
		_chartArea->setGeometry(
			0,
			st::statisticsChartHeaderHeight,
			newWidth,
			resultHeight
				- st::statisticsChartFooterHeight
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
			_chartView);

		const auto chartRect = chartAreaRect();

		p.fillRect(r, st::boxBg);

		for (auto &horizontalLine : _horizontalLines) {
			PaintHorizontalLines(p, horizontalLine, chartRect);
		}

		if (_chartData) {
			// p.setRenderHint(
			// 	QPainter::Antialiasing,
			// 	!_animationController.isFPSSlow()
			// 		|| !_animationController.animating());
			PainterHighQualityEnabler hp(p);
			_chartView->paint(
				p,
				_chartData,
				_animationController.currentXIndices(),
				_animationController.currentXLimits(),
				_animationController.currentHeightLimits(),
				chartRect,
				false);
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
		if (_details.widget) {
			const auto detailsAlpha = _details.widget->alpha();

			for (const auto &line : _chartData.lines) {
				_details.widget->setLineAlpha(
					line.id,
					_chartView->alpha(line.id));
			}
			_chartView->paintSelectedXIndex(
				p,
				_chartData,
				_animationController.currentXLimits(),
				_animationController.currentHeightLimits(),
				chartRect,
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
			rect::bottom(chartRect),
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
	_header->setRightInfo(_chartData.getDayString(indices.min)
		+ ' '
		+ QChar(8212)
		+ ' '
		+ _chartData.getDayString(indices.max));
	_header->update();
}

void ChartWidget::setupFooter() {
	_footer->setPaintChartCallback([=, fullXLimits = Limits{ 0., 1. }](
			QPainter &p,
			const QRect &r) {
		if (_chartData) {
			p.fillRect(r, st::boxBg);
			// p.setRenderHint(
			// 	QPainter::Antialiasing,
			// 	!_animationController.isFPSSlow()
			// 		|| !_animationController.animating());
			PainterHighQualityEnabler hp(p);
			_chartView->paint(
				p,
				_chartData,
				{ 0., float64(_chartData.x.size() - 1) },
				fullXLimits,
				_animationController.currentFooterHeightLimits(),
				r,
				true);
		}
	});

	_animationController.addHorizontalLineRequests(
	) | rpl::start_with_next([=] {
		addHorizontalLine(_animationController.finalHeightLimits(), true);
		_animationController.start();
	}, _footer->lifetime());

	_footer->xPercentageLimitsChange(
	) | rpl::start_with_next([=](Limits xPercentageLimits) {
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
			now);
		updateChartFullWidth(_chartArea->width());
		updateBottomDates();
		updateHeader();
		if ((now - _lastHeightLimitsChanged) < kHeightLimitsUpdateTimeout) {
			return;
		}
		_lastHeightLimitsChanged = now;
		addHorizontalLine(_animationController.finalHeightLimits(), true);
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
	_details.widget = base::make_unique_q<PointDetailsWidget>(
		this,
		_chartData,
		maxAbsoluteValue,
		_zoomEnabled);
	_details.widget->setClickedCallback([=] {
		if (const auto index = _details.widget->xIndex(); index >= 0) {
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
		_chartView->setEnabled(e.id, e.enabled, now);

		_animationController.setXPercentageLimits(
			_chartData,
			_animationController.currentXLimits(),
			_chartView,
			now);
	}, _filterButtons->lifetime());
}

void ChartWidget::setChartData(Data::StatisticalChart chartData) {
	_chartData = std::move(chartData);

	// _chartView = CreateChartView(ChartViewType::Linear);
	_chartView = CreateChartView(ChartViewType::Stack);

	setupDetails();
	setupFilterButtons();

	_animationController.setXPercentageLimits(
		_chartData,
		{ _chartData.xPercentage.front(), _chartData.xPercentage.back() },
		_chartView,
		0);
	updateChartFullWidth(_chartArea->width());
	updateHeader();
	updateBottomDates();
	_animationController.finish();
	addHorizontalLine(_animationController.finalHeightLimits(), false);

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
		float64 x) {
	_zoomedChartWidget = base::make_unique_q<ChartWidget>(
		dynamic_cast<Ui::RpWidget*>(parentWidget()));
	_zoomedChartWidget->setChartData(std::move(chartData));
	geometryValue(
	) | rpl::start_with_next([=](const QRect &geometry) {
		_zoomedChartWidget->moveToLeft(geometry.x(), geometry.y());
	}, _zoomedChartWidget->lifetime());
	_zoomedChartWidget->show();
	_zoomedChartWidget->resizeToWidth(width());

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

	customHeader->setGeometry(0, 0, width(), st::statisticsChartHeaderHeight);
	zoomOutButton->moveToLeft(0, 0);
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

rpl::producer<float64> ChartWidget::zoomRequests() {
	_zoomEnabled = true;
	setupDetails();
	return _zoomRequests.events();
}

} // namespace Statistic
