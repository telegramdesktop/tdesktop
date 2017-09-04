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

template <typename Type>
inline Type take(Type &value) {
	return std::exchange(value, Type {});
}

template <typename Type, size_t Size>
inline constexpr size_t array_size(const Type(&)[Size]) {
	return Size;
}

template <typename Range, typename Method>
decltype(auto) for_each(Range &&range, Method &&method) {
	return std::for_each(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Method>(method));
}

template <typename Method>
decltype(auto) for_each_apply(Method &&method) {
	return [&method](auto &&range) {
		return for_each(std::forward<decltype(range)>(range), std::forward<Method>(method));
	};
}

template <typename Range, typename Value>
decltype(auto) find(Range &&range, Value &&value) {
	return std::find(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Value>(value));
}

template <typename Range, typename Predicate>
decltype(auto) find_if(Range &&range, Predicate &&predicate) {
	return std::find_if(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Predicate>(predicate));
}

template <typename Range, typename Type>
decltype(auto) lower_bound(Range &&range, Type &&value) {
	return std::lower_bound(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Type>(value));
}

template <typename Range, typename Type, typename Predicate>
decltype(auto) lower_bound(Range &&range, Type &&value, Predicate &&predicate) {
	return std::lower_bound(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Type>(value),
		std::forward<Predicate>(predicate));
}

template <typename Range, typename Type>
decltype(auto) upper_bound(Range &&range, Type &&value) {
	return std::upper_bound(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Type>(value));
}

template <typename Range, typename Type, typename Predicate>
decltype(auto) upper_bound(Range &&range, Type &&value, Predicate &&predicate) {
	return std::upper_bound(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Type>(value),
		std::forward<Predicate>(predicate));
}

template <typename Range, typename Type>
decltype(auto) equal_range(Range &&range, Type &&value) {
	return std::equal_range(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Type>(value));
}

template <typename Range, typename Type, typename Predicate>
decltype(auto) equal_range(Range &&range, Type &&value, Predicate &&predicate) {
	return std::equal_range(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Type>(value),
		std::forward<Predicate>(predicate));
}

template <typename Range>
decltype(auto) sort(Range &&range) {
	return std::sort(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)));
}

template <typename Range, typename Predicate>
decltype(auto) sort(Range &&range, Predicate &&predicate) {
	return std::sort(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Predicate>(predicate));
}

template <typename Range, typename Predicate>
decltype(auto) stable_partition(Range &&range, Predicate &&predicate) {
	return std::stable_partition(
		std::begin(std::forward<Range>(range)),
		std::end(std::forward<Range>(range)),
		std::forward<Predicate>(predicate));
}

} // namespace base
