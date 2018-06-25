/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <atomic>
#include <memory>

namespace base {
namespace details {

struct alive_tracker {
	std::atomic<int> counter = 1;
	std::atomic<bool> dead = false;
};

inline alive_tracker *check_and_increment(alive_tracker *tracker) noexcept {
	if (tracker) {
		++tracker->counter;
	}
	return tracker;
}

inline void decrement(alive_tracker *tracker) noexcept {
	if (tracker->counter.fetch_sub(1) == 0) {
		delete tracker;
	}
}

} // namespace details

class has_weak_ptr;

template <typename T>
class weak_ptr;

class has_weak_ptr {
public:
	has_weak_ptr() = default;
	has_weak_ptr(const has_weak_ptr &other) noexcept {
	}
	has_weak_ptr(has_weak_ptr &&other) noexcept {
	}
	has_weak_ptr &operator=(const has_weak_ptr &other) noexcept {
		return *this;
	}
	has_weak_ptr &operator=(has_weak_ptr &&other) noexcept {
		return *this;
	}

	~has_weak_ptr() {
		if (auto alive = _alive.load()) {
			alive->dead.store(true);
			details::decrement(alive);
		}
	}

private:
	template <typename Child>
	friend class weak_ptr;

	details::alive_tracker *incrementAliveTracker() const {
		auto current = _alive.load();
		if (!current) {
			auto alive = std::make_unique<details::alive_tracker>();
			if (_alive.compare_exchange_strong(current, alive.get())) {
				return alive.release();
			}
		}
		++current->counter;
		return current;
	}

	mutable std::atomic<details::alive_tracker*> _alive = nullptr;

};

template <typename T>
class weak_ptr {
public:
	weak_ptr() = default;
	weak_ptr(T *value)
	: _alive(value ? value->incrementAliveTracker() : nullptr)
	, _value(value) {
	}
	weak_ptr(const std::unique_ptr<T> &value)
	: weak_ptr(value.get()) {
	}
	weak_ptr(const std::shared_ptr<T> &value)
	: weak_ptr(value.get()) {
	}
	weak_ptr(const std::weak_ptr<T> &value)
	: weak_ptr(value.lock().get()) {
	}
	weak_ptr(const weak_ptr &other) noexcept
	: _alive(details::check_and_increment(other._alive))
	, _value(other._value) {
	}
	weak_ptr(weak_ptr &&other) noexcept
	: _alive(std::exchange(other._alive, nullptr))
	, _value(std::exchange(other._value, nullptr)) {
	}
	template <
		typename Other,
		typename = std::enable_if_t<
			std::is_base_of_v<T, Other> && !std::is_same_v<T, Other>>>
	weak_ptr(const weak_ptr<Other> &other) noexcept
	: _alive(details::check_and_increment(other._alive))
	, _value(other._value) {
	}
	template <
		typename Other,
		typename = std::enable_if_t<
			std::is_base_of_v<T, Other> && !std::is_same_v<T, Other>>>
	weak_ptr(weak_ptr<Other> &&other) noexcept
	: _alive(std::exchange(other._alive, nullptr))
	, _value(std::exchange(other._value, nullptr)) {
	}

	weak_ptr &operator=(T *value) {
		reset(value);
		return *this;
	}
	weak_ptr &operator=(const std::unique_ptr<T> &value) {
		reset(value.get());
		return *this;
	}
	weak_ptr &operator=(const std::shared_ptr<T> &value) {
		reset(value.get());
		return *this;
	}
	weak_ptr &operator=(const std::weak_ptr<T> &value) {
		reset(value.lock().get());
		return *this;
	}
	weak_ptr &operator=(const weak_ptr &other) noexcept {
		if (_value != other._value) {
			destroy();
			_alive = details::check_and_increment(other._alive);
			_value = other._value;
		}
		return *this;
	}
	weak_ptr &operator=(weak_ptr &&other) noexcept {
		if (_value != other._value) {
			destroy();
			_alive = std::exchange(other._alive, nullptr);
			_value = std::exchange(other._value, nullptr);
		}
		return *this;
	}
	template <
		typename Other,
		typename = std::enable_if_t<
			std::is_base_of_v<T, Other> && !std::is_same_v<T, Other>>>
	weak_ptr &operator=(const weak_ptr<Other> &other) noexcept {
		if (_value != other._value) {
			destroy();
			_alive = details::check_and_increment(other._alive);
			_value = other._value;
		}
		return *this;
	}
	template <
		typename Other,
		typename = std::enable_if_t<
			std::is_base_of_v<T, Other> && !std::is_same_v<T, Other>>>
	weak_ptr &operator=(weak_ptr<Other> &&other) noexcept {
		if (_value != other._value) {
			destroy();
			_alive = std::exchange(other._alive, nullptr);
			_value = std::exchange(other._value, nullptr);
		}
		return *this;
	}

	~weak_ptr() {
		destroy();
	}

	T *get() const noexcept {
		return (_alive && !_alive->dead) ? _value : nullptr;
	}
	explicit operator bool() const noexcept {
		return (_alive && !_alive->dead);
	}
	T &operator*() const noexcept {
		return *get();
	}
	T *operator->() const noexcept {
		return get();
	}

	void reset(T *value = nullptr) {
		if (_value != value) {
			destroy();
			_alive = value ? value->incrementAliveTracker() : nullptr;
			_value = value;
		}
	}

private:
	void destroy() noexcept {
		if (_alive) {
			details::decrement(_alive);
		}
	}

	details::alive_tracker *_alive = nullptr;
	T *_value = nullptr;

	template <typename Other>
	friend class weak_ptr;

};

template <typename T>
inline bool operator==(const weak_ptr<T> &pointer, std::nullptr_t) noexcept {
	return (pointer.get() == nullptr);
}

template <typename T>
inline bool operator==(std::nullptr_t, const weak_ptr<T> &pointer) noexcept {
	return (pointer == nullptr);
}

template <typename T>
inline bool operator!=(const weak_ptr<T> &pointer, std::nullptr_t) noexcept {
	return !(pointer == nullptr);
}

template <typename T>
inline bool operator!=(std::nullptr_t, const weak_ptr<T> &pointer) noexcept {
	return !(pointer == nullptr);
}

template <
	typename T,
	typename = std::enable_if_t<std::is_base_of_v<has_weak_ptr, T>>>
weak_ptr<T> make_weak(T *value) {
	return value;
}

template <
	typename T,
	typename = std::enable_if_t<std::is_base_of_v<has_weak_ptr, T>>>
weak_ptr<T> make_weak(const std::unique_ptr<T> &value) {
	return value;
}

template <
	typename T,
	typename = std::enable_if_t<std::is_base_of_v<has_weak_ptr, T>>>
weak_ptr<T> make_weak(const std::shared_ptr<T> &value) {
	return value;
}

template <
	typename T,
	typename = std::enable_if_t<std::is_base_of_v<has_weak_ptr, T>>>
weak_ptr<T> make_weak(const std::weak_ptr<T> &value) {
	return value;
}

} // namespace base

namespace crl {

template <typename T, typename Enable>
struct guard_traits;

template <typename T>
struct guard_traits<base::weak_ptr<T>, void> {
	static base::weak_ptr<T> create(const base::weak_ptr<T> &value) {
		return value;
	}
	static base::weak_ptr<T> create(base::weak_ptr<T> &&value) {
		return std::move(value);
	}
	static bool check(const base::weak_ptr<T> &guard) {
		return guard.get() != nullptr;
	}

};

template <typename T>
struct guard_traits<
	T*,
	std::enable_if_t<
		std::is_base_of_v<base::has_weak_ptr, std::remove_cv_t<T>>>> {
	static base::weak_ptr<T> create(T *value) {
		return value;
	}
	static bool check(const base::weak_ptr<T> &guard) {
		return guard.get() != nullptr;
	}

};

template <typename T>
struct guard_traits<
	gsl::not_null<T*>,
	std::enable_if_t<
		std::is_base_of_v<base::has_weak_ptr, std::remove_cv_t<T>>>> {
	static base::weak_ptr<T> create(gsl::not_null<T*> value) {
		return value.get();
	}
	static bool check(const base::weak_ptr<T> &guard) {
		return guard.get() != nullptr;
	}

};

} // namespace crl
