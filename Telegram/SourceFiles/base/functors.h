/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace base {
namespace functors {

struct abs_helper {
	template <
		typename Type,
		typename = decltype(0 < std::declval<Type>()),
		typename = decltype(-std::declval<Type>())>
	constexpr Type operator()(Type value) const {
		return (0 < value) ? value : (-value);
	}
};
constexpr auto abs = abs_helper{};

constexpr auto add = [](auto value) {
	return [value](auto other) {
		return value + other;
	};
};

struct negate_helper {
	template <
		typename Type,
		typename = decltype(-std::declval<Type>())>
	constexpr Type operator()(Type value) const {
		return -value;
	}
};
constexpr auto negate = negate_helper{};

} // namespace functors
} // namespace base
