/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QLatin1String>

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

// We need a custom comparator for set<std::unique_ptr<T>>::find to work with pointers.
// thanks to http://stackoverflow.com/questions/18939882/raw-pointer-lookup-for-sets-of-unique-ptrs
template <typename T>
struct pointer_comparator {
	using is_transparent = std::true_type;

	// helper does some magic in order to reduce the number of
	// pairs of types we need to know how to compare: it turns
	// everything into a pointer, and then uses `std::less<T*>`
	// to do the comparison:
	struct helper {
		const T *ptr = nullptr;
		helper() = default;
		helper(const helper &other) = default;
		helper(const T *p) : ptr(p) {
		}
		template <typename ...Ts>
		helper(const std::shared_ptr<Ts...> &other) : ptr(other.get()) {
		}
		template <typename ...Ts>
		helper(const std::unique_ptr<Ts...> &other) : ptr(other.get()) {
		}
		bool operator<(helper other) const {
			return std::less<const T*>()(ptr, other.ptr);
		}
	};

	// without helper, we'd need 2^n different overloads, where
	// n is the number of types we want to support (so, 8 with
	// raw pointers, unique pointers, and shared pointers).  That
	// seems silly.
	// && helps enforce rvalue use only
	bool operator()(const helper &&lhs, const helper &&rhs) const {
		return lhs < rhs;
	}

};

} // namespace base

template <typename T>
inline void accumulate_max(T &a, const T &b) { if (a < b) a = b; }

template <typename T>
inline void accumulate_min(T &a, const T &b) { if (a > b) a = b; }

template <size_t Size>
QLatin1String qstr(const char(&string)[Size]) {
	return QLatin1String(string, Size - 1);
}
