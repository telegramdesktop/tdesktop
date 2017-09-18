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

#include "base/build_config.h"


// Custom libc++ build used for old OS X versions already has this.
#ifndef OS_MAC_OLD

#if defined COMPILER_CLANG || defined COMPILER_GCC
namespace std {

#ifdef COMPILER_GCC

template <bool Value>
using bool_constant = integral_constant<bool, Value>;

#endif // COMPILER_GCC

template <typename ...Args>
constexpr auto tuple_size_v = std::tuple_size<Args...>::value;

template <typename ...Args>
constexpr auto is_rvalue_reference_v = is_rvalue_reference<Args...>::value;

template <typename ...Args>
constexpr auto is_base_of_v = is_base_of<Args...>::value;

template <typename ...Args>
constexpr auto is_same_v = is_same<Args...>::value;

namespace detail {

template <typename Method, typename Tuple, size_t... I>
constexpr decltype(auto) apply_impl(
		Method &&method,
		Tuple &&tuple,
		index_sequence<I...>) {
    return forward<Method>(method)(get<I>(forward<Tuple>(tuple))...);
}

} // namespace detail

template <class Method, class Tuple>
constexpr decltype(auto) apply(Method &&method, Tuple&& tuple) {
    return detail::apply_impl(
        forward<Method>(method),
		forward<Tuple>(tuple),
        make_index_sequence<tuple_size_v<decay_t<Tuple>>>{});
}

} // namespace std
#endif // COMPILER_CLANG || COMPILER_GCC

#endif // OS_MAC_OLD

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
		is_callable_plain_v<Method, Arg>
		|| is_callable_tuple_v<Method, Arg>> {
};

template <typename Method, typename ...Args>
constexpr bool is_callable_v = is_callable<Method, Args...>::value;

template <typename Method, typename Arg>
inline decltype(auto) callable_helper(Method &&method, Arg &&arg, std::true_type) {
	return std::move(method)(std::forward<Arg>(arg));
}

template <typename Method, typename Arg>
inline decltype(auto) callable_helper(Method &&method, Arg &&arg, std::false_type) {
	return std::apply(std::move(method), std::forward<Arg>(arg));
}

template <typename Method, typename Arg>
inline decltype(auto) callable_invoke(Method &&method, Arg &&arg) {
	return callable_helper(
		std::forward<Method>(method),
		std::forward<Arg>(arg),
		is_callable_plain<Method, Arg>());
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
struct allows_const_ref
	: std::bool_constant<
		allows_const_ref_v<Method, Arg>> {
};

template <typename Method, typename Arg>
inline decltype(auto) const_ref_call_helper(
		Method &method,
		const Arg &arg,
		std::true_type) {
	return callable_invoke(method, arg);
}

template <typename Method, typename Arg>
inline decltype(auto) const_ref_call_helper(
		Method &method,
		const Arg &arg,
		std::false_type) {
	auto copy = arg;
	return callable_invoke(method, std::move(copy));
}

} // namespace details
} // namespace rpl
