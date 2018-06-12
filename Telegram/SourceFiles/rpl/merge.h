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
	
struct merge_state {
	merge_state(int working) : working(working) {
	}
	int working = 0;
};

template <size_t Index, typename consumer_type>
class merge_subscribe_one {
public:
	merge_subscribe_one(
		const consumer_type &consumer,
		merge_state *state)
	: _consumer(consumer)
	, _state(state) {
	}

	template <typename Value, typename Error, typename Generator>
	void subscribe(producer<Value, Error, Generator> &&producer) {
		_consumer.add_lifetime(std::move(producer).start(
		[consumer = _consumer](auto &&value) {
			consumer.put_next_forward(
				std::forward<decltype(value)>(value));
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
	merge_state *_state = nullptr;

};

template <
	typename consumer_type,
	typename Value,
	typename Error,
	typename ...Generators,
	std::size_t ...I>
inline void merge_subscribe(
		const consumer_type &consumer,
		merge_state *state,
		std::index_sequence<I...>,
		std::tuple<producer<Value, Error, Generators>...> &&saved) {
	auto consume = { (
		details::merge_subscribe_one<I,	consumer_type>(
			consumer,
			state
		).subscribe(std::get<I>(std::move(saved))), 0)... };
	(void)consume;
}

template <typename ...Producers>
class merge_implementation_helper;

template <typename ...Producers>
merge_implementation_helper<std::decay_t<Producers>...>
make_merge_implementation_helper(Producers &&...producers) {
	return merge_implementation_helper<std::decay_t<Producers>...>(
		std::forward<Producers>(producers)...);
}

template <
	typename Value,
	typename Error,
	typename ...Generators>
class merge_implementation_helper<producer<Value, Error, Generators>...> {
public:
	merge_implementation_helper(
		producer<Value, Error, Generators> &&...producers)
	: _saved(std::make_tuple(std::move(producers)...)) {
	}

	template <typename Handlers>
	lifetime operator()(const consumer<Value, Error, Handlers> &consumer) {
		auto state = consumer.template make_state<
			details::merge_state>(sizeof...(Generators));
		constexpr auto kArity = sizeof...(Generators);
		details::merge_subscribe(
			consumer,
			state,
			std::make_index_sequence<kArity>(),
			std::move(_saved));

		return lifetime();
	}

private:
	std::tuple<producer<Value, Error, Generators>...> _saved;

};

template <
	typename Value,
	typename Error,
	typename ...Generators>
inline auto merge_implementation(
		producer<Value, Error, Generators> &&...producers) {
	return make_producer<Value, Error>(
		make_merge_implementation_helper(std::move(producers)...));
}

template <typename ...Args>
struct merge_producers : std::false_type {
};

template <typename ...Args>
constexpr bool merge_producers_v
	= merge_producers<Args...>::value;

template <
	typename Value,
	typename Error,
	typename ...Generators>
struct merge_producers<
		producer<Value, Error, Generators>...>
	: std::true_type {
};

} // namespace details

template <
	typename ...Args,
	typename = std::enable_if_t<
		details::merge_producers_v<Args...>>>
inline decltype(auto) merge(Args &&...args) {
	return details::merge_implementation(std::move(args)...);
}

} // namespace rpl
