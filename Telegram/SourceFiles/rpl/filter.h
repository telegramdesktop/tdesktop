/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include <rpl/combine.h>
#include <rpl/mappers.h>
#include "base/optional.h"

namespace rpl {
namespace details {

template <typename Predicate>
class filter_helper {
public:
	template <typename OtherPredicate>
	filter_helper(OtherPredicate &&predicate)
		: _predicate(std::forward<OtherPredicate>(predicate)) {
	}

	template <
		typename Value,
		typename Error,
		typename Generator,
		typename = std::enable_if_t<
			details::is_callable_v<Predicate, Value>>>
	auto operator()(producer<Value, Error, Generator> &&initial) {
		return make_producer<Value, Error>([
			initial = std::move(initial),
			predicate = std::move(_predicate)
		](const auto &consumer) mutable {
			return std::move(initial).start(
				[
					consumer,
					predicate = std::move(predicate)
				](auto &&value) {
					const auto &immutable = value;
					if (details::callable_invoke(
						predicate,
						immutable)
					) {
						consumer.put_next_forward(
							std::forward<decltype(value)>(value));
					}
				}, [consumer](auto &&error) {
					consumer.put_error_forward(
						std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				});
		});
	}

private:
	Predicate _predicate;

};

} // namespace details

template <typename Predicate>
inline auto filter(Predicate &&predicate)
-> details::filter_helper<std::decay_t<Predicate>> {
	return details::filter_helper<std::decay_t<Predicate>>(
		std::forward<Predicate>(predicate));
}

namespace details {

template <typename FilterError, typename FilterGenerator>
class filter_helper<producer<bool, FilterError, FilterGenerator>> {
public:
	filter_helper(
		producer<bool, FilterError, FilterGenerator> &&filterer)
	: _filterer(std::move(filterer)) {
	}

	template <typename Value, typename Error, typename Generator>
	auto operator()(producer<Value, Error, Generator> &&initial) {
		using namespace mappers;
		return combine(std::move(initial), std::move(_filterer))
			| filter(_2)
			| map(_1_of_two);
	}

private:
	producer<bool, FilterError, FilterGenerator> _filterer;

};

template <typename Value>
inline const Value &deref_optional_helper(
		const base::optional<Value> &value) {
	return *value;
}

template <typename Value>
inline Value &&deref_optional_helper(
		base::optional<Value> &&value) {
	return std::move(*value);
}

class filter_optional_helper {
public:
	template <typename Value, typename Error, typename Generator>
	auto operator()(producer<
			base::optional<Value>,
			Error,
			Generator> &&initial) const {
		return make_producer<Value, Error>([
			initial = std::move(initial)
		](const auto &consumer) mutable {
			return std::move(initial).start(
				[consumer](auto &&value) {
					if (value) {
						consumer.put_next_forward(
							deref_optional_helper(
								std::forward<decltype(value)>(
									value)));
					}
				}, [consumer](auto &&error) {
					consumer.put_error_forward(
						std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				});
		});
	}

};

} // namespace details

inline auto filter_optional()
-> details::filter_optional_helper {
	return details::filter_optional_helper();
}

} // namespace rpl
