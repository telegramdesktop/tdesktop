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

#include <rpl/producer.h>
#include <rpl/event_stream.h>

namespace rpl {

template <typename Type>
class variable {
public:
	variable() : _data{} {
	}
	variable(const variable &other) = default;
	variable(variable &&other) = default;
	variable &operator=(const variable &other) = default;
	variable &operator=(variable &&other) = default;

	variable(const Type &data) : _data(data) {
	}
	variable(Type &&data) : _data(std::move(data)) {
	}
	variable &operator=(const Type &data) {
		return assign(data);
	}

	template <
		typename OtherType,
		typename = std::enable_if_t<
			std::is_constructible_v<Type, OtherType&&>
			&& !std::is_same_v<std::decay_t<OtherType>, Type>>>
	variable(OtherType &&data) : _data(std::forward<OtherType>(data)) {
	}

	template <
		typename OtherType,
		typename = std::enable_if_t<
			std::is_assignable_v<Type, OtherType&&>
			&& !std::is_same_v<std::decay_t<OtherType>, Type>>>
	variable &operator=(OtherType &&data) {
		return assign(std::forward<OtherType>(data));
	}

	template <
		typename OtherType,
		typename = std::enable_if_t<
			std::is_assignable_v<Type, OtherType>>>
	variable(rpl::producer<OtherType> &&stream) {
		std::move(stream)
			| start_with_next([this](auto &&data) {
				*this = std::forward<decltype(data)>(data);
			}, _lifetime);
	}

	template <
		typename OtherType,
		typename = std::enable_if_t<
			std::is_assignable_v<Type, OtherType>>>
	variable &operator=(rpl::producer<OtherType> &&stream) {
		_lifetime.destroy();
		std::move(stream)
			| start_with_next([this](auto &&data) {
				*this = std::forward<decltype(data)>(data);
			}, _lifetime);
	}

	Type current() const {
		return _data;
	}
	rpl::producer<Type> value() const {
		return _changes.events_starting_with_copy(_data);
	}

private:
	template <typename OtherType>
	variable &assign(OtherType &&data) {
		_lifetime.destroy();
		_data = std::forward<OtherType>(data);
		_changes.fire_copy(_data);
		return *this;
	}

	Type _data;
	rpl::event_stream<Type> _changes;
	rpl::lifetime _lifetime;

};

} // namespace rpl
