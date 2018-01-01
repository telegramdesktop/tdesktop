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

namespace rpl {
namespace details {

class take_helper {
public:
	take_helper(int count) : _count(count) {
	}

	template <
		typename Value,
		typename Error,
		typename Generator>
	auto operator()(producer<Value, Error, Generator> &&initial) {
		return make_producer<Value, Error>([
			initial = std::move(initial),
			limit = _count
		](const auto &consumer) mutable {
			auto count = consumer.template make_state<int>(limit);
			auto initial_consumer = make_consumer<Value, Error>(
			[consumer, count](auto &&value) {
				auto left = (*count)--;
				if (left) {
					consumer.put_next_forward(
						std::forward<decltype(value)>(value));
					--left;
				}
				if (!left) {
					consumer.put_done();
				}
			}, [consumer](auto &&error) {
				consumer.put_error_forward(
					std::forward<decltype(error)>(error));
			}, [consumer] {
				consumer.put_done();
			});
			consumer.add_lifetime(initial_consumer.terminator());
			return std::move(initial).start_existing(initial_consumer);
		});
	}

private:
	int _count = 0;

};

} // namespace details
inline auto take(int count)
-> details::take_helper {
	Expects(count >= 0);
	return details::take_helper(count);
}


} // namespace rpl
