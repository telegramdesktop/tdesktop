/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/timer.h"

#include <QtCore/QTimerEvent>

namespace base {
namespace {

QObject *TimersAdjuster() {
	static QObject adjuster;
	return &adjuster;
}

} // namespace

Timer::Timer(
	not_null<QThread*> thread,
	Fn<void()> callback)
: Timer(std::move(callback)) {
	moveToThread(thread);
}

Timer::Timer(Fn<void()> callback)
: QObject(nullptr)
, _callback(std::move(callback))
, _type(Qt::PreciseTimer)
, _adjusted(false) {
	setRepeat(Repeat::Interval);
	connect(
		TimersAdjuster(),
		&QObject::destroyed,
		this,
		[this] { adjust(); },
		Qt::QueuedConnection);
}

void Timer::start(crl::time timeout, Qt::TimerType type, Repeat repeat) {
	cancel();

	_type = type;
	setRepeat(repeat);
	_adjusted = false;
	setTimeout(timeout);
	_timerId = startTimer(_timeout, _type);
	if (_timerId) {
		_next = crl::now() + _timeout;
	} else {
		_next = 0;
	}
}

void Timer::cancel() {
	if (isActive()) {
		killTimer(base::take(_timerId));
	}
}

crl::time Timer::remainingTime() const {
	if (!isActive()) {
		return -1;
	}
	const auto now = crl::now();
	return (_next > now) ? (_next - now) : crl::time(0);
}

void Timer::Adjust() {
	QObject emitter;
	connect(
		&emitter,
		&QObject::destroyed,
		TimersAdjuster(),
		&QObject::destroyed);
}

void Timer::adjust() {
	auto remaining = remainingTime();
	if (remaining >= 0) {
		cancel();
		_timerId = startTimer(remaining, _type);
		_adjusted = true;
	}
}

void Timer::setTimeout(crl::time timeout) {
	Expects(timeout >= 0 && timeout <= std::numeric_limits<int>::max());

	_timeout = static_cast<unsigned int>(timeout);
}

int Timer::timeout() const {
	return _timeout;
}

void Timer::timerEvent(QTimerEvent *e) {
	if (repeat() == Repeat::Interval) {
		if (_adjusted) {
			start(_timeout, _type, repeat());
		} else {
			_next = crl::now() + _timeout;
		}
	} else {
		cancel();
	}

	if (const auto onstack = _callback) {
		onstack();
	}
}

int DelayedCallTimer::call(
		crl::time timeout,
		FnMut<void()> callback,
		Qt::TimerType type) {
	Expects(timeout >= 0);

	if (!callback) {
		return 0;
	}
	auto timerId = startTimer(static_cast<int>(timeout), type);
	if (timerId) {
		_callbacks.emplace(timerId, std::move(callback));
	}
	return timerId;
}

void DelayedCallTimer::cancel(int callId) {
	if (callId) {
		killTimer(callId);
		_callbacks.remove(callId);
	}
}

void DelayedCallTimer::timerEvent(QTimerEvent *e) {
	auto timerId = e->timerId();
	killTimer(timerId);

	auto it = _callbacks.find(timerId);
	if (it != _callbacks.end()) {
		auto callback = std::move(it->second);
		_callbacks.erase(it);

		callback();
	}
}

} // namespace base
