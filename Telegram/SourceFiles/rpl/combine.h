/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	template <typename Value, typename Error, typename Generator>
	void subscribe(producer<Value, Error, Generator> &&producer) {
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
		}, [consumer = _consumer](auto &&error) {
			consumer.put_error_forward(
				std::forward<decltype(error)>(error));
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
	typename ...Generators,
	std::size_t ...I>
inline void combine_subscribe(
		const consumer_type &consumer,
		combine_state<Values...> *state,
		std::index_sequence<I...>,
		std::tuple<producer<Values, Errors, Generators>...> &&saved) {
	auto consume = { (
		combine_subscribe_one<I, consumer_type, Values...>(
			consumer,
			state
		).subscribe(std::get<I>(std::move(saved))), 0)... };
	(void)consume;
}

template <typename ...Producers>
class combine_implementation_helper;

template <typename ...Producers>
combine_implementation_helper<std::decay_t<Producers>...>
make_combine_implementation_helper(Producers &&...producers) {
	return combine_implementation_helper<std::decay_t<Producers>...>(
		std::forward<Producers>(producers)...);
}

template <
	typename ...Values,
	typename ...Errors,
	typename ...Generators>
class combine_implementation_helper<producer<Values, Errors, Generators>...> {
public:
	using CombinedValue = std::tuple<Values...>;
	using CombinedError = normalized_variant_t<Errors...>;

	combine_implementation_helper(
		producer<Values, Errors, Generators> &&...producers)
	: _saved(std::make_tuple(std::move(producers)...)) {
	}

	template <typename Handlers>
	lifetime operator()(const consumer<CombinedValue, CombinedError, Handlers> &consumer) {
		auto state = consumer.template make_state<
			combine_state<Values...>>();
		constexpr auto kArity = sizeof...(Values);
		combine_subscribe(
			consumer,
			state,
			std::make_index_sequence<kArity>(),
			std::move(_saved));

		return lifetime();
	}

private:
	std::tuple<producer<Values, Errors, Generators>...> _saved;

};

template <
	typename ...Values,
	typename ...Errors,
	typename ...Generators>
inline auto combine_implementation(
		producer<Values, Errors, Generators> &&...producers) {
	using CombinedValue = std::tuple<Values...>;
	using CombinedError = normalized_variant_t<Errors...>;

	return make_producer<CombinedValue, CombinedError>(
		make_combine_implementation_helper(std::move(producers)...));
}

template <typename ...Args>
struct combine_just_producers : std::false_type {
};

template <typename ...Args>
constexpr bool combine_just_producers_v
	= combine_just_producers<Args...>::value;

template <
	typename ...Values,
	typename ...Errors,
	typename ...Generators>
struct combine_just_producers<
		producer<Values, Errors, Generators>...>
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

template <
	typename ...Values,
	typename ...Errors,
	typename ...Generators>
struct combine_result_type<producer<Values, Errors, Generators>...> {
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

template <typename ...Producers, std::size_t ...I>
inline decltype(auto) combine_call(
		std::index_sequence<I...>,
		Producers &&...producers) {
	return combine_implementation(
		argument_mapper<I>::call(std::move(producers)...)...);
}

} // namespace details

template <
	typename ...Args,
	typename = std::enable_if_t<
		details::combine_just_producers_v<Args...>
	|| details::combine_producers_with_mapper_v<Args...>>>
inline decltype(auto) combine(Args &&...args) {
	if constexpr (details::combine_just_producers_v<Args...>) {
		return details::combine_implementation(std::move(args)...);
	} else if constexpr (details::combine_producers_with_mapper_v<Args...>) {
		constexpr auto kProducersCount = sizeof...(Args) - 1;
		return details::combine_call(
			std::make_index_sequence<kProducersCount>(),
			std::forward<Args>(args)...)
				| map(details::argument_mapper<kProducersCount>::call(
					std::forward<Args>(args)...));
	} else {
		static_assert(false_(args...), "Bad combine() call.");
	}
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

template <typename Value, typename Error, typename Generator>
inline auto combine(
		std::vector<producer<Value, Error, Generator>> &&producers) {
	using state_type = details::combine_vector_state<Value>;
	return make_producer<std::vector<Value>, Error>([
		producers = std::move(producers)
	](const auto &consumer) mutable {
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
						details::take(state->accumulated);
						consumer.put_next_copy(state->latest);
					}
				}
			}, [consumer](auto &&error) {
				consumer.put_error_forward(
					std::forward<decltype(error)>(error));
			}, [consumer, state] {
				if (!--state->working) {
					consumer.put_done();
				}
			}));
		}
		if (!count) {
			consumer.put_done();
		}
		return lifetime();
	});
}

template <
	typename Value,
	typename Error,
	typename Generator,
	typename Mapper>
inline auto combine(
		std::vector<producer<Value, Error, Generator>> &&producers,
		Mapper &&mapper) {
	return combine(std::move(producers))
		| map(std::forward<Mapper>(mapper));
}

} // namespace rpl
