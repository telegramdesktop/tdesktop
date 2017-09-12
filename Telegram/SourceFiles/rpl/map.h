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

template <typename Transform>
class map_helper {
public:
	template <typename OtherTransform>
	map_helper(OtherTransform &&transform)
		: _transform(std::forward<OtherTransform>(transform)) {
	}

	template <
		typename Value,
		typename Error,
		typename NewValue = decltype(
			std::declval<Transform>()(std::declval<Value>())
		)>
	rpl::producer<NewValue, Error> operator()(
			rpl::producer<Value, Error> &&initial) {
		return [
			initial = std::move(initial),
			transform = std::move(_transform)
		](const consumer<NewValue, Error> &consumer) mutable {
			return std::move(initial).start(
				[
					consumer,
					transform = std::move(transform)
				](Value &&value) {
				consumer.put_next(transform(std::move(value)));
			}, [consumer](Error &&error) {
				consumer.put_error(std::move(error));
			}, [consumer] {
				consumer.put_done();
			});
		};
	}

private:
	Transform _transform;

};

} // namespace details

template <typename Transform>
auto map(Transform &&transform)
-> details::map_helper<std::decay_t<Transform>> {
	return details::map_helper<std::decay_t<Transform>>(
		std::forward<Transform>(transform));
}

} // namespace rpl
