/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include <rpl/event_stream.h>

namespace mapbox {
namespace util {

template <typename ...Types>
class variant;

} // namespace util
} // namespace mapbox

namespace rpl {
namespace details {

template <typename A, typename B>
struct supports_equality_compare {
	template <typename U, typename V>
	static auto test(const U *u, const V *v)
		-> decltype(*u == *v, true_t());
	static false_t test(...);
	static constexpr bool value
		= (sizeof(test((const A*)nullptr, (const B*)nullptr))
			== sizeof(true_t));
};

// Fix for MSVC expression SFINAE.
// It still doesn't work! :(
//
//template <typename Type1, typename ...Types1>
//struct supports_equality_compare<
//		mapbox::util::variant<Type1, Types1...>,
//		mapbox::util::variant<Type1, Types1...>> {
//	static constexpr bool value
//		= (supports_equality_compare<Type1, Type1>::value
//			&& supports_equality_compare<
//			mapbox::util::variant<Types1...>,
//			mapbox::util::variant<Types1...>>::value);
//
//};
//template <typename Type>
//struct supports_equality_compare<
//		mapbox::util::variant<Type>,
//		mapbox::util::variant<Type>> {
//	static constexpr bool value = supports_equality_compare<Type, Type>::value;
//
//};

template <typename A, typename B>
constexpr bool supports_equality_compare_v
	= supports_equality_compare<std::decay_t<A>, std::decay_t<B>>::value;

} // namespace details

template <typename Type>
class variable final {
public:
	variable() : _data{} {
	}
	variable(variable &&other) : _data(std::move(other._data)) {
	}
	variable &operator=(variable &&other) {
		return (*this = std::move(other._data));
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
