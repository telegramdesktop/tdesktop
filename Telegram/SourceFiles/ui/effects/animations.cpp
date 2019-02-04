/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/animations.h"

#include "core/application.h"

namespace Ui {
namespace Animations {
namespace {

constexpr auto kAnimationTimeout = crl::time(1000) / 60;
constexpr auto kIgnoreUpdatesTimeout = crl::time(4);

} // namespace

void Basic::start() {
	if (!animating()) {
		Core::App().animationManager().start(this);
	}
	_started = crl::now();
}

void Basic::stop() {
	if (animating()) {
		Core::App().animationManager().stop(this);
		_started = -1;
	}
}

void Manager::start(not_null<Basic*> animation) {
	if (_updating) {
		_starting.push_back(animation);
	} else {
		if (empty(_active)) {
			updateQueued();
		}
		_active.push_back(animation);
	}
}

void Manager::stop(not_null<Basic*> animation) {
	if (empty(_active)) {
		return;
	}
	if (_updating) {
		const auto i = ranges::find(_active, animation.get());
		if (i != end(_active)) {
			*i = nullptr;
		}
		return;
	}
	_active.erase(ranges::remove(_active, animation.get()), end(_active));
	if (empty(_active)) {
		stopTimer();
	}
}

void Manager::update() {
	if (_active.empty() || _updating || _scheduled) {
		return;
	}
	const auto now = crl::now();
	if (_lastUpdateTime + kIgnoreUpdatesTimeout >= now) {
		return;
	}
	schedule();

	_updating = true;
	const auto guard = gsl::finally([&] { _updating = false; });

	_lastUpdateTime = now;
	_active.erase(ranges::remove_if(_active, [&](Basic *element) {
		return !element || !element->call(now);
	}), end(_active));

	if (!empty(_starting)) {
		auto starting = std::move(_starting);
		_active.insert(end(_active), begin(starting), end(starting));
	}
}

void Manager::updateQueued() {
	InvokeQueued(this, [=] { update(); });
}

void Manager::schedule() {
	if (_scheduled) {
		return;
	}
	stopTimer();

	_scheduled = true;
	Ui::PostponeCall([=] {
		_scheduled = false;

		const auto next = _lastUpdateTime + kAnimationTimeout;
		const auto now = crl::now();
		if (now < next) {
			_timerId = startTimer(next - now, Qt::PreciseTimer);
		} else {
			updateQueued();
		}
	});
}

void Manager::stopTimer() {
	if (_timerId) {
		killTimer(base::take(_timerId));
	}
}

void Manager::timerEvent(QTimerEvent *e) {
	update();
}

} // namespace Animations
} // namespace Ui
