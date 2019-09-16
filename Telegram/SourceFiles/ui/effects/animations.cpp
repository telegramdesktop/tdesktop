/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/animations.h"

#include "ui/ui_utility.h"
#include "base/invoke_queued.h"

#include <QtCore/QPointer>

#include <crl/crl_on_main.h>
#include <crl/crl.h>
#include <rpl/filter.h>
#include <range/v3/algorithm/remove_if.hpp>
#include <range/v3/algorithm/remove.hpp>
#include <range/v3/algorithm/find.hpp>

namespace Ui {
namespace Animations {
namespace {

constexpr auto kAnimationTick = crl::time(1000) / 120;
constexpr auto kIgnoreUpdatesTimeout = crl::time(4);

Manager *ManagerInstance = nullptr;

} // namespace

void Basic::start() {
	Expects(ManagerInstance != nullptr);

	if (animating()) {
		restart();
	} else {
		ManagerInstance->start(this);
	}
}

void Basic::stop() {
	Expects(ManagerInstance != nullptr);

	if (animating()) {
		ManagerInstance->stop(this);
	}
}

void Basic::restart() {
	Expects(_started >= 0);

	_started = crl::now();

	Ensures(_started >= 0);
}

void Basic::markStarted() {
	Expects(_started < 0);

	_started = crl::now();

	Ensures(_started >= 0);
}

void Basic::markStopped() {
	Expects(_started >= 0);

	_started = -1;
}

Manager::Manager() {
	Expects(ManagerInstance == nullptr);

	ManagerInstance = this;

	crl::on_main_update_requests(
	) | rpl::filter([=] {
		return (_lastUpdateTime + kIgnoreUpdatesTimeout < crl::now());
	}) | rpl::start_with_next([=] {
		update();
	}, _lifetime);
}

Manager::~Manager() {
	Expects(ManagerInstance == this);
	Expects(_active.empty());
	Expects(_starting.empty());

	ManagerInstance = nullptr;
}

void Manager::start(not_null<Basic*> animation) {
	_forceImmediateUpdate = true;
	if (_updating) {
		_starting.emplace_back(animation.get());
	} else {
		schedule();
		_active.emplace_back(animation.get());
	}
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

void Manager::update() {
	if (_active.empty() || _updating || _scheduled) {
		return;
	}
	const auto now = crl::now();
	if (_forceImmediateUpdate) {
		_forceImmediateUpdate = false;
	}
	schedule();

	_updating = true;
	const auto guard = gsl::finally([&] { _updating = false; });

	_lastUpdateTime = now;
	const auto isFinished = [&](const ActiveBasicPointer &element) {
		return !element.call(now);
	};
	_active.erase(ranges::remove_if(_active, isFinished), end(_active));

	if (!empty(_starting)) {
		_active.insert(
			end(_active),
			std::make_move_iterator(begin(_starting)),
			std::make_move_iterator(end(_starting)));
		_starting.clear();
	}
}

void Manager::updateQueued() {
	Expects(_timerId == 0);

	_timerId = -1;
	InvokeQueued(delayedCallGuard(), [=] {
		Expects(_timerId < 0);

		_timerId = 0;
		update();
	});
}

void Manager::schedule() {
	if (_scheduled || _timerId < 0) {
		return;
	}
	stopTimer();

	_scheduled = true;
	PostponeCall(delayedCallGuard(), [=] {
		_scheduled = false;
		if (_forceImmediateUpdate) {
			_forceImmediateUpdate = false;
			updateQueued();
		} else {
			const auto next = _lastUpdateTime + kAnimationTick;
			const auto now = crl::now();
			if (now < next) {
				_timerId = startTimer(next - now, Qt::PreciseTimer);
			} else {
				updateQueued();
			}
		}
	});
}

not_null<const QObject*> Manager::delayedCallGuard() const {
	return static_cast<const QObject*>(this);
}

void Manager::stopTimer() {
	if (_timerId > 0) {
		killTimer(base::take(_timerId));
	}
}

void Manager::timerEvent(QTimerEvent *e) {
	update();
}

} // namespace Animations
} // namespace Ui
