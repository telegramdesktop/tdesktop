/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/build_config.h"
#include <tuple>

namespace rpl {
namespace details {

template <typename Arg>
const Arg &const_ref_val() noexcept;

template <typename Arg>
Arg &lvalue_ref_val() noexcept;

using false_t = char;
struct true_t {
	false_t data[2];
};
static_assert(sizeof(false_t) != sizeof(true_t), "I can't work :(");

template <
	typename Method,
	typename ...Args,
	typename = decltype(std::declval<Method>()(
		std::declval<Args>()...))>
true_t test_callable_plain(Method &&, Args &&...) noexcept;
false_t test_callable_plain(...) noexcept;

template <typename Method, typename ...Args>
struct is_callable_plain
	: std::bool_constant<(
		sizeof(test_callable_plain(
			std::declval<Method>(),
			std::declval<Args>()...
		)) == sizeof(true_t))> {
};

template <typename Method, typename ...Args>
constexpr bool is_callable_plain_v = is_callable_plain<Method, Args...>::value;

template <
	typename Method,
	typename ...Types,
	typename = decltype(std::declval<Method>()(
		std::declval<Types>()...))>
true_t test_callable_tuple(
	Method &&,
	std::tuple<Types...> &&) noexcept;
template <
	typename Method,
	typename ...Types,
	typename = decltype(std::declval<Method>()(
		const_ref_val<Types>()...))>
true_t test_callable_tuple(
	Method &&,
	const std::tuple<Types...> &) noexcept;
false_t test_callable_tuple(...) noexcept;

template <typename Method, typename Arg>
constexpr bool is_callable_tuple_v = (sizeof(test_callable_tuple(
	std::declval<Method>(),
	std::declval<Arg>())) == sizeof(true_t));

template <typename Method, typename Arg>
struct is_callable_tuple
	: std::bool_constant<
		is_callable_tuple_v<Method, Arg>> {
};

template <typename Method, typename ...Args>
struct is_callable;

template <typename Method>
struct is_callable<Method>
	: std::bool_constant<
		is_callable_plain_v<Method>> {
};

template <typename Method, typename Arg>
struct is_callable<Method, Arg>
	: std::bool_constant<
		is_callable_plain_v<Method, Arg> ||
		is_callable_tuple_v<Method, Arg> ||
		is_callable_plain_v<Method>> {
};

template <typename Method, typename ...Args>
constexpr bool is_callable_v = is_callable<Method, Args...>::value;

template <typename Method, typename Arg>
inline decltype(auto) callable_invoke(Method &&method, Arg &&arg) {
	if constexpr (is_callable_plain_v<Method, Arg>) {
		return std::forward<Method>(method)(std::forward<Arg>(arg));
	} else if constexpr (is_callable_tuple_v<Method, Arg>) {
		return std::apply(
			std::forward<Method>(method),
			std::forward<Arg>(arg));
	} else if constexpr (is_callable_v<Method>) {
		return std::forward<Method>(method)();
	} else {
		static_assert(false_(method, arg), "Bad callable_invoke() call.");
	}
}

template <typename Method, typename Arg>
using callable_result = decltype(callable_invoke(
	std::declval<Method>(),
	std::declval<Arg>()));

template <
	typename Method,
	typename Arg,
	typename = decltype(std::declval<Method>()(
		const_ref_val<std::decay_t<Arg>>()))>
true_t test_allows_const_ref(Method &&, Arg &&) noexcept;
false_t test_allows_const_ref(...) noexcept;

template <typename Method, typename Arg>
constexpr bool allows_const_ref_v = (sizeof(test_allows_const_ref(
	std::declval<Method>(),
	std::declval<Arg>())) == sizeof(true_t));

template <typename Method, typename Arg>
inline decltype(auto) const_ref_call_invoke(
		Method &&method,
		const Arg &arg) {
	if constexpr (allows_const_ref_v<Method, Arg>) {
		return callable_invoke(std::forward<Method>(method), arg);
	} else {
		auto copy = arg;
		return callable_invoke(
			std::forward<Method>(method),
			std::move(copy));
	}
}

} // namespace details
} // namespace rpl
