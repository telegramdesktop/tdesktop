/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/concurrent_timer.h"

#include <QtCore/QThread>
#include <QtCore/QCoreApplication>

using namespace base::details;

namespace base {
namespace details {
namespace {

constexpr auto kCallDelayedEvent = QEvent::Type(QEvent::User + 1);
constexpr auto kCancelTimerEvent = QEvent::Type(QEvent::User + 2);
static_assert(kCancelTimerEvent < QEvent::MaxUser);

ConcurrentTimerEnvironment *Environment/* = nullptr*/;
QMutex EnvironmentMutex;

class CallDelayedEvent : public QEvent {
public:
	CallDelayedEvent(
		crl::time_type timeout,
		Qt::TimerType type,
		FnMut<void()> method);

	crl::time_type timeout() const;
	Qt::TimerType type() const;
	FnMut<void()> takeMethod();

private:
	crl::time_type _timeout = 0;
	Qt::TimerType _type = Qt::PreciseTimer;
	FnMut<void()> _method;

};

class CancelTimerEvent : public QEvent {
public:
	CancelTimerEvent();

};

CallDelayedEvent::CallDelayedEvent(
	crl::time_type timeout,
	Qt::TimerType type,
	FnMut<void()> method)
: QEvent(kCallDelayedEvent)
, _timeout(timeout)
, _type(type)
, _method(std::move(method)) {
	Expects(_timeout >= 0 && _timeout < std::numeric_limits<int>::max());
}

crl::time_type CallDelayedEvent::timeout() const {
	return _timeout;
}

Qt::TimerType CallDelayedEvent::type() const {
	return _type;
}

FnMut<void()> CallDelayedEvent::takeMethod() {
	return base::take(_method);
}

CancelTimerEvent::CancelTimerEvent() : QEvent(kCancelTimerEvent) {
}

} // namespace

class TimerObject : public QObject {
public:
	TimerObject(
		not_null<QThread*> thread,
		not_null<QObject*> adjuster,
		Fn<void()> adjust);

protected:
	bool event(QEvent *e) override;

private:
	void callDelayed(not_null<CallDelayedEvent*> e);
	void callNow();
	void cancel();
	void adjust();

	FnMut<void()> _next;
	Fn<void()> _adjust;
	int _timerId = 0;

};

TimerObject::TimerObject(
		not_null<QThread*> thread,
		not_null<QObject*> adjuster,
		Fn<void()> adjust)
: _adjust(std::move(adjust)) {
	moveToThread(thread);
	connect(
		adjuster,
		&QObject::destroyed,
		this,
		&TimerObject::adjust,
		Qt::DirectConnection);
}

bool TimerObject::event(QEvent *e) {
	const auto type = e->type();
	switch (type) {
	case kCallDelayedEvent:
		callDelayed(static_cast<CallDelayedEvent*>(e));
		return true;
	case kCancelTimerEvent:
		cancel();
		return true;
	case QEvent::Timer:
		callNow();
		return true;
	}
	return QObject::event(e);
}

void TimerObject::callDelayed(not_null<CallDelayedEvent*> e) {
	cancel();

	const auto timeout = e->timeout();
	const auto type = e->type();
	_next = e->takeMethod();
	if (timeout > 0) {
		_timerId = startTimer(timeout, type);
	} else {
		base::take(_next)();
	}
}

void TimerObject::cancel() {
	if (const auto id = base::take(_timerId)) {
		killTimer(id);
	}
	_next = nullptr;
}

void TimerObject::callNow() {
	auto next = base::take(_next);
	cancel();
	next();
}

void TimerObject::adjust() {
	if (_adjust) {
		_adjust();
	}
}

TimerObjectWrap::TimerObjectWrap(Fn<void()> adjust) {
	QMutexLocker lock(&EnvironmentMutex);

	if (Environment) {
		_value = Environment->createTimer(std::move(adjust));
	}
}

TimerObjectWrap::~TimerObjectWrap() {
	if (_value) {
		QMutexLocker lock(&EnvironmentMutex);

		if (Environment) {
			_value.release()->deleteLater();
		}
	}
}

void TimerObjectWrap::call(
		crl::time_type timeout,
		Qt::TimerType type,
		FnMut<void()> method) {
	sendEvent(std::make_unique<CallDelayedEvent>(
		timeout,
		type,
		std::move(method)));
}

void TimerObjectWrap::cancel() {
	sendEvent(std::make_unique<CancelTimerEvent>());
}

void TimerObjectWrap::sendEvent(std::unique_ptr<QEvent> event) {
	if (!_value) {
		return;
	}
	QCoreApplication::postEvent(
		_value.get(),
		event.release(),
		Qt::HighEventPriority);
}

} // namespace details

ConcurrentTimerEnvironment::ConcurrentTimerEnvironment() {
	_thread.start();
	_adjuster.moveToThread(&_thread);

	acquire();
}

ConcurrentTimerEnvironment::~ConcurrentTimerEnvironment() {
	_thread.quit();
	release();
	_thread.wait();
	QObject::disconnect(&_adjuster, &QObject::destroyed, nullptr, nullptr);
}

std::unique_ptr<TimerObject> ConcurrentTimerEnvironment::createTimer(
		Fn<void()> adjust) {
	return std::make_unique<TimerObject>(
		&_thread,
		&_adjuster,
		std::move(adjust));
}

void ConcurrentTimerEnvironment::Adjust() {
	QMutexLocker lock(&EnvironmentMutex);
	if (Environment) {
		Environment->adjustTimers();
	}
}

void ConcurrentTimerEnvironment::adjustTimers() {
	QObject emitter;
	QObject::connect(
		&emitter,
		&QObject::destroyed,
		&_adjuster,
		&QObject::destroyed,
		Qt::QueuedConnection);
}

void ConcurrentTimerEnvironment::acquire() {
	Expects(Environment == nullptr);

	QMutexLocker lock(&EnvironmentMutex);
	Environment = this;
}

void ConcurrentTimerEnvironment::release() {
	Expects(Environment == this);

	QMutexLocker lock(&EnvironmentMutex);
	Environment = nullptr;
}

ConcurrentTimer::ConcurrentTimer(
	Fn<void(FnMut<void()>)> runner,
	Fn<void()> callback)
: _runner(std::move(runner))
, _object(createAdjuster())
, _callback(std::move(callback))
, _type(Qt::PreciseTimer)
, _adjusted(false) {
	setRepeat(Repeat::Interval);
}

Fn<void()> ConcurrentTimer::createAdjuster() {
	auto guards = base::make_binary_guard();
	_guard = std::make_shared<bool>(true);
	return [=, runner = _runner, guard = std::weak_ptr<bool>(_guard)] {
		runner([=] {
			if (!guard.lock()) {
				return;
			}
			adjust();
		});
	};
}

void ConcurrentTimer::start(
		TimeMs timeout,
		Qt::TimerType type,
		Repeat repeat) {
	_type = type;
	setRepeat(repeat);
	_adjusted = false;
	setTimeout(timeout);

	cancelAndSchedule(_timeout);
	_next = crl::time() + _timeout;
}

void ConcurrentTimer::cancelAndSchedule(int timeout) {
	auto guards = base::make_binary_guard();
	_running = std::move(guards.first);
	auto method = [
		=,
		runner = _runner,
		guard = std::move(guards.second)
	]() mutable {
		if (!guard.alive()) {
			return;
		}
		runner([=, guard = std::move(guard)] {
			if (!guard.alive()) {
				return;
			}
			timerEvent();
		});
	};
	_object.call(timeout, _type, std::move(method));
}

void ConcurrentTimer::timerEvent() {
	if (repeat() == Repeat::Interval) {
		if (_adjusted) {
			start(_timeout, _type, repeat());
		} else {
			_next = crl::time() + _timeout;
		}
	} else {
		cancel();
	}

	if (_callback) {
		_callback();
	}
}

void ConcurrentTimer::cancel() {
	_running = {};
	if (isActive()) {
		_running = base::binary_guard();
		_object.cancel();
	}
}

TimeMs ConcurrentTimer::remainingTime() const {
	if (!isActive()) {
		return -1;
	}
	const auto now = crl::time();
	return (_next > now) ? (_next - now) : TimeMs(0);
}

void ConcurrentTimer::adjust() {
	auto remaining = remainingTime();
	if (remaining >= 0) {
		cancelAndSchedule(remaining);
		_adjusted = true;
	}
}

void ConcurrentTimer::setTimeout(TimeMs timeout) {
	Expects(timeout >= 0 && timeout <= std::numeric_limits<int>::max());

	_timeout = static_cast<unsigned int>(timeout);
}

int ConcurrentTimer::timeout() const {
	return _timeout;
}

} // namespace base
