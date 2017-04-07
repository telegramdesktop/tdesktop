/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "base/lambda.h"
#include "base/observer.h"

namespace base {

class Timer final : private QObject {
public:
	Timer(base::lambda<void()> callback = base::lambda<void()>());

	static Qt::TimerType DefaultType(TimeMs timeout) {
		constexpr auto kThreshold = TimeMs(1000);
		return (timeout > kThreshold) ? Qt::CoarseTimer : Qt::PreciseTimer;
	}

	void setCallback(base::lambda<void()> callback) {
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
		return (_timerId != 0);
	}

	void cancel();
	TimeMs remainingTime() const;

	static void Adjust();

protected:
	void timerEvent(QTimerEvent *e) override;

private:
	enum class Repeat {
		Interval   = 0,
		SingleShot = 1,
	};
	void start(TimeMs timeout, Qt::TimerType type, Repeat repeat);
	void adjust();

	void setTimeout(TimeMs timeout);
	int timeout() const;

	base::lambda<void()> _callback;
	TimeMs _next = 0;
	int _timeout = 0;
	int _timerId = 0;

	Qt::TimerType _type : 2;
	bool _adjusted : 1;
	Repeat _repeat : 1;

};

class DelayedCallTimer final : private QObject {
public:
	int call(TimeMs timeout, lambda_once<void()> callback) {
		return call(timeout, std::move(callback), Timer::DefaultType(timeout));
	}

	int call(TimeMs timeout, lambda_once<void()> callback, Qt::TimerType type);
	void cancel(int callId);

protected:
	void timerEvent(QTimerEvent *e) override;

private:
	std::map<int, lambda_once<void()>> _callbacks; // Better to use flatmap.

};

} // namespace base