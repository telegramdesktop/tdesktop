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
