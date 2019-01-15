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

template <typename Container, typename T>
inline bool contains(const Container &container, const T &value) {
	const auto end = std::end(container);
	return std::find(std::begin(container), end, value) != end;
}

} // namespace base

template <typename T>
inline void accumulate_max(T &a, const T &b) { if (a < b) a = b; }

template <typename T>
inline void accumulate_min(T &a, const T &b) { if (a > b) a = b; }

template <size_t Size>
QLatin1String qstr(const char(&string)[Size]) {
	return QLatin1String(string, Size - 1);
}
