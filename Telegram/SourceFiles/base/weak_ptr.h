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

class has_weak_ptr;

namespace details {

struct alive_tracker {
	explicit alive_tracker(const has_weak_ptr *value) : value(value) {
	}

	std::atomic<int> counter = 1;
	std::atomic<const has_weak_ptr*> value;
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
			alive->value.store(nullptr);
			details::decrement(alive);
		}
	}

private:
	template <typename Child>
	friend class weak_ptr;

	details::alive_tracker *incrementAliveTracker() const {
		auto current = _alive.load();
		if (!current) {
			auto alive = std::make_unique<details::alive_tracker>(this);
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
	: _alive(value ? value->incrementAliveTracker() : nullptr) {
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
	: _alive(details::check_and_increment(other._alive)) {
	}
	weak_ptr(weak_ptr &&other) noexcept
	: _alive(std::exchange(other._alive, nullptr)) {
	}
	template <
		typename Other,
		typename = std::enable_if_t<
			std::is_base_of_v<T, Other> && !std::is_same_v<T, Other>>>
	weak_ptr(const weak_ptr<Other> &other) noexcept
	: _alive(details::check_and_increment(other._alive)) {
	}
	template <
		typename Other,
		typename = std::enable_if_t<
			std::is_base_of_v<T, Other> && !std::is_same_v<T, Other>>>
	weak_ptr(weak_ptr<Other> &&other) noexcept
	: _alive(std::exchange(other._alive, nullptr)) {
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
		if (_alive != other._alive) {
			destroy();
			_alive = details::check_and_increment(other._alive);
		}
		return *this;
	}
	weak_ptr &operator=(weak_ptr &&other) noexcept {
		if (_alive != other._alive) {
			destroy();
			_alive = std::exchange(other._alive, nullptr);
		}
		return *this;
	}
	template <
		typename Other,
		typename = std::enable_if_t<
			std::is_base_of_v<T, Other> && !std::is_same_v<T, Other>>>
	weak_ptr &operator=(const weak_ptr<Other> &other) noexcept {
		if (_alive != other._alive) {
			destroy();
			_alive = details::check_and_increment(other._alive);
		}
		return *this;
	}
	template <
		typename Other,
		typename = std::enable_if_t<
			std::is_base_of_v<T, Other> && !std::is_same_v<T, Other>>>
	weak_ptr &operator=(weak_ptr<Other> &&other) noexcept {
		if (_alive != other._alive) {
			destroy();
			_alive = std::exchange(other._alive, nullptr);
		}
		return *this;
	}

	~weak_ptr() {
		destroy();
	}

	T *get() const noexcept {
		const auto strong = _alive ? _alive->value.load() : nullptr;
		if constexpr (std::is_const_v<T>) {
			return static_cast<T*>(strong);
		} else {
			return const_cast<T*>(static_cast<const T*>(strong));
		}
	}
	explicit operator bool() const noexcept {
		return (_alive && _alive->value);
	}
	T &operator*() const noexcept {
		return *get();
	}
	T *operator->() const noexcept {
		return get();
	}

	void reset(T *value = nullptr) {
		if (get() != value) {
			destroy();
			_alive = value ? value->incrementAliveTracker() : nullptr;
		}
	}

private:
	void destroy() noexcept {
		if (_alive) {
			details::decrement(_alive);
		}
	}

	details::alive_tracker *_alive = nullptr;

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
