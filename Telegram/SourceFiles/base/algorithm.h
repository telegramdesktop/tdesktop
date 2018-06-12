/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace base {

template <typename Type>
inline Type take(Type &value) {
	return std::exchange(value, Type {});
}

template <typename Type>
inline Type duplicate(const Type &value) {
	return value;
}

template <typename Type, size_t Size>
inline constexpr size_t array_size(const Type(&)[Size]) {
	return Size;
}

} // namespace base
