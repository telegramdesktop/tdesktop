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
#include <rpl/complete.h>
#include "base/optional.h"

namespace rpl {
namespace details {

template <typename Value>
struct combine_latest_vector_state {
	std::vector<base::optional<Value>> accumulated;
	std::vector<Value> latest;
	int invalid = 0;
	int working = 0;
};

} // namespace details

template <typename Value, typename Error>
producer<std::vector<Value>, Error> combine_latest(
		std::vector<producer<Value, Error>> &&producers) {
	if (producers.empty()) {
		return complete<std::vector<Value>, Error>();
	}

	using state_type = details::combine_latest_vector_state<Value>;
	using consumer_type = consumer<std::vector<Value>, Error>;
	return [producers = std::move(producers)](
			const consumer_type &consumer) mutable {
		auto count = producers.size();
		auto state = consumer.make_state<state_type>();
		state->accumulated.resize(count);
		state->invalid = count;
		state->working = count;
		for (auto index = 0; index != count; ++index) {
			auto &producer = producers[index];
			consumer.add_lifetime(std::move(producer).start(
			[consumer, state, index](Value &&value) {
				if (state->accumulated.empty()) {
					state->latest[index] = std::move(value);
					consumer.put_next_copy(state->latest);
				} else if (state->accumulated[index]) {
					state->accumulated[index] = std::move(value);
				} else {
					state->accumulated[index] = std::move(value);
					if (!--state->invalid) {
						state->latest.reserve(
							state->accumulated.size());
						for (auto &&value : state->accumulated) {
							state->latest.push_back(
								std::move(*value));
						}
						base::take(state->accumulated);
						consumer.put_next_copy(state->latest);
					}
				}
			}, [consumer](Error &&error) {
				consumer.put_error(std::move(error));
			}, [consumer, state] {
				if (!--state->working) {
					consumer.put_done();
				}
			}));
		}
		return lifetime();
	};
}

namespace details {

template <typename Value, typename ...Values>
struct combine_latest_tuple_state {
	base::optional<Value> first;
	base::optional<std::tuple<Values...>> others;
	int working = 2;
};

} // namespace details

template <
	typename Value,
	typename Error,
	typename ...Values,
	typename ...Errors>
producer<std::tuple<Value, Values...>, base::variant<Error, Errors...>> combine_latest(
		producer<Value, Error> &&first,
		producer<Values, Errors> &&...others) {
	auto others_combined = combine_latest(std::move(others)...);
	return [
		first = std::move(first),
		others = std::move(others_combined)
	](const consumer<std::tuple<Value, Values...>, base::variant<Error, Errors...>> &consumer) mutable {
		auto state = consumer.make_state<details::combine_latest_tuple_state<Value, Values...>>();
		consumer.add_lifetime(std::move(first).start([consumer, state](Value &&value) {
			state->first = std::move(value);
			if (state->others) {
				consumer.put_next(std::tuple_cat(std::make_tuple(*state->first), *state->others));
			}
		}, [consumer](Error &&error) {
			consumer.put_error(std::move(error));
		}, [consumer, state] {
			if (!--state->working) {
				consumer.put_done();
			}
		}));
		consumer.add_lifetime(std::move(others).start([consumer, state](std::tuple<Values...> &&value) {
			state->others = std::move(value);
			if (state->first) {
				consumer.put_next(std::tuple_cat(std::make_tuple(*state->first), *state->others));
			}
		}, [consumer](base::variant<Errors...> &&error) {
			base::visit([&](auto &&errorValue) {
				consumer.put_error(std::move(errorValue));
			}, std::move(error));
		}, [consumer, state] {
			if (!--state->working) {
				consumer.put_done();
			}
		}));
		return lifetime();
	};
}

template <
	typename Value,
	typename Error>
producer<std::tuple<Value>, Error> combine_latest(
		producer<Value, Error> &&producer) {
	return std::move(producer) | map([](auto &&value) {
		return std::make_tuple(std::forward<decltype(value)>(value));
	});
}

} // namespace rpl
