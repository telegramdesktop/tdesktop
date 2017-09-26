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

#include "base/optional.h"
#include <rpl/map.h>
#include <rpl/producer.h>
#include <rpl/details/type_list.h>
#include <rpl/details/callable.h>
#include <rpl/mappers.h>
#include <rpl/complete.h>

namespace rpl {
namespace details {

template <typename ...Values>
struct combine_state {
	combine_state() : accumulated(std::tuple<base::optional<Values>...>()) {
	}
	base::optional<std::tuple<base::optional<Values>...>> accumulated;
	base::optional<std::tuple<Values...>> latest;
	int invalid = sizeof...(Values);
	int working = sizeof...(Values);
};

template <typename ...Values, std::size_t ...I>
inline std::tuple<Values...> combine_make_first(
		std::tuple<base::optional<Values>...> &&accumulated,
		std::index_sequence<I...>) {
	return std::make_tuple(std::move(*std::get<I>(accumulated))...);
}

template <size_t Index, typename consumer_type, typename ...Values>
class combine_subscribe_one {
public:
	combine_subscribe_one(
		const consumer_type &consumer,
		combine_state<Values...> *state)
	: _consumer(consumer)
	, _state(state) {
	}

	template <typename Value, typename Error>
	void subscribe(producer<Value, Error> &&producer) {
		_consumer.add_lifetime(std::move(producer).start(
			[consumer = _consumer, state = _state](Value &&value) {
			if (!state->accumulated) {
				std::get<Index>(*state->latest) = std::move(value);
				consumer.put_next_copy(*state->latest);
			} else {
				auto &accumulated = std::get<Index>(
					*state->accumulated);
				if (accumulated) {
					accumulated = std::move(value);
				} else {
					accumulated = std::move(value);
					if (!--state->invalid) {
						constexpr auto kArity = sizeof...(Values);
						state->latest = combine_make_first(
							std::move(*state->accumulated),
							std::make_index_sequence<kArity>());
						state->accumulated = base::none;
						consumer.put_next_copy(*state->latest);
					}
				}
			}
		}, [consumer = _consumer](Error &&error) {
			consumer.put_error(std::move(error));
		}, [consumer = _consumer, state = _state] {
			if (!--state->working) {
				consumer.put_done();
			}
		}));
	}

private:
	const consumer_type &_consumer;
	combine_state<Values...> *_state = nullptr;

};

template <
	typename consumer_type,
	typename ...Values,
	typename ...Errors,
	std::size_t ...I>
inline void combine_subscribe(
		const consumer_type &consumer,
		combine_state<Values...> *state,
		std::index_sequence<I...>,
		producer<Values, Errors> &&...producers) {
	auto consume = { (
		details::combine_subscribe_one<
			I,
			consumer_type,
			Values...
		>(
			consumer,
			state
		).subscribe(std::move(producers)), 0)... };
	(void)consume;
}

template <typename ...Values, typename ...Errors>
inline auto combine_implementation(
		producer<Values, Errors> &&...producers) {
	using CombinedError = details::normalized_variant_t<Errors...>;
	using Result = producer<
		std::tuple<Values...>,
		CombinedError>;
	using consumer_type = typename Result::consumer_type;
	auto result = [](
			const consumer_type &consumer,
			producer<Values, Errors> &...producers) {
		auto state = consumer.template make_state<
			details::combine_state<Values...>>();

		constexpr auto kArity = sizeof...(Values);
		details::combine_subscribe(
			consumer,
			state,
			std::make_index_sequence<kArity>(),
			std::move(producers)...);

		return lifetime();
	};
	return Result(std::bind(
		result,
		std::placeholders::_1,
		std::move(producers)...));
}

template <typename ...Args>
struct combine_just_producers : std::false_type {
};

template <typename ...Args>
constexpr bool combine_just_producers_v
	= combine_just_producers<Args...>::value;

template <typename ...Values, typename ...Errors>
struct combine_just_producers<producer<Values, Errors>...>
	: std::true_type {
};

template <typename ArgsList>
struct combine_just_producers_list
	: type_list::extract_to_t<ArgsList, combine_just_producers> {
};

template <typename ...Args>
struct combine_result_type;

template <typename ...Args>
using combine_result_type_t
	= typename combine_result_type<Args...>::type;

template <typename ...Values, typename ...Errors>
struct combine_result_type<producer<Values, Errors>...> {
	using type = std::tuple<Values...>;
};

template <typename ArgsList>
struct combine_result_type_list
	: type_list::extract_to_t<ArgsList, combine_result_type> {
};

template <typename ArgsList>
using combine_result_type_list_t
	= typename combine_result_type_list<ArgsList>::type;

template <typename ArgsList>
using combine_producers_no_mapper_t
	= type_list::chop_last_t<ArgsList>;

template <typename ArgsList>
constexpr bool combine_is_good_mapper(std::true_type) {
	return is_callable_v<
		type_list::last_t<ArgsList>,
		combine_result_type_list_t<
			combine_producers_no_mapper_t<ArgsList>
		>>;
}

template <typename ArgsList>
constexpr bool combine_is_good_mapper(std::false_type) {
	return false;
}

template <typename ArgsList>
struct combine_producers_with_mapper_list : std::bool_constant<
	combine_is_good_mapper<ArgsList>(
		combine_just_producers_list<
			combine_producers_no_mapper_t<ArgsList>
		>())> {
};

template <typename ...Args>
struct combine_producers_with_mapper
	: combine_producers_with_mapper_list<type_list::list<Args...>> {
};

template <typename ...Args>
constexpr bool combine_producers_with_mapper_v
	 = combine_producers_with_mapper<Args...>::value;

template <typename ...Values, typename ...Errors>
inline decltype(auto) combine_helper(
		std::true_type,
		producer<Values, Errors> &&...producers) {
	return combine_implementation(std::move(producers)...);
}

template <typename ...Producers, std::size_t ...I>
inline decltype(auto) combine_call(
		std::index_sequence<I...>,
		Producers &&...producers) {
	return combine_implementation(
		argument_mapper<I>::call(std::move(producers)...)...);
}

template <typename ...Args>
inline decltype(auto) combine_helper(
		std::false_type,
		Args &&...args) {
	constexpr auto kProducersCount = sizeof...(Args) - 1;
	return combine_call(
		std::make_index_sequence<kProducersCount>(),
		std::forward<Args>(args)...)
		| map(argument_mapper<kProducersCount>::call(
			std::forward<Args>(args)...));
}

} // namespace details

template <
	typename ...Args,
	typename = std::enable_if_t<
		details::combine_just_producers_v<Args...>
	|| details::combine_producers_with_mapper_v<Args...>>>
inline decltype(auto) combine(Args &&...args) {
	return details::combine_helper(
		details::combine_just_producers<Args...>(),
		std::forward<Args>(args)...);
}

namespace details {

template <typename Value>
struct combine_vector_state {
	std::vector<base::optional<Value>> accumulated;
	std::vector<Value> latest;
	int invalid = 0;
	int working = 0;
};

} // namespace details

template <typename Value, typename Error>
inline producer<std::vector<Value>, Error> combine(
		std::vector<producer<Value, Error>> &&producers) {
	if (producers.empty()) {
		return complete<std::vector<Value>, Error>();
	}

	using state_type = details::combine_vector_state<Value>;
	using consumer_type = consumer<std::vector<Value>, Error>;
	return [producers = std::move(producers)](
			const consumer_type &consumer) mutable {
		auto count = producers.size();
		auto state = consumer.template make_state<state_type>();
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

template <typename Value, typename Error, typename Mapper>
inline auto combine(
		std::vector<producer<Value, Error>> &&producers,
		Mapper &&mapper) {
	return combine(std::move(producers))
		| map(std::forward<Mapper>(mapper));
}

} // namespace rpl
