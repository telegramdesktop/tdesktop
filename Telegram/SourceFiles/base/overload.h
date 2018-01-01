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

// This implementation was taken from range-v3 library.
// It was modified so that more than one of passed function objects
// could be called with an argument list and the first one that
// matches gets used instead of a compile-time ambiguity error.
//
// This allows to write "default" visitor handlers with [](auto&&) syntax.

template <typename ...Args>
constexpr bool is_callable_v = rpl::details::is_callable_plain_v<Args...>;

template <typename ...Args>
struct overloaded;

template <>
struct overloaded<> {
};

template <typename First, typename ...Rest>
struct overloaded<First, Rest...>
	: private First
	, private overloaded<Rest...> {

private:
	using Others = overloaded<Rest...>;

public:
	overloaded() = default;

	constexpr overloaded(First first, Rest... rest)
	: First(std::move(first))
	, Others(std::move(rest)...) {
	}

	template <typename... Args>
	auto operator()(Args&&... args)
	-> decltype(std::declval<First&>()(std::forward<Args>(args)...)) {
		return static_cast<First&>(*this)(std::forward<Args>(args)...);
	}

	template <typename... Args>
	auto operator()(Args&&... args) const
	-> decltype(std::declval<const First&>()(std::forward<Args>(args)...)) {
		return static_cast<const First&>(*this)(std::forward<Args>(args)...);
	}

	template <
		typename... Args,
		typename = std::enable_if_t<!is_callable_v<First&, Args&&...>>>
	auto operator()(Args&&... args)
	-> decltype(std::declval<Others&>()(std::forward<Args>(args)...)) {
		return static_cast<Others&>(*this)(std::forward<Args>(args)...);
	}

	template <
		typename... Args,
		typename = std::enable_if_t<!is_callable_v<const First&, Args&&...>>>
	auto operator()(Args&&... args) const
	-> decltype(std::declval<const Others&>()(std::forward<Args>(args)...)) {
		return static_cast<const Others&>(*this)(std::forward<Args>(args)...);
	}

};

} // namespace details

template <typename Function>
Function overload(Function &&function) {
	return std::forward<Function>(function);
}

template <typename ...Functions, typename = std::enable_if_t<(sizeof...(Functions) > 1)>>
auto overload(Functions ...functions) {
	return details::overloaded<Functions...>(std::move(functions)...);
}

} // namespace base

