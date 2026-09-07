/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/smooth_scroll.h"

#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/scroll_area.h"
#include "ui/ui_utility.h"

#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QScrollBar>

namespace Ui {
namespace {

constexpr auto kDuration = crl::time(260);

struct State {
	Animations::Basic animation;
	crl::time started = 0;
	int from = 0;
	int target = 0;
	int applied = 0;
	bool tracking = false;
};

[[nodiscard]] base::flat_set<QObject*> &Attached() {
	static auto result = base::flat_set<QObject*>();
	return result;
}

[[nodiscard]] base::flat_set<QObject*> &Skipped() {
	static auto result = base::flat_set<QObject*>();
	return result;
}

[[nodiscard]] bool Register(not_null<QWidget*> scroll) {
	const auto object = static_cast<QObject*>(scroll.get());
	if (Skipped().contains(object) || !Attached().emplace(object).second) {
		return false;
	}
	QObject::connect(object, &QObject::destroyed, [](QObject *dead) {
		Attached().remove(dead);
	});
	return true;
}

[[nodiscard]] bool DiscreteWheel(not_null<QWheelEvent*> e) {
	// A trackpad, a touch screen and a momentum tail all arrive as a
	// phased stream of small deltas that is already smooth, and would
	// only be made worse by animating on top of it. A classic wheel
	// notch is the one input that arrives without a phase. Modifiers
	// are left alone as well: the scrolls multiply a notch to a page.
	if (e->phase() != Qt::NoScrollPhase) {
		return false;
	} else if (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
		return false;
	}
	const auto delta = e->angleDelta();
	return std::abs(delta.y()) >= 120
		&& std::abs(delta.y()) > std::abs(delta.x());
}

template <typename Scroll>
void InitAnimation(not_null<State*> state, not_null<Scroll*> scroll) {
	state->animation.init([=](crl::time now) {
		// Anything that moved the scroll while we were animating it -
		// a jump to a message, the keyboard, a geometry update after
		// more history was loaded - owns the position now, and wins.
		if (state->tracking && scroll->scrollTop() != state->applied) {
			state->animation.stop();
			state->tracking = false;
			return;
		}
		const auto progress = std::clamp(
			(now - state->started) / float64(kDuration),
			0.,
			1.);
		scroll->scrollToY(anim::interpolate(
			state->from,
			state->target,
			anim::easeOutQuint(1., progress)));
		state->applied = scroll->scrollTop();
		state->tracking = true;
		if (progress >= 1.) {
			state->animation.stop();
			state->tracking = false;
		}
	});
}

template <typename Scroll>
[[nodiscard]] bool ProcessWheel(
		not_null<State*> state,
		not_null<Scroll*> scroll,
		not_null<QWheelEvent*> e,
		// Already points where the user wants to go: a natural-scrolling
		// setting is applied to the angle delta before it reaches us, and
		// inverted() only reports that setting. Every other scroll in the
		// app takes the delta as it comes, so this one does too.
		int delta) {
	const auto giveUp = [&] {
		state->animation.stop();
		state->tracking = false;
		return false;
	};
	if (anim::Disabled() || !DiscreteWheel(e)) {
		return giveUp();
	}
	const auto max = scroll->scrollTopMax();
	if (max <= 0 || !delta) {
		return giveUp();
	}
	const auto animating = state->animation.animating();
	const auto base = animating ? state->target : scroll->scrollTop();
	const auto target = base + delta;
	if (target < 0 || target > max) {
		// The edges belong to the scroll itself: that is where the
		// elastic overscroll is stretched and where a chat is asked
		// for more content to append below the last message.
		if (animating) {
			// A flight in progress leaves the scroll on an
			// intermediate frame, so give it the position that
			// flight was travelling to. The notch is handed back
			// to the scroll below and would otherwise start from
			// that frame and stop short of the edge.
			scroll->scrollToY(base);
		}
		return giveUp();
	}
	state->from = scroll->scrollTop();
	state->target = target;
	state->started = crl::now();
	state->tracking = false;
	if (!animating) {
		state->animation.start();
	}
	return true;
}

} // namespace

void InstallSmoothScroll(
		not_null<ScrollArea*> scroll,
		Fn<bool(not_null<QWheelEvent*>)> before) {
	if (!Register(scroll)) {
		return;
	}
	const auto state = scroll->lifetime().make_state<State>();
	InitAnimation(state, scroll);
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		if (before && before(e)) {
			return true;
		}
		const auto single = scroll->verticalScrollBar()->singleStep()
			* QApplication::wheelScrollLines();
		const auto delta = -e->angleDelta().y() * single / 120;
		return ProcessWheel(state, scroll, e, delta);
	});
}

void InstallSmoothScroll(
		not_null<ElasticScroll*> scroll,
		Fn<bool(not_null<QWheelEvent*>)> before) {
	if (!Register(scroll)) {
		return;
	}
	const auto state = scroll->lifetime().make_state<State>();
	InitAnimation(state, scroll);
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		if (before && before(e)) {
			return true;
		}
		return ProcessWheel(state, scroll, e, -ScrollDelta(e).y());
	});
}

void DisableSmoothScroll(not_null<QWidget*> scroll) {
	const auto object = static_cast<QObject*>(scroll.get());
	Skipped().emplace(object);
	QObject::connect(object, &QObject::destroyed, [](QObject *dead) {
		Skipped().remove(dead);
	});
}

void EnsureSmoothScroll(not_null<QObject*> wheelTarget) {
	if (!wheelTarget->isWidgetType()) {
		return;
	}
	auto widget = static_cast<QWidget*>(wheelTarget.get());
	while (widget) {
		if (const auto area = dynamic_cast<ScrollArea*>(widget)) {
			InstallSmoothScroll(area);
			return;
		} else if (const auto elastic = dynamic_cast<ElasticScroll*>(widget)) {
			InstallSmoothScroll(elastic);
			return;
		}
		widget = widget->parentWidget();
	}
}

} // namespace Ui
