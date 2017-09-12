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

template <typename Value, typename Error>
auto then(producer<Value, Error> &&following) {
	return [following = std::move(following)](
			producer<Value, Error> &&initial) mutable
			-> producer<Value, Error> {
		return [
			initial = std::move(initial),
			following = std::move(following)
		](const consumer<Value, Error> &consumer) mutable {
			return std::move(initial).start(
			[consumer](Value &&value) {
				consumer.put_next(std::move(value));
			}, [consumer](Error &&error) {
				consumer.put_error(std::move(error));
			}, [
				consumer,
				following = std::move(following)
			]() mutable {
				consumer.add_lifetime(std::move(following).start(
				[consumer](Value &&value) {
					consumer.put_next(std::move(value));
				}, [consumer](Error &&error) {
					consumer.put_error(std::move(error));
				}, [consumer] {
					consumer.put_done();
				}));
			});
		};
	};
}

} // namespace rpl
