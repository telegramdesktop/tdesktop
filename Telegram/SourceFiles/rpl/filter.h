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
#include <rpl/combine.h>

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
		typename = std::enable_if_t<
			details::is_callable_v<Predicate, Value>>>
	rpl::producer<Value, Error> operator()(
			rpl::producer<Value, Error> &&initial) {
		return [
				initial = std::move(initial),
				predicate = std::move(_predicate)
			](
				const consumer<Value, Error> &consumer) mutable {
			return std::move(initial).start(
				[
					consumer,
					predicate = std::move(predicate)
				](auto &&value) {
					const auto &immutable = value;
					if (details::callable_invoke(predicate, immutable)) {
						consumer.put_next_forward(std::forward<decltype(value)>(value));
					}
				}, [consumer](auto &&error) {
					consumer.put_error_forward(std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				});
		};
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

template <>
class filter_helper<producer<bool>> {
public:
	filter_helper(producer<bool> &&filterer)
		: _filterer(std::move(filterer)) {
	}

	template <
		typename Value,
		typename Error>
	rpl::producer<Value, Error> operator()(
			rpl::producer<Value, Error> &&initial) {
		return combine(std::move(initial), std::move(_filterer))
			| filter([](auto &&value, bool let) { return let; })
			| map([](auto &&value, bool) {
				return std::forward<decltype(value)>(value);
			});
	}

private:
	producer<bool> _filterer;

};

} // namespace details
} // namespace rpl
