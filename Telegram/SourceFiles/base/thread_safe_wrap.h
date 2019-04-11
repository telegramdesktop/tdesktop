/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <utility>

namespace base {

template <typename T>
class thread_safe_wrap {
public:
	template <typename ...Args>
	thread_safe_wrap(Args &&...args) : _value(std::forward<Args>(args)...) {
	}

	template <typename Callback>
	auto with(Callback &&callback) {
		QMutexLocker lock(&_mutex);
		return callback(_value);
	}

	template <typename Callback>
	auto with(Callback &&callback) const {
		QMutexLocker lock(&_mutex);
		return callback(_value);
	}

private:
	T _value;
	QMutex _mutex;

};

template <typename T, template<typename...> typename Container = std::deque>
class thread_safe_queue {
public:
	template <typename ...Args>
	void emplace(Args &&...args) {
		_wrap.with([&](Container<T> &value) {
			value.emplace_back(std::forward<Args>(args)...);
		});
	}

	Container<T> take() {
		return _wrap.with([&](Container<T> &value) {
			return std::exchange(value, Container<T>());
		});
	}

private:
	thread_safe_wrap<Container<T>> _wrap;

};

} // namespace base
