/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/selecting_scroll.h"

#include "base/event_filter.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>

namespace Ui {
namespace {

constexpr auto kSpeedInterval = crl::time(15);
constexpr auto kMaxSpeed = 37;
constexpr auto kMaxFrameInterval = crl::time(64);

[[nodiscard]] bool LeftButtonStillDown() {
	return (QGuiApplication::mouseButtons() & Qt::LeftButton) != 0;
}

[[nodiscard]] int DeltaOutside(QRect visible, int y) {
	return visible.isEmpty()
		? 0
		: (y < visible.top())
		? (y - visible.top())
		: (y > visible.bottom())
		? (y - visible.bottom())
		: 0;
}

[[nodiscard]] float64 SpeedForDelta(int delta) {
	return (delta > 0)
		? std::min(delta * 3 / 20 + 1, kMaxSpeed)
		: std::max(delta * 3 / 20 - 1, -kMaxSpeed);
}

} // namespace

void SetupSelectingScroll(
		not_null<RpWidget*> widget,
		Fn<void(int pixels)> scrollBy) {
	Expects(scrollBy != nullptr);

	struct State {
		Animations::Basic animation;
		crl::time lastFrame = 0;
		float64 accumulated = 0.;
		int delta = 0;
	};
	const auto state = widget->lifetime().make_state<State>();
	const auto stop = [=] {
		state->delta = 0;
		state->accumulated = 0.;
		state->animation.stop();
	};
	state->animation.init([=](crl::time now) {
		if (!state->delta || !LeftButtonStillDown()) {
			stop();
			return false;
		}
		const auto elapsed = std::clamp(
			now - state->lastFrame,
			crl::time(0),
			kMaxFrameInterval);
		state->lastFrame = now;
		state->accumulated += SpeedForDelta(state->delta)
			* float64(elapsed)
			/ kSpeedInterval;
		if (const auto pixels = int(state->accumulated)) {
			state->accumulated -= pixels;
			scrollBy(pixels);
		}
		return true;
	});
	const auto check = [=](int delta) {
		state->delta = delta;
		if (!delta) {
			stop();
		} else if (!state->animation.animating()) {
			state->lastFrame = crl::now();
			state->animation.start();
		}
	};

	base::install_event_filter(widget, [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::MouseButtonRelease || type == QEvent::Hide) {
			stop();
		} else if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			check((mouse->buttons() & Qt::LeftButton)
				? DeltaOutside(
					widget->visibleRegion().boundingRect(),
					mouse->pos().y())
				: 0);
		}
		return base::EventFilterResult::Continue;
	});
}

} // namespace Ui
