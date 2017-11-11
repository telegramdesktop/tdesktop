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
namespace details {

template <typename A, typename B>
struct supports_equality_compare {
	template <typename U, typename V>
	static auto test(const U *u, const V *v)
		-> decltype(*u == *v, details::true_t());
	static details::false_t test(...);
	static constexpr bool value
		= (sizeof(test(
		(std::decay_t<A>*)nullptr,
			(std::decay_t<B>*)nullptr
		)) == sizeof(details::true_t));
};

template <typename A, typename B>
constexpr bool supports_equality_compare_v
	= supports_equality_compare<A, B>::value;

} // namespace details

template <typename Type>
class variable final {
public:
	variable() : _data{} {
	}

	template <
		typename OtherType,
		typename = std::enable_if_t<
			std::is_constructible_v<Type, OtherType&&>>>
	variable(OtherType &&data) : _data(std::forward<OtherType>(data)) {
	}

	template <
		typename OtherType,
		typename = std::enable_if_t<
			std::is_assignable_v<Type&, OtherType&&>>>
	variable &operator=(OtherType &&data) {
		_lifetime.destroy();
		return assign(std::forward<OtherType>(data));
	}

	template <
		typename OtherType,
		typename Error,
		typename Generator,
		typename = std::enable_if_t<
			std::is_assignable_v<Type&, OtherType>>>
	variable(producer<OtherType, Error, Generator> &&stream) {
		std::move(stream)
			| start_with_next([this](auto &&data) {
				assign(std::forward<decltype(data)>(data));
			}, _lifetime);
	}

	template <
		typename OtherType,
		typename Error,
		typename Generator,
		typename = std::enable_if_t<
			std::is_assignable_v<Type&, OtherType>>>
	variable &operator=(
			producer<OtherType, Error, Generator> &&stream) {
		_lifetime.destroy();
		std::move(stream)
			| start_with_next([this](auto &&data) {
				assign(std::forward<decltype(data)>(data));
			}, _lifetime);
	}

	Type current() const {
		return _data;
	}
	auto value() const {
		return _changes.events_starting_with_copy(_data);
	}
	auto changes() const {
		return _changes.events();
	}

private:
	template <typename OtherType>
	variable &assign(OtherType &&data) {
		if constexpr (details::supports_equality_compare_v<Type, OtherType>) {
			if (!(_data == data)) {
				_data = std::forward<OtherType>(data);
				_changes.fire_copy(_data);
			}
		} else if constexpr (details::supports_equality_compare_v<Type, Type>) {
			auto old = std::move(_data);
			_data = std::forward<OtherType>(data);
			if (!(_data == old)) {
				_changes.fire_copy(_data);
			}
		} else {
			_data = std::forward<OtherType>(data);
			_changes.fire_copy(_data);
		}
		return *this;
	}

	Type _data;
	event_stream<Type> _changes;
	lifetime _lifetime;

};

} // namespace rpl
