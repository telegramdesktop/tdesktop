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
#include "base/optional.h"

namespace rpl {
namespace details {

class distinct_until_changed_helper {
public:
	template <typename Value, typename Error, typename Generator>
	auto operator()(
			producer<Value, Error, Generator> &&initial) const {
		return make_producer<Value, Error>([
			initial = std::move(initial)
		](const auto &consumer) mutable {
			auto previous = consumer.template make_state<
				base::optional<Value>
			>();
			return std::move(initial).start(
				[consumer, previous](auto &&value) {
					if (!(*previous) || (**previous) != value) {
						*previous = value;
						consumer.put_next_forward(std::forward<decltype(value)>(value));
					}
				}, [consumer](auto &&error) {
					consumer.put_error_forward(std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				});
		});
	}

};

} // namespace details

inline auto distinct_until_changed()
-> details::distinct_until_changed_helper {
	return details::distinct_until_changed_helper();
}

} // namespace rpl
