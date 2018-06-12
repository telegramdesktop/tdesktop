/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

namespace rpl {

template <typename Value = empty_value, typename Error = no_error>
inline auto complete() {
	return make_producer<Value, Error>([](const auto &consumer) {
		consumer.put_done();
		return lifetime();
	});
}

} // namespace rpl
