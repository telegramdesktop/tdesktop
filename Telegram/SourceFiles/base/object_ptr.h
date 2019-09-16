/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QPointer>

// Smart pointer for QObject*, has move semantics, destroys object if it doesn't have a parent.
template <typename Object>
class object_ptr {
public:
	object_ptr(std::nullptr_t) noexcept {
	}

	// No default constructor, but constructors with at least
	// one argument are simply make functions.
	template <typename Parent, typename... Args>
	explicit object_ptr(Parent &&parent, Args&&... args)
	: _object(new Object(std::forward<Parent>(parent), std::forward<Args>(args)...)) {
	}
	static object_ptr<Object> fromRaw(Object *value) noexcept {
		object_ptr<Object> result = { nullptr };
		result._object = value;
		return result;
	}
	Object *release() noexcept {
		return static_cast<Object*>(base::take(_object).data());
	}

	object_ptr(const object_ptr &other) = delete;
	object_ptr &operator=(const object_ptr &other) = delete;
	object_ptr(object_ptr &&other) noexcept : _object(base::take(other._object)) {
	}
	object_ptr &operator=(object_ptr &&other) noexcept {
		auto temp = std::move(other);
		destroy();
		std::swap(_object, temp._object);
		return *this;
	}

	template <
		typename OtherObject,
		typename = std::enable_if_t<
			std::is_base_of_v<Object, OtherObject>>>
	object_ptr(object_ptr<OtherObject> &&other) noexcept
	: _object(base::take(other._object)) {
	}

	template <
		typename OtherObject,
		typename = std::enable_if_t<
			std::is_base_of_v<Object, OtherObject>>>
	object_ptr &operator=(object_ptr<OtherObject> &&other) noexcept {
		_object = base::take(other._object);
		return *this;
	}

	object_ptr &operator=(std::nullptr_t) noexcept {
		_object = nullptr;
		return *this;
	}

	// So we can pass this pointer to methods like connect().
	Object *data() const noexcept {
		return static_cast<Object*>(_object.data());
	}
	operator Object*() const noexcept {
		return data();
	}

	explicit operator bool() const noexcept {
		return _object != nullptr;
	}

	Object *operator->() const noexcept {
		return data();
	}
	Object &operator*() const noexcept {
		return *data();
	}

	// Use that instead "= new Object(parent, ...)"
	template <typename Parent, typename... Args>
	Object *create(Parent &&parent, Args&&... args) {
		destroy();
		_object = new Object(
			std::forward<Parent>(parent),
			std::forward<Args>(args)...);
		return data();
	}
	void destroy() noexcept {
		delete base::take(_object);
	}
	void destroyDelayed() {
		if (_object) {
			if (auto widget = base::up_cast<QWidget*>(data())) {
				widget->hide();
			}
			base::take(_object)->deleteLater();
		}
	}

	~object_ptr() noexcept {
		if (auto pointer = _object) {
			if (!pointer->parent()) {
				destroy();
			}
		}
	}

private:
	template <typename OtherObject>
	friend class object_ptr;

	QPointer<QObject> _object;

};
