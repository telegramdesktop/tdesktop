/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <any>

namespace base {
namespace details {

template <typename Value>
struct moveable_as_copyable_wrap {
	moveable_as_copyable_wrap(Value &&other)
	: value(std::move(other)) {
	}
	moveable_as_copyable_wrap &operator=(Value &&other) {
		value = std::move(other);
		return *this;
	}
	moveable_as_copyable_wrap(moveable_as_copyable_wrap &&other)
	: value(std::move(other.value)) {
	}
	moveable_as_copyable_wrap(
			const moveable_as_copyable_wrap &other) {
		Unexpected("Attempt to copy-construct a move-only type.");
	}
	moveable_as_copyable_wrap &operator=(
			moveable_as_copyable_wrap &&other) {
		value = std::move(other.value);
		return *this;
	}
	moveable_as_copyable_wrap &operator=(
			const moveable_as_copyable_wrap &other) {
		Unexpected("Attempt to copy-assign a move-only type.");
	}

	Value value;

};

template <
	typename Value,
	typename = std::enable_if_t<
		std::is_move_constructible_v<std::decay_t<Value>>
		&& !std::is_lvalue_reference_v<Value>>>
auto wrap_moveable_as_copyable(Value &&value) {
	return moveable_as_copyable_wrap<Value>(std::move(value));
}

} // namespace details

class unique_any;

template <typename Value>
Value *any_cast(unique_any *value) noexcept;

template <typename Value>
const Value *any_cast(const unique_any *value) noexcept;

class unique_any final {
public:
	// Construction and destruction [any.cons]
	constexpr unique_any() noexcept {
	}

	unique_any(const unique_any &other) = delete;
	unique_any &operator=(const unique_any &other) = delete;

	unique_any(unique_any &&other) noexcept
	: _impl(std::move(other._impl)) {
	}

	unique_any &operator=(unique_any &&other) noexcept {
		_impl = std::move(other._impl);
		return *this;
	}

	template <
		typename Value,
		typename = std::enable_if_t<
			!std::is_same_v<std::decay_t<Value>, unique_any>>>
	unique_any(Value &&other)
	: unique_any(
		std::forward<Value>(other),
		std::is_copy_constructible<std::decay_t<Value>>()) {
	}

	template <
		typename Value,
		typename = std::enable_if_t<
			!std::is_same_v<std::decay_t<Value>, unique_any>>>
	unique_any &operator=(Value &&other) {
		if constexpr (std::is_copy_constructible_v<std::decay_t<Value>>) {
			_impl = std::forward<Value>(other);
		} else if constexpr (std::is_move_constructible_v<std::decay_t<Value>>
			&& !std::is_lvalue_reference_v<Value>) {
			_impl = details::wrap_moveable_as_copyable(std::move(other));
		} else {
			static_assert(
				false_t(Value{}),
				"Bad value for base::unique_any.");
		}
		return *this;
	}

	template <
		typename Value,
		typename ...Args,
		typename = std::enable_if_t<
			std::is_constructible_v<std::decay_t<Value>, Args...>
			&& std::is_copy_constructible_v<decay_t<Value>>>>
	std::decay_t<Value> &emplace(Args &&...args) {
		return _impl.emplace<Value>(std::forward<Args>(args)...);
	}

	void reset() noexcept {
		_impl.reset();
	}

	void swap(unique_any &other) noexcept {
		_impl.swap(other._impl);
	}

	bool has_value() const noexcept {
		return _impl.has_value();
	}

	// Should check if it is a moveable_only wrap first.
	//const std::type_info &type() const noexcept {
	//	return _impl.type();
	//}

private:
	template <
		typename Value,
		typename = std::enable_if_t<
			!std::is_same_v<std::decay_t<Value>, unique_any>
			&& std::is_copy_constructible_v<std::decay_t<Value>>>>
	unique_any(Value &&other, std::true_type)
	: _impl(std::forward<Value>(other)) {
	}

	template <
		typename Value,
		typename = std::enable_if_t<
			!std::is_same_v<std::decay_t<Value>, unique_any>
			&& !std::is_copy_constructible_v<std::decay_t<Value>>
			&& std::is_move_constructible_v<std::decay_t<Value>>
			&& !std::is_lvalue_reference_v<Value>>>
	unique_any(Value &&other, std::false_type)
	: _impl(details::wrap_moveable_as_copyable(std::move(other))) {
	}

	template <
		typename Value,
		typename ...Args>
	friend unique_any make_any(Args &&...args);

	template <typename Value>
	friend const Value *any_cast(const unique_any *value) noexcept;

	template <typename Value>
	friend Value *any_cast(unique_any *value) noexcept;

	std::any _impl;

};

inline void swap(unique_any &a, unique_any &b) noexcept {
	a.swap(b);
}

template <
	typename Value,
	typename ...Args>
inline auto make_any(Args &&...args)
-> std::enable_if_t<
		std::is_copy_constructible_v<std::decay_t<Value>>,
		unique_any> {
	return std::make_any<Value>(std::forward<Args>(args)...);
}

template <
	typename Value,
	typename ...Args>
inline auto make_any(Args &&...args)
-> std::enable_if_t<
		!std::is_copy_constructible_v<std::decay_t<Value>>
		&& std::is_move_constructible_v<std::decay_t<Value>>,
		unique_any> {
	return Value(std::forward<Args>(args)...);
}

template <typename Value>
inline Value *any_cast(unique_any *value) noexcept {
	if constexpr (std::is_copy_constructible_v<Value>) {
		return std::any_cast<Value>(&value->_impl);
	} else if constexpr (std::is_move_constructible_v<Value>) {
		auto wrap = std::any_cast<
			details::moveable_as_copyable_wrap<Value>
		>(&value->_impl);
		return wrap ? &wrap->value : nullptr;
	} else {
		static_assert(
			false_t(Value{}),
			"Bad type for base::any_cast.");
	}
}

template <typename Value>
inline const Value *any_cast(const unique_any *value) noexcept {
	if constexpr (std::is_copy_constructible_v<Value>) {
		return std::any_cast<Value>(&value->_impl);
	} else if constexpr (std::is_move_constructible_v<Value>) {
		auto wrap = std::any_cast<
			details::moveable_as_copyable_wrap<Value>
		>(&value->_impl);
		return wrap ? &wrap->value : nullptr;
	} else {
		static_assert(
			false_t(Value{}),
			"Bad type for base::any_cast.");
	}
}

} // namespace base
