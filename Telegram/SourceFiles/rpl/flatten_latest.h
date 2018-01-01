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

class flatten_latest_helper {
public:
	template <
		typename Value,
		typename Error,
		typename Generator,
		typename MetaGenerator>
	auto operator()(producer<
			producer<Value, Error, Generator>,
			Error,
			MetaGenerator> &&initial) const {
		return make_producer<Value, Error>([
			initial = std::move(initial)
		](const auto &consumer) mutable {
			auto state = consumer.template make_state<State>();
			return std::move(initial).start(
			[consumer, state](producer<Value, Error> &&inner) {
				state->finished = false;
				state->alive = lifetime();
				std::move(inner).start(
				[consumer](auto &&value) {
					consumer.put_next_forward(std::forward<decltype(value)>(value));
				}, [consumer](auto &&error) {
					consumer.put_error_forward(std::forward<decltype(error)>(error));
				}, [consumer, state] {
					if (state->finished) {
						consumer.put_done();
					} else {
						state->finished = true;
					}
				}, state->alive);
			}, [consumer](auto &&error) {
				consumer.put_error_forward(std::forward<decltype(error)>(error));
			}, [consumer, state] {
				if (state->finished) {
					consumer.put_done();
				} else {
					state->finished = true;
				}
			});
		});
	}

private:
	struct State {
		lifetime alive;
		bool finished = false;
	};

};

} // namespace details

inline auto flatten_latest()
-> details::flatten_latest_helper {
	return details::flatten_latest_helper();
}

} // namespace rpl

