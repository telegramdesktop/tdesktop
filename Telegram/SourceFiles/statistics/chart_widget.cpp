/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_widget.h"

#include "base/qt/qt_key_modifiers.h"
#include "statistics/linear_chart_view.h"
#include "ui/abstract_button.h"
#include "ui/effects/animation_value_f.h"
#include "ui/rect.h"
#include "styles/style_boxes.h"

namespace Statistic {

namespace {

constexpr auto kHeightLimitsUpdateTimeout = crl::time(320);

[[nodiscard]] bool AnimFinished(const anim::value &anim) {
	return anim.current() == anim.to();
}

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

class ChartWidget::Footer final : public Ui::AbstractButton {
public:
	Footer(not_null<Ui::RpWidget*> parent);

	[[nodiscard]] rpl::producer<Limits> xPercentageLimitsChange() const;
	[[nodiscard]] rpl::producer<> userInteractionFinished() const;
	[[nodiscard]] rpl::producer<> directionChanges() const;

private:
	not_null<Ui::AbstractButton*> _left;
	not_null<Ui::AbstractButton*> _right;

	rpl::event_stream<Limits> _xPercentageLimitsChange;
	rpl::event_stream<> _userInteractionFinished;
	rpl::event_stream<> _directionChanges;

	struct {
		int x = 0;
		int leftLimit = 0;
		int rightLimit = 0;
		int diffX = 0;
	} _start;

};

ChartWidget::Footer::Footer(not_null<Ui::RpWidget*> parent)
: Ui::AbstractButton(parent)
, _left(Ui::CreateChild<Ui::AbstractButton>(this))
, _right(Ui::CreateChild<Ui::AbstractButton>(this)) {
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

	_left->move(10, 0);
	_right->move(50, 0);

	const auto handleDrag = [&](
			not_null<Ui::AbstractButton*> side,
			Fn<int()> leftLimit,
			Fn<int()> rightLimit) {
		side->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return (e->type() == QEvent::MouseButtonPress)
				|| (e->type() == QEvent::MouseButtonRelease)
				|| ((e->type() == QEvent::MouseMove) && side->isDown());
		}) | rpl::start_with_next([=](not_null<QEvent*> e) {
			const auto pos = static_cast<QMouseEvent*>(e.get())->pos();
			switch (e->type()) {
			case QEvent::MouseMove: {
				const auto nowDiffXDirection = (pos.x() - _start.x) < 0;
				const auto wasDiffXDirection = _start.diffX < 0;
				if (base::IsCtrlPressed()) {
					const auto diff = (pos.x() - _start.x);
					_left->move(_left->x() + diff, side->y());
					_right->move(_right->x() + diff, side->y());
				} else {
					_start.diffX = pos.x() - _start.x;
					const auto nextX = std::clamp(
						side->x() + (pos.x() - _start.x),
						_start.leftLimit,
						_start.rightLimit);
					side->move(nextX, side->y());
				}
				_xPercentageLimitsChange.fire({
					.min = _left->x() / float64(width()),
					.max = rect::right(_right) / float64(width()),
				});
				if (nowDiffXDirection != wasDiffXDirection) {
					_directionChanges.fire({});
				}
			} break;
			case QEvent::MouseButtonPress: {
				_start.x = pos.x();
				_start.leftLimit = leftLimit();
				_start.rightLimit = rightLimit();
			} break;
			case QEvent::MouseButtonRelease: {
				_userInteractionFinished.fire({});
				_xPercentageLimitsChange.fire({
					.min = _left->x() / float64(width()),
					.max = rect::right(_right) / float64(width()),
				});
				_start = {};
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

rpl::producer<> ChartWidget::Footer::directionChanges() const {
	return _directionChanges.events();
}

ChartWidget::ChartWidget(not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent)
, _footer(std::make_unique<Footer>(this)) {
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_footer->setGeometry(
			0,
			s.height() - st::countryRowHeight,
			s.width(),
			st::countryRowHeight);
	}, _footer->lifetime());
	_footer->paintRequest(
	) | rpl::start_with_next([=, limits = Limits{ 0., 1. }] {
		auto p = QPainter(_footer.get());

		if (_chartData) {
			const auto startXIndex2 = 0;
			const auto endXIndex2 = int(_chartData.xPercentage.size() - 1);
			const auto limitsY = Limits{
				float64(FindMinValue(_chartData, startXIndex2, endXIndex2)),
				float64(FindMaxValue(_chartData, startXIndex2, endXIndex2)),
			};
			p.fillRect(_footer->rect(), st::boxBg);
			Statistic::PaintLinearChartView(
				p,
				_chartData,
				{},
				limits,
				limitsY,
				_footer->rect());
		}
	}, _footer->lifetime());

	constexpr auto kExpandingDelay = crl::time(100);
	constexpr auto kXExpandingDuration = 200.;
	constexpr auto kYExpandingDuration = 400.;
	constexpr auto kAlphaExpandingDuration = 120.;
	_xPercentage.animation.init([=](crl::time now) {
		// if ((_xPercentage.yAnimationStartedAt && (now - _xPercentage.lastUserInteracted) < kExpandingDelay)) {
		// 	_xPercentage.yAnimationStartedAt = _xPercentage.lastUserInteracted;
		// }
		if (!_xPercentage.yAnimationStartedAt
			&& ((now - _xPercentage.lastUserInteracted) >= kExpandingDelay)) {
			// if (!_xPercentage.yAnimationStartedAt) {
			// _xPercentage.animValueYAlpha = anim::value(0., 1.);
			// {
			// 	const auto startXIndex = _chartData.findStartIndex(
			// 		_xPercentage.now.min);
			// 	const auto endXIndex = _chartData.findEndIndex(
			// 		startXIndex,
			// 		_xPercentage.now.max);
			// 	addHorizontalLine(
			// 		{
			// 			float64(FindMinValue(_chartData, startXIndex, endXIndex)),
			// 			float64(FindMaxValue(_chartData, startXIndex, endXIndex)),
			// 		},
			// 		true);
			// }

			// }
			_xPercentage.yAnimationStartedAt = _xPercentage.lastUserInteracted
				+ kExpandingDelay;
		}
		_xPercentage.dtCurrent.min  = std::min(
			_xPercentage.dtCurrent.min + _xPercentage.dtYSpeed,
			1.);
		_xPercentage.dtCurrent.max = std::min(
			_xPercentage.dtCurrent.max + _xPercentage.dtYSpeed,
			1.);
		const auto dtY = std::min(
			(now - _xPercentage.yAnimationStartedAt) / kYExpandingDuration,
			1.);
		const auto dtAlpha = std::min(
			(now - _xPercentage.yAnimationStartedAt) / kAlphaExpandingDuration,
			1.);
		const auto dtX = std::min(
			(now - _xPercentage.animation.started()) / kXExpandingDuration,
			1.);

		const auto xFinished = AnimFinished(_xPercentage.animValueXMin)
			&& AnimFinished(_xPercentage.animValueXMax);
		const auto yFinished = AnimFinished(_xPercentage.animValueYMin)
			&& AnimFinished(_xPercentage.animValueYMax);
		if (xFinished && yFinished) {
			_xPercentage.animation.stop();
		}
		if (xFinished) {
			_xPercentage.animValueXMin.finish();
			_xPercentage.animValueXMax.finish();

			_xPercentage.was = _xPercentage.now;
		} else {
			_xPercentage.animValueXMin.update(dtX, anim::linear);
			_xPercentage.animValueXMax.update(dtX, anim::linear);
		}
		// if (yFinished) {
		// 	// _xPercentage.animValueYMin.finish();
		// 	// _xPercentage.animValueYMax.finish();

		// 	_xPercentage.yAnimationStartedAt = 0;
		// }
		if (_xPercentage.yAnimationStartedAt) {
			_xPercentage.animValueYMin.update(dtY, anim::sineInOut);
			_xPercentage.animValueYMax.update(dtY, anim::sineInOut);
			_xPercentage.animValueYAlpha.update(dtY, anim::sineInOut);

			auto &&subrange = ranges::make_subrange(
				begin(_horizontalLines),// + 1,
				end(_horizontalLines));
			for (auto &horizontalLine : std::move(subrange)) {
				horizontalLine.computeRelative(
					_xPercentage.animValueYMax.current(),
					_xPercentage.animValueYMin.current());
			}
		}

		if (yFinished) {
			_xPercentage.animValueYAlpha.finish();
		}
		if (_xPercentage.yAnimationStartedAt) {
			const auto value = _xPercentage.animValueYAlpha.current();
			_horizontalLines.back().alpha = value;

			const auto startIt = begin(_horizontalLines);
			const auto endIt = end(_horizontalLines);
			for (auto it = startIt; it != (endIt - 1); it++) {
				const auto was = it->alpha;
				it->alpha = it->fixedAlpha * (1. - value);
				const auto now = it->alpha;
			}
			if (value == 1.) {
				while (_horizontalLines.size() > 1) {
					const auto startIt = begin(_horizontalLines);
					if (!startIt->alpha) {
						_horizontalLines.erase(startIt);
					} else {
						break;
					}
				}
			}
		}
		if (yFinished) {
			// _xPercentage.animValueYMin.finish();
			// _xPercentage.animValueYMax.finish();
			_xPercentage.animValueYAlpha.finish();

			_xPercentage.yAnimationStartedAt = 0;
		}

		update();
	});

	_footer->userInteractionFinished(
	) | rpl::start_with_next([=] {
		// _xPercentage.yAnimationStartedAt = crl::now();
		_xPercentage.animValueYAlpha = anim::value(0., 1.);

		{
			const auto startXIndex = _chartData.findStartIndex(
				_xPercentage.now.min);
			const auto endXIndex = _chartData.findEndIndex(
				startXIndex,
				_xPercentage.now.max);
			addHorizontalLine(
				{
					float64(FindMinValue(_chartData, startXIndex, endXIndex)),
					float64(FindMaxValue(_chartData, startXIndex, endXIndex)),
				},
				true);
		}
	}, _footer->lifetime());

	_footer->directionChanges(
	) | rpl::start_with_next([=] {
		// _xPercentage.yAnimationStartedAt = crl::now();
		// _xPercentage.animValueYAlpha = anim::value(0., 1.);

		// {
		// 	const auto startXIndex = _chartData.findStartIndex(
		// 		_xPercentage.now.min);
		// 	const auto endXIndex = _chartData.findEndIndex(
		// 		startXIndex,
		// 		_xPercentage.now.max);
		// 	addHorizontalLine(
		// 		{
		// 			float64(FindMinValue(_chartData, startXIndex, endXIndex)),
		// 			float64(FindMaxValue(_chartData, startXIndex, endXIndex)),
		// 		},
		// 		true);
		// }
	}, _footer->lifetime());

	_footer->xPercentageLimitsChange(
	) | rpl::start_with_next([=](Limits xPercentageLimits) {
		if ((_xPercentage.now.min == xPercentageLimits.min)
			&& (_xPercentage.now.max == xPercentageLimits.max)) {
			return;
		}
		if (!_xPercentage.animation.animating()) {
			_xPercentage.animation.start();
		}
		_xPercentage.animValueXMin.start(xPercentageLimits.min);
		_xPercentage.animValueXMax.start(xPercentageLimits.max);
		_xPercentage.now = xPercentageLimits;
		_xPercentage.lastUserInteracted = crl::now();

		const auto &chartData = _chartData;
		{
			auto minY = std::numeric_limits<float64>::max();
			auto maxY = 0.;
			auto minYIndex = 0;
			auto maxYIndex = 0;
			const auto tempXPercentage = Limits{
				.min = *ranges::lower_bound(
					chartData.xPercentage,
					xPercentageLimits.min),
				.max = *ranges::lower_bound(
					chartData.xPercentage,
					xPercentageLimits.max),
			};
			for (auto i = 0; i < chartData.xPercentage.size(); i++) {
				if (chartData.xPercentage[i] == tempXPercentage.min) {
					minYIndex = i;
				}
				if (chartData.xPercentage[i] == tempXPercentage.max) {
					maxYIndex = i;
				}
			}
			for (const auto &line : chartData.lines) {
				for (auto i = minYIndex; i < maxYIndex; i++) {
					if (line.y[i] > maxY) {
						maxY = line.y[i];
					}
					if (line.y[i] < minY) {
						minY = line.y[i];
					}
				}
			}
			// if (_xPercentage.animValueYMin.from() == minY) {
			// 	minY += 1;
			// 	// _xPercentage.animValueYMin.finish();
			// }
			// if (_xPercentage.animValueYMax.from() == maxY) {
			// 	// maxY -= 0.1;
			// 	_xPercentage.animValueYMax.finish();
			// }
			_xPercentage.animValueYMin = anim::value(
				_xPercentage.animValueYMin.current(),
				minY);
			_xPercentage.animValueYMax = anim::value(
				_xPercentage.animValueYMax.current(),
				maxY);

			{
				auto k = (_xPercentage.animValueYMax.current() - _xPercentage.animValueYMin.current())
					/ float64(maxY - minY);
				if (k > 1.) {
					k = 1. / k;
				}
				// constexpr auto kUpdateStep1 = 0.1;
				constexpr auto kUpdateStep1 = 0.03;
				constexpr auto kUpdateStep2 = 0.03;
				constexpr auto kUpdateStep3 = 0.045;
				constexpr auto kUpdateStepThreshold1 = 0.7;
				constexpr auto kUpdateStepThreshold2 = 0.1;
				_xPercentage.dtYSpeed = (k > kUpdateStepThreshold1)
					? kUpdateStep1
					: (k < kUpdateStepThreshold2)
					? kUpdateStep2
					: kUpdateStep3;
				_xPercentage.dtCurrent = { 0., 0. };
			}

			// _horizontalLines.front().computeRelative(maxY, minY);
		}
		{
			const auto now = crl::now();
			if ((now - _lastHeightLimitsChanged) < kHeightLimitsUpdateTimeout) {
				return;
			}
			_lastHeightLimitsChanged = now;

			_xPercentage.animValueYAlpha = anim::value(0., 1.);
			{
				const auto startXIndex = _chartData.findStartIndex(
					_xPercentage.now.min);
				const auto endXIndex = _chartData.findEndIndex(
					startXIndex,
					_xPercentage.now.max);
				addHorizontalLine(
					{
						float64(FindMinValue(_chartData, startXIndex, endXIndex)),
						float64(FindMaxValue(_chartData, startXIndex, endXIndex)),
					},
					true);
			}
		}
		// _xPercentage.animation.stop();
		// const auto was = _xPercentageLimits;
		// const auto now = xPercentageLimits;
		// _xPercentage.animation.start([=](float64 value) {
		// 	_xPercentageLimits = {
		// 		.min = *ranges::lower_bound(
		// 			_chartData.xPercentage,
		// 			anim::interpolateF(was.min, now.min, value)),
		// 		.max = *ranges::lower_bound(
		// 			_chartData.xPercentage,
		// 			anim::interpolateF(was.max, now.max, value)),
		// 	};
		// 	const auto startXIndex = _chartData.findStartIndex(
		// 		_xPercentageLimits.min);
		// 	const auto endXIndex = _chartData.findEndIndex(
		// 		startXIndex,
		// 		_xPercentageLimits.max);
		// 	setHeightLimits(
		// 		{
		// 			float64(FindMinValue(_chartData, startXIndex, endXIndex)),
		// 			float64(FindMaxValue(_chartData, startXIndex, endXIndex)),
		// 		},
		// 		false);
		// 	update();
		// }, 0., 1., 400);
	}, _footer->lifetime());
	resize(width(), st::confirmMaxHeight + st::countryRowHeight * 2);
}

void ChartWidget::setChartData(Data::StatisticalChart chartData) {
	_chartData = chartData;

	{
		_xPercentageLimits = {
			.min = _chartData.xPercentage.front(),
			.max = _chartData.xPercentage.back(),
		};
		_xPercentage.now = {
			.min = _chartData.xPercentage.front(),
			.max = _chartData.xPercentage.back(),
		};
		const auto startXIndex = _chartData.findStartIndex(
			_xPercentageLimits.min);
		const auto endXIndex = _chartData.findEndIndex(
			startXIndex,
			_xPercentageLimits.max);
		setHeightLimits(
			{
				float64(FindMinValue(_chartData, startXIndex, endXIndex)),
				float64(FindMaxValue(_chartData, startXIndex, endXIndex)),
			},
			false);
		update();
	}
}

void ChartWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto r = rect();
	const auto captionRect = r;
	const auto chartRectBottom = st::lineWidth
		+ _footer->height()
		+ st::countryRowHeight;
	const auto chartRect = r
		- QMargins{ 0, st::boxTextFont->height, 0, chartRectBottom };

	p.fillRect(r, st::boxBg);

	for (auto &horizontalLine : _horizontalLines) {
		PaintHorizontalLines(p, horizontalLine, chartRect);
	}

	if (_chartData) {
		Statistic::PaintLinearChartView(
			p,
			_chartData,
			_xPercentage.was,
			{ _xPercentage.animValueXMin.current(), _xPercentage.animValueXMax.current() },
			{ _xPercentage.animValueYMin.current(), _xPercentage.animValueYMax.current() },
			// _xPercentage.now,
			chartRect);
	}

	for (auto &horizontalLine : _horizontalLines) {
		PaintCaptionsToHorizontalLines(p, horizontalLine, chartRect);
	}
}

void ChartWidget::setHeightLimits(Limits newHeight, bool animated) {
	{
		const auto lineMaxHeight = ChartHorizontalLinesData::LookupHeight(
			newHeight.max);
		const auto diff = std::abs(lineMaxHeight - _animateToHeight.max);
		const auto heightChanged = (!newHeight.max)
			|| (diff < _thresholdHeight.max);
		if (!heightChanged && (newHeight.max == _animateToHeight.min)) {
			return;
		}
	}

	const auto newLinesData = ChartHorizontalLinesData(
		newHeight.max,
		newHeight.min,
		true);
	newHeight = Limits{
		.min = newLinesData.lines.front().absoluteValue,
		.max = newLinesData.lines.back().absoluteValue,
	};

	{
		auto k = (_currentHeight.max - _currentHeight.min)
			/ float64(newHeight.max - newHeight.min);
		if (k > 1.) {
			k = 1. / k;
		}
		constexpr auto kUpdateStep1 = 0.1;
		constexpr auto kUpdateStep2 = 0.03;
		constexpr auto kUpdateStep3 = 0.045;
		constexpr auto kUpdateStepThreshold1 = 0.7;
		constexpr auto kUpdateStepThreshold2 = 0.1;
		const auto s = (k > kUpdateStepThreshold1)
			? kUpdateStep1
			: (k < kUpdateStepThreshold2)
			? kUpdateStep2
			: kUpdateStep3;

		const auto refresh = (newHeight.max != _animateToHeight.max)
			|| (_useMinHeight && (newHeight.min != _animateToHeight.min));
		if (refresh) {
			_startFromH = _currentHeight;
			_startFrom = {};
			_minMaxUpdateStep = s;
		}
	}

	_animateToHeight = newHeight;
	measureHeightThreshold();

	{
		const auto now = crl::now();
		if ((now - _lastHeightLimitsChanged) < kHeightLimitsUpdateTimeout) {
			return;
		}
		_lastHeightLimitsChanged = now;
	}

	if (!animated) {
		_currentHeight = newHeight;
		_horizontalLines.clear();
		_horizontalLines.push_back(newLinesData);
		_horizontalLines.back().alpha = 1.;
		return;
	}
	for (auto &horizontalLine : _horizontalLines) {
		horizontalLine.fixedAlpha = horizontalLine.alpha;
	}
	_horizontalLines.push_back(newLinesData);
}

void ChartWidget::addHorizontalLine(Limits newHeight, bool animated) {
	const auto newLinesData = ChartHorizontalLinesData(
		newHeight.max,
		newHeight.min,
		true);
	for (auto &horizontalLine : _horizontalLines) {
		horizontalLine.fixedAlpha = horizontalLine.alpha;
	}
	_horizontalLines.push_back(newLinesData);
}

void ChartWidget::measureHeightThreshold() {
	const auto chartHeight = height();
	if (!_animateToHeight.max || !chartHeight) {
		return;
	}
	_thresholdHeight.max = (_animateToHeight.max / float64(chartHeight))
		* st::boxTextFont->height;
}

} // namespace Statistic
