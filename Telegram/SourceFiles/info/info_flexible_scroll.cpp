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
namespace {

base::options::toggle ClassicProfileScroll({
	.id = kClassicProfileScroll,
	.name = "Use classic profile scroll processing.",
	.description = "Reverts the profile cover scroll to the previous "
		"(filler-based) implementation.",
	.restartRequired = true,
});

constexpr auto kScrollStepTime = crl::time(260);

} // namespace

const char kClassicProfileScroll[] = "classic-profile-scroll";

bool UseClassicProfileScroll() {
	return ClassicProfileScroll.value();
}

void SetupFlexibleRegularScroll(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::RpWidget*> inner,
		not_null<Ui::RpWidget*> pinnedToTop,
		Fn<void(int)> setScrollTopSkip,
		Fn<void(int)> setInnerTopReserve,
		Fn<void(QMargins)> setPaintPadding,
		Fn<void(rpl::producer<not_null<QEvent*>>)> setViewport,
		bool abortSnapOnExternalScroll) {
	const auto min = pinnedToTop->minimumHeight();
	const auto max = pinnedToTop->maximumHeight();

	setScrollTopSkip(min);
	setInnerTopReserve(max - min);
	setPaintPadding({ 0, min, 0, 0 });
	setViewport(pinnedToTop->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::Wheel;
	}));

	inner->widthValue(
	) | rpl::on_next([=](int w) {
		pinnedToTop->resize(w, pinnedToTop->height());
	}, pinnedToTop->lifetime());

	const auto applyTop = [=](int top) {
		const auto height = std::clamp(max - top, min, max);
		if (pinnedToTop->height() != height) {
			pinnedToTop->resize(pinnedToTop->width(), height);
		}
		scroll->setVerticalBarTopSkip(height - min);
	};
	scroll->scrollTopValue(
	) | rpl::on_next(applyTop, pinnedToTop->lifetime());
	applyTop(scroll->scrollTop());

	struct State {
		Ui::Animations::Basic animation;
		int fromTop = 0;
		int targetTop = 0;
		int lastApplied = -1;
		crl::time startTime = 0;
	};
	const auto state = scroll->lifetime().make_state<State>();
	const auto single = scroll->verticalScrollBar()->singleStep()
		* QApplication::wheelScrollLines();
	const auto step2 = st::infoProfileTopBarStep2;
	const auto step1 = (max < st::infoProfileTopBarHeightMax)
		? (step2 + st::lineWidth)
		: st::infoProfileTopBarStep1;

	state->animation.init([=](crl::time now) {
		if (abortSnapOnExternalScroll
			&& state->lastApplied >= 0
			&& scroll->scrollTop() != state->lastApplied) {
			state->animation.stop();
			state->lastApplied = -1;
			return;
		}
		const auto progress = std::clamp(
			(now - state->startTime) / float64(kScrollStepTime),
			0.,
			1.);
		const auto value = anim::interpolate(
			state->fromTop,
			state->targetTop,
			anim::easeOutQuint(1., progress));
		scroll->scrollToY(value);
		if (abortSnapOnExternalScroll) {
			const auto actual = scroll->scrollTop();
			if (actual != std::clamp(value, 0, scroll->scrollTopMax())) {
				state->animation.stop();
				state->lastApplied = -1;
				return;
			}
			state->lastApplied = actual;
		}
		if (progress >= 1.) {
			state->animation.stop();
			state->lastApplied = -1;
		}
	});
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		const auto delta = e->angleDelta().y();
		if (std::abs(delta) != 120 || e->phase() != Qt::NoScrollPhase) {
			state->animation.stop();
			return false;
		}
		const auto base = state->animation.animating()
			? state->targetTop
			: scroll->scrollTop();
		const auto diff = (delta > 0) ? -single : single;
		const auto plain = base + diff;
		const auto anchor = (diff > 0)
			? ((base == 0)
				? step1
				: (base == step1)
				? step2
				: plain)
			: ((plain < step1)
				? 0
				: (plain < step2)
				? step1
				: plain);
		state->targetTop = std::clamp(anchor, 0, scroll->scrollTopMax());
		state->fromTop = scroll->scrollTop();
		state->startTime = crl::now();
		state->lastApplied = -1;
		if (!state->animation.animating()) {
			state->animation.start();
		}
		return true;
	});
}

FlexibleScrollHelper::FlexibleScrollHelper(
	not_null<Ui::ScrollArea*> scroll,
	not_null<Ui::RpWidget*> inner,
	not_null<Ui::RpWidget*> pinnedToTop,
	Fn<void(QMargins)> setPaintPadding,
	Fn<void(rpl::producer<not_null<QEvent*>>&&)> setViewport,
	FlexibleScrollData &data,
	bool abortSnapOnExternalScroll)
: _scroll(scroll)
, _inner(inner)
, _pinnedToTop(pinnedToTop)
, _setPaintPadding(setPaintPadding)
, _setViewport(setViewport)
, _data(data)
, _abortSnapOnExternalScroll(abortSnapOnExternalScroll) {
	setupScrollAnimation();
	setupScrollHandling();
}

void FlexibleScrollHelper::setupScrollAnimation() {
	const auto clearScrollState = [=] {
		_scrollAnimation.stop();
		_scrollTopFrom = 0;
		_scrollTopTo = 0;
		_timeOffset = 0;
		_lastScrollApplied = 0;
		_lastScrollSeen = -1;
	};

	_scrollAnimation.init([=](crl::time now) {
		if (_abortSnapOnExternalScroll
			&& _lastScrollSeen >= 0
			&& _scroll->scrollTop() != _lastScrollSeen) {
			clearScrollState();
			return;
		}
		const auto progress = float64(now
			- _scrollAnimation.started()
			- _timeOffset) / kScrollStepTime;
		const auto eased = anim::easeOutQuint(1.0, progress);
		const auto scrollCurrent = anim::interpolate(
			_scrollTopFrom,
			_scrollTopTo,
			std::clamp(eased, 0., 1.));
		scrollToY(scrollCurrent);
		if (_abortSnapOnExternalScroll) {
			const auto actual = _scroll->scrollTop();
			if (actual
				!= std::clamp(scrollCurrent, 0, _scroll->scrollTopMax())) {
				clearScrollState();
				return;
			}
			_lastScrollSeen = actual;
		}
		_lastScrollApplied = scrollCurrent;
		if (progress >= 1) {
			clearScrollState();
		}
	});
}

void FlexibleScrollHelper::setupScrollHandling() {
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
		if (std::abs(delta) != 120 || (wheel->phase() != Qt::NoScrollPhase)) {
			if (_scrollAnimation.animating()) {
				_scrollAnimation.stop();
				_scrollTopFrom = 0;
				_scrollTopTo = 0;
				_timeOffset = 0;
				_lastScrollApplied = 0;
			}
			const auto pixels = wheel->pixelDelta().y();
			_scroll->scrollToY(_scroll->scrollTop()
				- (pixels ? pixels : delta));
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
			_lastScrollSeen = -1;
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
