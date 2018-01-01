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

template <typename T>
class unique_qptr {
public:
	unique_qptr() = default;
	unique_qptr(std::nullptr_t) noexcept {
	}
	explicit unique_qptr(T *pointer) noexcept
	: _object(pointer) {
	}

	unique_qptr(const unique_qptr &other) = delete;
	unique_qptr &operator=(const unique_qptr &other) = delete;
	unique_qptr(unique_qptr &&other) noexcept
	: _object(base::take(other._object)) {
	}
	unique_qptr &operator=(unique_qptr &&other) noexcept {
		if (_object != other._object) {
			destroy();
			_object = base::take(other._object);
		}
		return *this;
	}

	template <
		typename U,
		typename = std::enable_if_t<std::is_base_of_v<T, U>>>
	unique_qptr(unique_qptr<U> &&other) noexcept
	: _object(base::take(other._object)) {
	}

	template <
		typename U,
		typename = std::enable_if_t<std::is_base_of_v<T, U>>>
	unique_qptr &operator=(unique_qptr<U> &&other) noexcept {
		if (_object != other._object) {
			destroy();
			_object = base::take(other._object);
		}
		return *this;
	}

	unique_qptr &operator=(std::nullptr_t) noexcept {
		destroy();
		return *this;
	}

	template <typename ...Args>
	explicit unique_qptr(std::in_place_t, Args &&...args)
	: _object(new T(std::forward<Args>(args)...)) {
	}

	template <typename ...Args>
	T *emplace(Args &&...args) {
		reset(new T(std::forward<Args>(args)...));
		return get();
	}

	void reset(T *value = nullptr) noexcept {
		if (_object != value) {
			destroy();
			_object = value;
		}
	}

	T *get() const noexcept {
		return static_cast<T*>(_object.data());
	}
	operator T*() const noexcept {
		return get();
	}

	T *release() noexcept {
		return static_cast<T*>(base::take(_object).data());
	}

	explicit operator bool() const noexcept {
		return _object != nullptr;
	}

	T *operator->() const noexcept {
		return get();
	}
	T &operator*() const noexcept {
		return *get();
	}

	~unique_qptr() noexcept {
		destroy();
	}

private:
	void destroy() noexcept {
		delete base::take(_object).data();
	}

	template <typename U>
	friend class unique_qptr;

	QPointer<QObject> _object;

};

template <typename T, typename ...Args>
inline unique_qptr<T> make_unique_q(Args &&...args) {
	return unique_qptr<T>(std::in_place, std::forward<Args>(args)...);
}

} // namespace base
