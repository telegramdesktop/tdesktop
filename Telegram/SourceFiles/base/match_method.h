/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/details/callable.h>

namespace base {

template <typename Data, typename Method, typename ...Methods>
inline decltype(auto) match_method(
		Data &&data,
		Method &&method,
		Methods &&...methods) {
	if constexpr (rpl::details::is_callable_plain_v<Method, Data&&>) {
		return std::forward<Method>(method)(std::forward<Data>(data));
	} else {
		return match_method(
			std::forward<Data>(data),
			std::forward<Methods>(methods)...);
	}
}

} // namespace base
