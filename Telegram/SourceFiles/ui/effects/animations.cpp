/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/animations.h"

#include "core/application.h"
#include "core/sandbox.h"

namespace Ui {
namespace Animations {
namespace {

constexpr auto kAnimationTick = crl::time(1000) / 60;
constexpr auto kIgnoreUpdatesTimeout = crl::time(4);

} // namespace

void Basic::start() {
	if (animating()) {
		restart();
	} else {
		Core::App().animationManager().start(this);
	}
}

void Basic::stop() {
	if (animating()) {
		Core::App().animationManager().stop(this);
	}
}

void Basic::restart() {
	Expects(_started >= 0);

	_started = crl::now();
}

void Basic::markStarted() {
	Expects(_started < 0);

	_started = crl::now();
}

void Basic::markStopped() {
	Expects(_started >= 0);

	_started = -1;
}

Manager::Manager() {
	Core::Sandbox::Instance().widgetUpdateRequests(
	/*) | rpl::filter([=] {
		return (_lastUpdateTime + kIgnoreUpdatesTimeout < crl::now());
	}*/) | rpl::start_with_next([=] {
		update(UpdateSource::RepaintRequest);
	}, _lifetime);
}

void Manager::start(not_null<Basic*> animation) {
	_forceUpdateProcessing = true;
	if (_updating) {
		_starting.emplace_back(animation.get());
		return;
	}
	_active.emplace_back(animation.get());
	startTimer();
	updateQueued();
}

void Manager::stop(not_null<Basic*> animation) {
	if (empty(_active) && empty(_starting)) {
		return;
	}
	const auto value = animation.get();
	const auto proj = &ActiveBasicPointer::get;
	auto &list = _updating ? _starting : _active;
	list.erase(ranges::remove(list, value, proj), end(list));

	if (_updating) {
		const auto i = ranges::find(_active, value, proj);
		if (i != end(_active)) {
			*i = nullptr;
		}
	} else if (empty(_active)) {
		stopTimer();
	}
}

void Manager::update(UpdateSource source) {
	if (_active.empty() || _updating) {
		return;
	}
	const auto now = crl::now();
	if (_forceUpdateProcessing) {
		_forceUpdateProcessing = false;
	} else if (now < _lastUpdateTime + kIgnoreUpdatesTimeout) {
		return;
	}

	_updating = true;
	const auto guard = gsl::finally([&] { _updating = false; });

	_lastUpdateTime = now;
	const auto isFinished = [&](const ActiveBasicPointer &element) {
		return !element.call(now);
	};
	_active.erase(ranges::remove_if(_active, isFinished), end(_active));

	if (empty(_starting)) {
		if (empty(_active)) {
			stopTimer();
		}
		return;
	}
	_active.insert(
		end(_active),
		std::make_move_iterator(begin(_starting)),
		std::make_move_iterator(end(_starting)));
	_starting.clear();
	if (_forceUpdateProcessing) {
		updateQueued();
	}
}

void Manager::updateQueued() {
	if (_queued) {
		return;
	}
	_queued = true;
	crl::on_main(delayedCallGuard(), [=] {
		_queued = false;
		update(UpdateSource::Queued);
	});
}

not_null<const QObject*> Manager::delayedCallGuard() const {
	return static_cast<const QObject*>(this);
}

void Manager::startTimer() {
	if (_timerId) {
		return;
	}
	_timerId = QObject::startTimer(kAnimationTick, Qt::PreciseTimer);
}

void Manager::stopTimer() {
	if (!_timerId) {
		return;
	}
	killTimer(base::take(_timerId));
}

void Manager::timerEvent(QTimerEvent *e) {
	update(UpdateSource::TimerEvent);
}

} // namespace Animations
} // namespace Ui
