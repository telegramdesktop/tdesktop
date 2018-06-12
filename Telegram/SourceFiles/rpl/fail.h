/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

namespace rpl {

template <typename Value, typename Error>
inline auto fail(Error &&error) {
	return make_producer<Value, std::decay_t<Error>>([
		error = std::forward<Error>(error)
	](const auto &consumer) mutable {
		consumer.put_error(std::move(error));
		return lifetime();
	});
}

} // namespace rpl
