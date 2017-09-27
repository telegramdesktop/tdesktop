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

namespace rpl {
namespace details {

template <typename Value, typename Error, typename Generator>
class then_helper {
public:
	then_helper(producer<Value, Error, Generator> &&following)
	: _following(std::move(following)) {
	}

	template <
		typename OtherValue,
		typename OtherError,
		typename OtherGenerator,
		typename NewValue = superset_type_t<Value, OtherValue>,
		typename NewError = superset_type_t<Error, OtherError>>
	auto operator()(
		producer<OtherValue, OtherError, OtherGenerator> &&initial
	) {
		return make_producer<NewValue, NewError>([
			initial = std::move(initial),
			following = std::move(_following)
		](const auto &consumer) mutable {
			return std::move(initial).start(
			[consumer](auto &&value) {
				consumer.put_next_forward(
					std::forward<decltype(value)>(value));
			}, [consumer](auto &&error) {
				consumer.put_error_forward(
					std::forward<decltype(error)>(error));
			}, [
				consumer,
				following = std::move(following)
			]() mutable {
				consumer.add_lifetime(std::move(following).start(
				[consumer](auto &&value) {
					consumer.put_next_forward(
						std::forward<decltype(value)>(value));
				}, [consumer](auto &&error) {
					consumer.put_error_forward(
						std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				}));
			});
		});
	}

private:
	producer<Value, Error, Generator> _following;

};

} // namespace details
template <typename Value, typename Error, typename Generator>
inline auto then(producer<Value, Error, Generator> &&following)
-> details::then_helper<Value, Error, Generator> {
	return { std::move(following) };
}

} // namespace rpl
