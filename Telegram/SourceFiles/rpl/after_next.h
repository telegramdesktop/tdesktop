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

template <typename SideEffect>
class after_next_helper {
public:
	template <typename OtherSideEffect>
	after_next_helper(OtherSideEffect &&method)
		: _method(std::forward<OtherSideEffect>(method)) {
	}

	template <typename Value, typename Error, typename Generator>
	auto operator()(producer<Value, Error, Generator> &&initial) {
		return make_producer<Value, Error>([
			initial = std::move(initial),
			method = std::move(_method)
		](const auto &consumer) mutable {
			return std::move(initial).start(
			[method = std::move(method), consumer](auto &&value) {
				auto copy = method;
				consumer.put_next_copy(value);
				std::move(copy)(
					std::forward<decltype(value)>(value));
			}, [consumer](auto &&error) {
				consumer.put_error_forward(
					std::forward<decltype(error)>(error));
			}, [consumer] {
				consumer.put_done();
			});
		});
	}

private:
	SideEffect _method;

};

} // namespace details

template <typename SideEffect>
inline auto after_next(SideEffect &&method)
-> details::after_next_helper<std::decay_t<SideEffect>> {
	return details::after_next_helper<std::decay_t<SideEffect>>(
		std::forward<SideEffect>(method));
}

} // namespace rpl
