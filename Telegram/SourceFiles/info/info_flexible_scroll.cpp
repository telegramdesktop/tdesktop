/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_flexible_scroll.h"

#include "ui/effects/animation_value.h"
#include "ui/widgets/scroll_area.h"
#include "base/event_filter.h"
#include "base/options.h"
#include "styles/style_info.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QScrollBar>

namespace Info {

base::options::toggle AlternativeScrollProcessing({
	.id = kAlternativeScrollProcessing,
	.name = "Use legacy scroll processing in profiles.",
});

const char kAlternativeScrollProcessing[] = "alternative-scroll-processing";

FlexibleScrollHelper::FlexibleScrollHelper(
	not_null<Ui::ScrollArea*> scroll,
	not_null<Ui::RpWidget*> inner,
	not_null<Ui::RpWidget*> pinnedToTop,
	Fn<void(QMargins)> setPaintPadding,
	Fn<void(rpl::producer<not_null<QEvent*>>&&)> setViewport,
	FlexibleScrollData &data)
: _scroll(scroll)
, _inner(inner)
, _pinnedToTop(pinnedToTop)
, _setPaintPadding(setPaintPadding)
, _setViewport(setViewport)
, _data(data) {
	setupScrollAnimation();
	if (AlternativeScrollProcessing.value()) {
		setupScrollHandling();
	} else {
		setupScrollHandlingWithFilter();
	}
}

void FlexibleScrollHelper::setupScrollAnimation() {
	constexpr auto kScrollStepTime = crl::time(260);

	const auto clearScrollState = [=] {
		_scrollAnimation.stop();
		_scrollTopFrom = 0;
		_scrollTopTo = 0;
		_timeOffset = 0;
		_lastScrollApplied = 0;
	};

	_scrollAnimation.init([=](crl::time now) {
		const auto progress = float64(now
			- _scrollAnimation.started()
			- _timeOffset) / kScrollStepTime;
		const auto eased = anim::easeOutQuint(1.0, progress);
		const auto scrollCurrent = anim::interpolate(
			_scrollTopFrom,
			_scrollTopTo,
			std::clamp(eased, 0., 1.));
		scrollToY(scrollCurrent);
		_lastScrollApplied = scrollCurrent;
		if (progress >= 1) {
			clearScrollState();
		}
	});
}

void FlexibleScrollHelper::setupScrollHandling() {
	const auto heightDiff = [=] {
		return _pinnedToTop->maximumHeight()
			- _pinnedToTop->minimumHeight();
	};

	rpl::combine(
		_pinnedToTop->heightValue(),
		_inner->heightValue()
	) | rpl::on_next([=](int, int h) {
		_data.contentHeightValue.fire(h + heightDiff());
	}, _pinnedToTop->lifetime());

	const auto singleStep = _scroll->verticalScrollBar()->singleStep()
		* QApplication::wheelScrollLines();
	const auto step1 = (_pinnedToTop->maximumHeight()
			< st::infoProfileTopBarHeightMax)
		? (st::infoProfileTopBarStep2 + st::lineWidth)
		: st::infoProfileTopBarStep1;
	const auto step2 = st::infoProfileTopBarStep2;
	// const auto stepDepreciation = singleStep
	// 	- st::infoProfileTopBarActionButtonsHeight;
	_scrollTopPrevious = _scroll->scrollTop();

	_scroll->scrollTopValue(
	) | rpl::on_next([=](int top) {
		if (_applyingFakeScrollState) {
			return;
		}
		const auto diff = top - _scrollTopPrevious;
		if (std::abs(diff) == singleStep) {
			const auto previousValue = top - diff;
			const auto nextStep = (diff > 0)
				? ((previousValue == 0)
					? step1
					: (previousValue == step1)
					? step2
					: -1)
				// : ((top < step1
				// 	&& (top + stepDepreciation != step1
				// 		|| _scrollAnimation.animating()))
				: ((top < step1)
					? 0
					: (top < step2)
					? step1
					: -1);
			{
				_applyingFakeScrollState = true;
				scrollToY(previousValue);
				_applyingFakeScrollState = false;
			}
			if (_scrollAnimation.animating()
				&& ((_scrollTopTo > _scrollTopFrom) != (diff > 0))) {
				auto overriddenDirection = true;
				if (_scrollTopTo > _scrollTopFrom) {
					// From going down to going up.
					if (_scrollTopTo == step1) {
						_scrollTopTo = 0;
					} else if (_scrollTopTo == step2) {
						_scrollTopTo = step1;
					} else {
						overriddenDirection = false;
					}
				} else {
					// From going up to going down.
					if (_scrollTopTo == 0) {
						_scrollTopTo = step1;
					} else if (_scrollTopTo == step1) {
						_scrollTopTo = step2;
					} else {
						overriddenDirection = false;
					}
				}
				if (overriddenDirection) {
					_timeOffset = crl::now() - _scrollAnimation.started();
					_scrollTopFrom = _lastScrollApplied
						? _lastScrollApplied
						: previousValue;
					return;
				} else {
					_scrollAnimation.stop();
					_scrollTopFrom = 0;
					_scrollTopTo = 0;
					_timeOffset = 0;
					_lastScrollApplied = 0;
				}
			}
			_scrollTopFrom = _lastScrollApplied
				? _lastScrollApplied
				: previousValue;
			if (!_scrollAnimation.animating()) {
				_scrollTopTo = ((nextStep != -1) ? nextStep : top);
				_scrollAnimation.start();
			} else {
				if (_scrollTopTo > _scrollTopFrom) {
					// Down.
					if (_scrollTopTo == step1) {
						_scrollTopTo = step2;
					} else {
						_scrollTopTo += diff;
					}
				} else {
					// Up.
					if (_scrollTopTo == step2) {
						_scrollTopTo = step1;
					} else if (_scrollTopTo == step1) {
						_scrollTopTo = 0;
					} else {
						_scrollTopTo += diff;
					}
				}
				_timeOffset = (crl::now() - _scrollAnimation.started());
			}
			return;
		}
		_scrollTopPrevious = top;
		const auto current = heightDiff() - top;
		_inner->moveToLeft(0, std::min(0, current));
		_pinnedToTop->resize(
			_pinnedToTop->width(),
			std::max(current + _pinnedToTop->minimumHeight(), 0));
	}, _inner->lifetime());

	_data.fillerWidthValue.events(
	) | rpl::on_next([=](int w) {
		_inner->resizeToWidth(w);
	}, _inner->lifetime());

	_setPaintPadding({ 0, _pinnedToTop->minimumHeight(), 0, 0 });
	_setViewport(_pinnedToTop->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::Wheel;
	}));
}

void FlexibleScrollHelper::setupScrollHandlingWithFilter() {
	rpl::combine(
		_pinnedToTop->heightValue(),
		_inner->heightValue()
	) | rpl::on_next([=](int, int h) {
		const auto max = _pinnedToTop->maximumHeight();
		const auto min = _pinnedToTop->minimumHeight();
		const auto diff = max - min;
		const auto progress = (diff > 0)
			? std::clamp(
				(_pinnedToTop->height() - min) / float64(diff),
				0.,
				1.)
			: 1.;
		_data.contentHeightValue.fire(h
			+ anim::interpolate(diff, 0, progress));
	}, _pinnedToTop->lifetime());

	const auto singleStep = _scroll->verticalScrollBar()->singleStep()
		* QApplication::wheelScrollLines();
	const auto step1 = (_pinnedToTop->maximumHeight()
			< st::infoProfileTopBarHeightMax)
		? (st::infoProfileTopBarStep2 + st::lineWidth)
		: st::infoProfileTopBarStep1;
	const auto step2 = st::infoProfileTopBarStep2;

	base::install_event_filter(_scroll->verticalScrollBar(), [=](
			not_null<QEvent*> e) {
		if (e->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		const auto wheel = static_cast<QWheelEvent*>(e.get());
		const auto delta = wheel->angleDelta().y();
		if (std::abs(delta) != 120) {
			scrollToY(_scroll->scrollTop() - delta);
			return base::EventFilterResult::Cancel;
		}
		const auto actualTop = _scroll->scrollTop();
		const auto animationActive = _scrollAnimation.animating()
			&& (_lastScrollApplied != _scrollTopTo);
		const auto top = animationActive
			? (_lastScrollApplied ? _lastScrollApplied : actualTop)
			: actualTop;
		const auto diff = (delta > 0) ? -singleStep : singleStep;
		const auto previousValue = top;
		const auto targetTop = top + diff;
		const auto nextStep = (diff > 0)
			? ((previousValue == 0)
				? step1
				: (previousValue == step1)
				? step2
				: -1)
			: ((targetTop < step1)
				? 0
				: (targetTop < step2)
				? step1
				: -1);
		if (animationActive
			&& ((_scrollTopTo > _scrollTopFrom) != (diff > 0))) {
			auto overriddenDirection = true;
			if (_scrollTopTo > _scrollTopFrom) {
				if (_scrollTopTo == step1) {
					_scrollTopTo = 0;
				} else if (_scrollTopTo == step2) {
					_scrollTopTo = step1;
				} else {
					overriddenDirection = false;
				}
			} else {
				if (_scrollTopTo == 0) {
					_scrollTopTo = step1;
				} else if (_scrollTopTo == step1) {
					_scrollTopTo = step2;
				} else {
					overriddenDirection = false;
				}
			}
			if (overriddenDirection) {
				_timeOffset = crl::now() - _scrollAnimation.started();
				_scrollTopFrom = _lastScrollApplied
					? _lastScrollApplied
					: top;
				return base::EventFilterResult::Cancel;
			} else {
				_scrollAnimation.stop();
				_scrollTopFrom = 0;
				_scrollTopTo = 0;
				_timeOffset = 0;
				_lastScrollApplied = 0;
			}
		}
		_scrollTopFrom = top;
		if (!animationActive) {
			_scrollTopTo = (nextStep != -1) ? nextStep : targetTop;
			_scrollAnimation.start();
		} else {
			if (_scrollTopTo > _scrollTopFrom) {
				if (_scrollTopTo == step1) {
					_scrollTopTo = step2;
				} else {
					_scrollTopTo += diff;
				}
			} else {
				if (_scrollTopTo == step2) {
					_scrollTopTo = step1;
				} else if (_scrollTopTo == step1) {
					_scrollTopTo = 0;
				} else {
					_scrollTopTo += diff;
				}
			}
			_timeOffset = crl::now() - _scrollAnimation.started();
		}
		return base::EventFilterResult::Cancel;
	}, _filterLifetime);

	_scroll->scrollTopValue() | rpl::on_next([=](int top) {
		applyScrollToPinnedLayout(top);
	}, _inner->lifetime());

	_data.fillerWidthValue.events(
	) | rpl::on_next([=](int w) {
		_inner->resizeToWidth(w);
	}, _inner->lifetime());

	_setPaintPadding({ 0, _pinnedToTop->minimumHeight(), 0, 0 });
	_setViewport(_pinnedToTop->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::Wheel;
	}));
}

void FlexibleScrollHelper::scrollToY(int scrollCurrent) {
	applyScrollToPinnedLayout(scrollCurrent);
	_scroll->scrollToY(scrollCurrent);
}

void FlexibleScrollHelper::applyScrollToPinnedLayout(int scrollCurrent) {
	const auto top = std::min(scrollCurrent, _scroll->scrollTopMax());
	const auto minimumHeight = _pinnedToTop->minimumHeight();
	const auto current = _pinnedToTop->maximumHeight()
		- minimumHeight
		- top;
	_inner->moveToLeft(0, std::min(0, current));
	_pinnedToTop->resize(
		_pinnedToTop->width(),
		std::max(current + minimumHeight, 0));
}

} // namespace Info
