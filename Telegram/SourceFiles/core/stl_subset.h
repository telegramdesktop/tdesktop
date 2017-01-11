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

#include <stdint.h>

// we copy some parts of C++11/14/17 std:: library, because on OS X 10.6+
// version we can use C++11/14/17, but we can not use its library :(
namespace std_ {

using nullptr_t = decltype(nullptr);

template <typename T, T V>
struct integral_constant {
	static constexpr T value = V;

	using value_type = T;
	using type = integral_constant<T, V>;

	constexpr operator value_type() const noexcept {
		return (value);
	}

	constexpr value_type operator()() const noexcept {
		return (value);
	}
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template <typename T>
struct remove_reference {
	using type = T;
};
template <typename T>
struct remove_reference<T&> {
	using type = T;
};
template <typename T>
struct remove_reference<T&&> {
	using type = T;
};
template <typename T>
using remove_reference_t = typename remove_reference<T>::type;

template <typename T>
struct is_lvalue_reference : false_type {
};
template <typename T>
struct is_lvalue_reference<T&> : true_type {
};

template <typename T>
struct is_rvalue_reference : false_type {
};
template <typename T>
struct is_rvalue_reference<T&&> : true_type {
};

template <typename T>
inline constexpr T &&forward(typename remove_reference<T>::type &value) noexcept {
	return static_cast<T&&>(value);
}
template <typename T>
inline constexpr T &&forward(typename remove_reference<T>::type &&value) noexcept {
	static_assert(!is_lvalue_reference<T>::value, "bad forward call");
	return static_cast<T&&>(value);
}

template <typename T>
inline constexpr typename remove_reference<T>::type &&move(T &&value) noexcept {
	return static_cast<typename remove_reference<T>::type&&>(value);
}

template <typename T>
void swap_moveable(T &a, T &b) {
	T tmp = move(a);
	a = move(b);
	b = move(tmp);
}

template <typename T>
struct remove_const {
	using type = T;
};

template <typename T>
struct remove_const<const T> {
	using type = T;
};

template <typename T>
struct remove_volatile {
	using type = T;
};

template <typename T>
struct remove_volatile<volatile T> {
	using type = T;
};

template <typename T>
using decay_simple_t = typename remove_const<typename remove_volatile<typename remove_reference<T>::type>::type>::type;

template <typename T1, typename T2>
struct is_same : false_type {
};

template <typename T>
struct is_same<T, T> : true_type {
};

template <bool, typename T = void>
struct enable_if {
};

template <typename T>
struct enable_if<true, T> {
	using type = T;
};

template <bool Test, typename T = void>
using enable_if_t = typename enable_if<Test, T>::type;

template <bool, typename First, typename Second>
struct conditional {
	using type = Second;
};

template <typename First, typename Second>
struct conditional<true, First, Second> {
	using type = First;
};

template <bool Test, typename First, typename Second>
using conditional_t = typename conditional<Test, First, Second>::type;

template <typename T>
struct add_const {
	using type = const T;
};

template <typename T>
using add_const_t = typename add_const<T>::type;

// This is not full unique_ptr, but at least with std interface.
template <typename T>
class unique_ptr {
public:
	constexpr unique_ptr() noexcept = default;
	unique_ptr(const unique_ptr<T> &) = delete;
	unique_ptr<T> &operator=(const unique_ptr<T> &) = delete;

	constexpr unique_ptr(std_::nullptr_t) {
	}
	unique_ptr<T> &operator=(std_::nullptr_t) noexcept {
		reset();
		return (*this);
	}

	explicit unique_ptr(T *p) noexcept : _p(p) {
	}

	template <typename U>
	unique_ptr(unique_ptr<U> &&other) noexcept : _p(other.release()) {
	}
	template <typename U>
	unique_ptr<T> &operator=(unique_ptr<U> &&other) noexcept {
		reset(other.release());
		return (*this);
	}
	unique_ptr<T> &operator=(unique_ptr<T> &&other) noexcept {
		if (this != &other) {
			reset(other.release());
		}
		return (*this);
	}

	void swap(unique_ptr<T> &other) noexcept {
		std::swap(_p, other._p);
	}
	~unique_ptr() noexcept {
		if (_p) {
			static_assert(sizeof(T) > 0, "can't delete an incomplete type");
			delete _p;
			_p = nullptr;
		}
	}

	T &operator*() const {
		return (*get());
	}
	T *operator->() const noexcept {
		return get();
	}
	T *get() const noexcept {
		return _p;
	}
	explicit operator bool() const noexcept {
		return get() != nullptr;
	}

	T *release() noexcept {
		auto old = _p;
		_p = nullptr;
		return old;
	}

	void reset(T *p = nullptr) noexcept {
		auto old = _p;
		_p = p;
		if (old) {
			static_assert(sizeof(T) > 0, "can't delete an incomplete type");
			delete old;
		}
	}

private:
	T *_p = nullptr;

};

template <typename T, typename... Args>
inline unique_ptr<T> make_unique(Args&&... args) {
	return unique_ptr<T>(new T(forward<Args>(args)...));
}

template <typename T>
inline bool operator==(const unique_ptr<T> &a, std_::nullptr_t) noexcept {
	return !a;
}
template <typename T>
inline bool operator==(std_::nullptr_t, const unique_ptr<T> &b) noexcept {
	return !b;
}
template <typename T>
inline bool operator!=(const unique_ptr<T> &a, std_::nullptr_t b) noexcept {
	return !(a == b);
}
template <typename T>
inline bool operator!=(std_::nullptr_t a, const unique_ptr<T> &b) noexcept {
	return !(a == b);
}

using _yes = char(&)[1];
using _no = char(&)[2];

template <typename Base, typename Derived>
struct _host {
	operator Base*() const;
	operator Derived*();
};

template <typename Base, typename Derived>
struct is_base_of {
	template <typename T>
	static _yes check(Derived*, T);
	static _no check(Base*, int);

	static constexpr bool value = sizeof(check(_host<Base, Derived>(), int())) == sizeof(_yes);
};

inline void *align(size_t alignment, size_t size, void*& ptr, size_t& space) noexcept {
#ifndef OS_MAC_OLD
	using std::uintptr_t;
#endif // OS_MAC_OLD

	auto p = reinterpret_cast<uintptr_t>(ptr);
	auto a = (p - 1u + alignment) & -alignment;
	auto d = a - p;
	if ((size + d) > space) {
		return nullptr;
	}
	space -= d;
	return ptr = reinterpret_cast<void*>(a);
}

} // namespace std_
