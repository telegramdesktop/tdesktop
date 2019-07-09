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

template <typename Value, typename Error, typename Generator>
class then_helper {
public:
	then_helper(producer<Value, Error, Generator> &&following)
	: _following(std::move(following)) {
	}

	template <
		typename OtherValue,
		typename OtherError,
		typename OtherGenerator,
		typename NewValue = superset_type_t<Value, OtherValue>,
		typename NewError = superset_type_t<Error, OtherError>>
	auto operator()(
		producer<OtherValue, OtherError, OtherGenerator> &&initial
	) {
		return make_producer<NewValue, NewError>([
			initial = std::move(initial),
			following = std::move(_following)
		](const auto &consumer) mutable {
			return std::move(initial).start(
			[consumer](auto &&value) {
				consumer.put_next_forward(
					std::forward<decltype(value)>(value));
			}, [consumer](auto &&error) {
				consumer.put_error_forward(
					std::forward<decltype(error)>(error));
			}, [
				consumer,
				following = std::move(following)
			]() mutable {
				consumer.add_lifetime(std::move(following).start(
				[consumer](auto &&value) {
					consumer.put_next_forward(
						std::forward<decltype(value)>(value));
				}, [consumer](auto &&error) {
					consumer.put_error_forward(
						std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				}));
			});
		});
	}

private:
	producer<Value, Error, Generator> _following;

};

} // namespace details
template <typename Value, typename Error, typename Generator>
inline auto then(producer<Value, Error, Generator> &&following)
-> details::then_helper<Value, Error, Generator> {
	return { std::move(following) };
}

} // namespace rpl
