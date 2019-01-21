/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace rpl {
namespace details {

class skip_helper {
public:
	skip_helper(int count) : _count(count) {
	}

	template <
		typename Value,
		typename Error,
		typename Generator>
	auto operator()(producer<Value, Error, Generator> &&initial) {
		return make_producer<Value, Error>([
			initial = std::move(initial),
			skipping = _count
		](const auto &consumer) mutable {
			auto count = consumer.template make_state<int>(skipping);
			auto initial_consumer = make_consumer<Value, Error>(
			[consumer, count](auto &&value) {
				if (*count) {
					--*count;
				} else {
					consumer.put_next_forward(
						std::forward<decltype(value)>(value));
				}
			}, [consumer](auto &&error) {
				consumer.put_error_forward(
					std::forward<decltype(error)>(error));
			}, [consumer] {
				consumer.put_done();
			});
			consumer.add_lifetime(initial_consumer.terminator());
			return std::move(initial).start_existing(initial_consumer);
		});
	}

private:
	int _count = 0;

};

} // namespace details

inline auto skip(int count)
-> details::skip_helper {
	Expects(count >= 0);

	return details::skip_helper(count);
}

} // namespace rpl
