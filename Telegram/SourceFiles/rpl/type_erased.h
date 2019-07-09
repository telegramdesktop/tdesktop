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

class type_erased_helper {
public:
	template <typename Value, typename Error, typename Generator>
	producer<Value, Error> operator()(
			producer<Value, Error, Generator> &&initial) const {
		return std::move(initial);
	}

};

} // namespace details

inline auto type_erased()
-> details::type_erased_helper {
	return details::type_erased_helper();
}

} // namespace rpl
