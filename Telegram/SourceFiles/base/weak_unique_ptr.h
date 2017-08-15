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

class enable_weak_from_this;

template <typename T>
class weak_unique_ptr;

class enable_weak_from_this {
public:
	enable_weak_from_this() = default;
	enable_weak_from_this(const enable_weak_from_this &other) noexcept {
	}
	enable_weak_from_this(enable_weak_from_this &&other) noexcept {
	}
	enable_weak_from_this &operator=(const enable_weak_from_this &other) noexcept {
		return *this;
	}
	enable_weak_from_this &operator=(enable_weak_from_this &&other) noexcept {
		return *this;
	}

private:
	template <typename Child>
	friend class weak_unique_ptr;

	std::shared_ptr<enable_weak_from_this*> getGuarded() {
		if (!_guarded) {
			_guarded = std::make_shared<enable_weak_from_this*>(static_cast<enable_weak_from_this*>(this));
		}
		return _guarded;
	}

	std::shared_ptr<enable_weak_from_this*> _guarded;

};

template <typename T>
class weak_unique_ptr {
public:
	weak_unique_ptr() = default;
	weak_unique_ptr(T *value) : _guarded(value ? value->getGuarded() : std::shared_ptr<enable_weak_from_this*>()) {
	}
	weak_unique_ptr(const std::unique_ptr<T> &value) : weak_unique_ptr(value.get()) {
	}

	weak_unique_ptr &operator=(T *value) {
		_guarded = value ? value->getGuarded() : std::shared_ptr<enable_weak_from_this*>();
		return *this;
	}
	weak_unique_ptr &operator=(const std::unique_ptr<T> &value) {
		return (*this = value.get());
	}

	T *get() const noexcept {
		if (auto shared = _guarded.lock()) {
			return static_cast<T*>(*shared);
		}
		return nullptr;
	}
	explicit operator bool() const noexcept {
		return !!_guarded.lock();
	}
	T &operator*() const noexcept {
		return *get();
	}
	T *operator->() const noexcept {
		return get();
	}

private:
	std::weak_ptr<enable_weak_from_this*> _guarded;

};

template <typename T>
inline bool operator==(const weak_unique_ptr<T> &pointer, std::nullptr_t) {
	return (pointer.get() == nullptr);
}

template <typename T>
inline bool operator==(std::nullptr_t, const weak_unique_ptr<T> &pointer) {
	return (pointer == nullptr);
}

template <typename T>
inline bool operator!=(const weak_unique_ptr<T> &pointer, std::nullptr_t) {
	return !(pointer == nullptr);
}

template <typename T>
inline bool operator!=(std::nullptr_t, const weak_unique_ptr<T> &pointer) {
	return !(pointer == nullptr);
}

template <typename T>
weak_unique_ptr<T> make_weak_unique(T *value) {
	return weak_unique_ptr<T>(value);
}

template <typename T>
weak_unique_ptr<T> make_weak_unique(const std::unique_ptr<T> &value) {
	return weak_unique_ptr<T>(value);
}

} // namespace base

#ifdef QT_VERSION
template <typename Lambda>
inline void InvokeQueued(base::enable_weak_from_this *context, Lambda &&lambda) {
	QObject proxy;
	QObject::connect(&proxy, &QObject::destroyed, QCoreApplication::instance(), [guard = base::make_weak_unique(context), lambda = std::forward<Lambda>(lambda)] {
		if (guard) {
			lambda();
		}
	}, Qt::QueuedConnection);
}
#endif // QT_VERSION
