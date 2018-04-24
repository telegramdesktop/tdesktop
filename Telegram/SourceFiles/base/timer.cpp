/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/timer.h"

namespace base {
namespace {

QObject *TimersAdjuster() {
	static QObject adjuster;
	return &adjuster;
}

} // namespace

Timer::Timer(
	not_null<QThread*> thread,
	base::lambda<void()> callback)
: Timer(std::move(callback)) {
	moveToThread(thread);
}


Timer::Timer(base::lambda<void()> callback)
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

void Timer::start(TimeMs timeout, Qt::TimerType type, Repeat repeat) {
	cancel();

	_type = type;
	setRepeat(repeat);
	_adjusted = false;
	setTimeout(timeout);
	_timerId = startTimer(_timeout, _type);
	if (_timerId) {
		_next = getms(true) + _timeout;
	} else {
		_next = 0;
	}
}

void Timer::cancel() {
	if (isActive()) {
		killTimer(base::take(_timerId));
	}
}

TimeMs Timer::remainingTime() const {
	if (!isActive()) {
		return -1;
	}
	auto now = getms(true);
	return (_next > now) ? (_next - now) : TimeMs(0);
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

void Timer::setTimeout(TimeMs timeout) {
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
			_next = getms(true) + _timeout;
		}
	} else {
		cancel();
	}

	if (_callback) {
		_callback();
	}
}

int DelayedCallTimer::call(
		TimeMs timeout,
		lambda_once<void()> callback,
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
