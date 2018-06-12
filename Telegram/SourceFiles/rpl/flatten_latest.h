/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

