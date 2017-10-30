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
	unique_qptr(std::nullptr_t) {
	}
	explicit unique_qptr(T *pointer)
	: _object(pointer) {
	}

	unique_qptr(const unique_qptr &other) = delete;
	unique_qptr &operator=(const unique_qptr &other) = delete;
	unique_qptr(unique_qptr &&other)
	: _object(base::take(other._object)) {
	}
	unique_qptr &operator=(unique_qptr &&other) {
		if (_object != other._object) {
			destroy();
			_object = std::move(other._object);
		}
		return *this;
	}

	template <
		typename U,
		typename = std::enable_if_t<std::is_base_of_v<T, U>>>
	unique_qptr(unique_qptr<U> &&other)
	: _object(base::take(other._object)) {
	}

	template <
		typename U,
		typename = std::enable_if_t<std::is_base_of_v<T, U>>>
	unique_qptr &operator=(unique_qptr<U> &&other) {
		if (_object != other._object) {
			destroy();
			_object = std::move(other._object);
		}
		return *this;
	}

	unique_qptr &operator=(std::nullptr_t) {
		destroy();
		_object = nullptr;
		return *this;
	}

	void reset(T *value) {
		if (_object != value) {
			destroy();
			_object = value;
		}
	}

	T *get() const {
		return static_cast<T*>(_object.data());
	}
	operator T*() const {
		return get();
	}

	T *release() {
		return static_cast<T*>(base::take(_object).data());
	}

	explicit operator bool() const {
		return _object != nullptr;
	}

	T *operator->() const {
		return get();
	}
	T &operator*() const {
		return *get();
	}

	void destroy() {
		delete base::take(_object).data();
	}

	~unique_qptr() {
		destroy();
	}

private:
	template <typename U>
	friend class unique_qptr;

	QPointer<QObject> _object;

};

template <typename T, typename ...Args>
inline unique_qptr<T> make_unique_q(Args &&...args) {
	return unique_qptr<T>(new T(std::forward<Args>(args)...));
}

} // namespace base
