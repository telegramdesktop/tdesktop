/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/binary_guard.h"
#include <crl/crl_time.h>
#include <crl/crl_object_on_queue.h>
#include <QtCore/QThread>

namespace base {
namespace details {

class TimerObject;

class TimerObjectWrap {
public:
	explicit TimerObjectWrap(Fn<void()> adjust);
	~TimerObjectWrap();

	void call(
		crl::time_type timeout,
		Qt::TimerType type,
		FnMut<void()> method);
	void cancel();

private:
	void sendEvent(std::unique_ptr<QEvent> event);

	std::unique_ptr<TimerObject> _value;

};

} // namespace details

class ConcurrentTimerEnvironment {
public:
	ConcurrentTimerEnvironment();
	~ConcurrentTimerEnvironment();

	std::unique_ptr<details::TimerObject> createTimer(Fn<void()> adjust);

	static void Adjust();

private:
	void acquire();
	void release();
	void adjustTimers();

	QThread _thread;
	QObject _adjuster;

};

class ConcurrentTimer {
public:
	explicit ConcurrentTimer(
		Fn<void(FnMut<void()>)> runner,
		Fn<void()> callback = nullptr);

	template <typename Object>
	explicit ConcurrentTimer(
		crl::weak_on_queue<Object> weak,
		Fn<void()> callback = nullptr);

	static Qt::TimerType DefaultType(TimeMs timeout) {
		constexpr auto kThreshold = TimeMs(1000);
		return (timeout > kThreshold) ? Qt::CoarseTimer : Qt::PreciseTimer;
	}

	void setCallback(Fn<void()> callback) {
		_callback = std::move(callback);
	}

	void callOnce(TimeMs timeout) {
		callOnce(timeout, DefaultType(timeout));
	}

	void callEach(TimeMs timeout) {
		callEach(timeout, DefaultType(timeout));
	}

	void callOnce(TimeMs timeout, Qt::TimerType type) {
		start(timeout, type, Repeat::SingleShot);
	}

	void callEach(TimeMs timeout, Qt::TimerType type) {
		start(timeout, type, Repeat::Interval);
	}

	bool isActive() const {
		return _running.alive();
	}

	void cancel();
	TimeMs remainingTime() const;

private:
	enum class Repeat : unsigned {
		Interval = 0,
		SingleShot = 1,
	};
	Fn<void()> createAdjuster();
	void start(TimeMs timeout, Qt::TimerType type, Repeat repeat);
	void adjust();

	void cancelAndSchedule(int timeout);

	void setTimeout(TimeMs timeout);
	int timeout() const;

	void timerEvent();

	void setRepeat(Repeat repeat) {
		_repeat = static_cast<unsigned>(repeat);
	}
	Repeat repeat() const {
		return static_cast<Repeat>(_repeat);
	}

	Fn<void(FnMut<void()>)> _runner;
	std::shared_ptr<bool> _guard; // Must be before _object.
	details::TimerObjectWrap _object;
	Fn<void()> _callback;
	base::binary_guard _running;
	TimeMs _next = 0;
	int _timeout = 0;

	Qt::TimerType _type : 2;
	bool _adjusted : 1;
	unsigned _repeat : 1;

};

template <typename Object>
ConcurrentTimer::ConcurrentTimer(
	crl::weak_on_queue<Object> weak,
	Fn<void()> callback)
: ConcurrentTimer(weak.runner(), std::move(callback)) {
}

} // namespace base
