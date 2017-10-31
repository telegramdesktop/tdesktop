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

namespace base {
namespace details {

template <
	typename Type,
	typename Operator,
	typename = decltype(Operator{}(
		std::declval<Type>(),
		std::declval<Type>()))>
char test_operator(const Type &, const Operator &);
int test_operator(...);

template <typename Type, template <typename> typename Operator>
struct has_operator
	: std::bool_constant<
		sizeof(test_operator(
			std::declval<Type>(),
			std::declval<Operator<Type>>()
		)) == sizeof(char)> {
};

template <
	typename Type,
	template <typename> typename Operator>
constexpr bool has_operator_v
	= has_operator<Type, Operator>::value;

template <typename Type>
constexpr bool has_less_v = has_operator_v<Type, std::less>;

template <typename Type>
constexpr bool has_greater_v = has_operator_v<Type, std::greater>;

template <typename Type>
constexpr bool has_less_equal_v = has_operator_v<Type, std::less_equal>;

template <typename Type>
constexpr bool has_greater_equal_v = has_operator_v<Type, std::greater_equal>;

template <typename Type>
constexpr bool has_equal_to_v = has_operator_v<Type, std::equal_to>;

template <typename Type>
constexpr bool has_not_equal_to_v = has_operator_v<Type, std::not_equal_to>;

} // namespace details
} // namespace base

template <
	typename ValueType,
	typename Helper = decltype(
		value_ordering_helper(std::declval<ValueType>()))>
inline auto operator<(const ValueType &a, const ValueType &b)
-> std::enable_if_t<base::details::has_less_v<Helper>, bool> {
	return value_ordering_helper(a) < value_ordering_helper(b);
}

template <
	typename ValueType,
	typename Helper = decltype(
		value_ordering_helper(std::declval<ValueType>()))>
inline auto operator>(const ValueType &a, const ValueType &b)
-> std::enable_if_t<
	base::details::has_greater_v<Helper>
		|| base::details::has_less_v<Helper>,
	bool
> {
	if constexpr (base::details::has_greater_v<Helper>) {
		return value_ordering_helper(a) > value_ordering_helper(b);
	} else {
		return value_ordering_helper(b) < value_ordering_helper(a);
	}
}

template <
	typename ValueType,
	typename Helper = decltype(
		value_ordering_helper(std::declval<ValueType>()))>
inline auto operator<=(const ValueType &a, const ValueType &b)
-> std::enable_if_t<
	base::details::has_less_equal_v<Helper>
		|| base::details::has_less_v<Helper>,
	bool
> {
	if constexpr (base::details::has_less_equal_v<Helper>) {
		return value_ordering_helper(a) <= value_ordering_helper(b);
	} else {
		return !(value_ordering_helper(b) < value_ordering_helper(a));
	}
}

template <
	typename ValueType,
	typename Helper = decltype(
		value_ordering_helper(std::declval<ValueType>()))>
inline auto operator>=(const ValueType &a, const ValueType &b)
-> std::enable_if_t<
	base::details::has_greater_equal_v<Helper>
		|| base::details::has_less_v<Helper>,
	bool
> {
	if constexpr (base::details::has_greater_equal_v<Helper>) {
		return value_ordering_helper(a) >= value_ordering_helper(b);
	} else {
		return !(value_ordering_helper(a) < value_ordering_helper(b));
	}
}

template <
	typename ValueType,
	typename Helper = decltype(
		value_ordering_helper(std::declval<ValueType>()))>
inline auto operator==(const ValueType &a, const ValueType &b)
-> std::enable_if_t<base::details::has_equal_to_v<Helper>, bool> {
	return value_ordering_helper(a) == value_ordering_helper(b);
}

template <
	typename ValueType,
	typename Helper = decltype(
		value_ordering_helper(std::declval<ValueType>()))>
inline auto operator!=(const ValueType &a, const ValueType &b)
-> std::enable_if_t<
	base::details::has_not_equal_to_v<Helper>
		|| base::details::has_equal_to_v<Helper>,
	bool
> {
	if constexpr (base::details::has_not_equal_to_v<Helper>) {
		return value_ordering_helper(a) != value_ordering_helper(b);
	} else {
		return !(value_ordering_helper(a) == value_ordering_helper(b));
	}
}
