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

// This version of remove_if allows predicate to push_back() items.
// The added items won't be tested for predicate but just left in the container.
template <typename Container, typename Predicate>
void push_back_safe_remove_if(
	Container &&container,
	Predicate &&predicate) {
	auto first = size_t(0);
	auto count = container.size();
	auto moveFrom = first;
	for (; moveFrom != count; ++moveFrom) {
		if (predicate(container[moveFrom])) {
			break;
		}
	}
	if (moveFrom != count) {
		auto moveTo = moveFrom;
		for (++moveFrom; moveFrom != count; ++moveFrom) {
			if (!predicate(container[moveFrom])) {
				container[moveTo++] = std::move(container[moveFrom]);
			}
		}

		// Move items that we've added while checking the initial items.
		count = container.size();
		for (; moveFrom != count; ++moveFrom) {
			container[moveTo++] = std::move(container[moveFrom]);
		}

		container.erase(container.begin() + moveTo, container.end());
	}
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

} // namespace base
