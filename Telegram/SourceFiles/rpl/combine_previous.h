/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include "base/optional.h"

namespace rpl {
namespace details {

class combine_previous_helper {
public:
	template <typename Value, typename Error, typename Generator>
	auto operator()(
			producer<Value, Error, Generator> &&initial) const {
		return make_producer<std::tuple<Value, Value>, Error>([
			initial = std::move(initial)
		](const auto &consumer) mutable {
			auto previous = consumer.template make_state<
				base::optional<Value>
			>();
			return std::move(initial).start(
				[consumer, previous](auto &&value) {
					if (auto exists = *previous) {
						auto &existing = *exists;
						auto next = std::make_tuple(
							std::move(existing),
							value);
						consumer.put_next(std::move(next));
						existing = std::forward<decltype(value)>(
							value);
					} else {
						*previous = std::forward<decltype(value)>(
							value);
					}
				}, [consumer](auto &&error) {
					consumer.put_error_forward(std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				});
		});
	}

};

template <typename DefaultValue>
class combine_previous_with_default_helper {
public:
	template <typename OtherValue>
	combine_previous_with_default_helper(OtherValue &&value)
		: _value(std::forward<OtherValue>(value)) {
	}

	template <typename Value, typename Error, typename Generator>
	auto operator()(producer<Value, Error, Generator> &&initial) {
		return make_producer<std::tuple<Value, Value>, Error>([
			initial = std::move(initial),
			value = Value(std::move(_value))
		](const auto &consumer) mutable {
			auto previous = consumer.template make_state<Value>(
				std::move(value));
			return std::move(initial).start(
				[consumer, previous](auto &&value) {
					auto &existing = *previous;
					auto next = std::make_tuple(
						std::move(existing),
						value);
					consumer.put_next(std::move(next));
					existing = std::forward<decltype(value)>(value);
				}, [consumer](auto &&error) {
					consumer.put_error_forward(std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				});
		});
	}

private:
	DefaultValue _value;

};

template <typename DefaultValue>
combine_previous_with_default_helper<std::decay_t<DefaultValue>>
combine_previous_with_default(DefaultValue &&value) {
	return { std::forward<DefaultValue>(value) };
}

} // namespace details

inline auto combine_previous()
-> details::combine_previous_helper {
	return details::combine_previous_helper();
}

template <typename DefaultValue>
inline auto combine_previous(DefaultValue &&value) {
	return details::combine_previous_with_default(
		std::forward<DefaultValue>(value));
}

} // namespace rpl
