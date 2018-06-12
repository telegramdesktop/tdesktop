/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include <vector>

namespace rpl {

template <typename Value>
inline auto single(Value &&value) {
	return make_producer<std::decay_t<Value>>([
		value = std::forward<Value>(value)
	](const auto &consumer) mutable {
		consumer.put_next(std::move(value));
		consumer.put_done();
		return lifetime();
	});
}

inline auto single() {
	return make_producer<>([](const auto &consumer) {
		consumer.put_next({});
		consumer.put_done();
		return lifetime();
	});
}

template <typename Value>
inline auto vector(std::vector<Value> &&values) {
	return make_producer<Value>([
		values = std::move(values)
	](const auto &consumer) mutable {
		for (auto &value : values) {
			consumer.put_next(std::move(value));
		}
		consumer.put_done();
		return lifetime();
	});
}

inline auto vector(std::vector<bool> &&values) {
	return make_producer<bool>([
		values = std::move(values)
	](const auto &consumer) {
		for (auto value : values) {
			consumer.put_next_copy(value);
		}
		consumer.put_done();
		return lifetime();
	});
}

template <
	typename Range,
	typename Value = std::decay_t<
		decltype(*std::begin(std::declval<Range>()))>>
inline auto range(Range &&range) {
	return vector(std::vector<Value>(
		std::begin(range),
		std::end(range)));
}

inline auto ints(int from, int till) {
	Expects(from <= till);
	return make_producer<int>([from, till](const auto &consumer) {
		for (auto i = from; i != till; ++i) {
			consumer.put_next_copy(i);
		}
		consumer.put_done();
		return lifetime();
	});
}

inline auto ints(int count) {
	return ints(0, count);
}

} // namespace rpl

