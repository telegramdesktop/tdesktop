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
	using namespace rpl::details;
	if constexpr (is_callable_plain_v<Method, Data&&>) {
		return std::forward<Method>(method)(std::forward<Data>(data));
	} else {
		return match_method(
			std::forward<Data>(data),
			std::forward<Methods>(methods)...);
	}
}

template <
	typename Data1,
	typename Data2,
	typename Method,
	typename ...Methods>
inline decltype(auto) match_method2(
		Data1 &&data1,
		Data2 &&data2,
		Method &&method,
		Methods &&...methods) {
	using namespace rpl::details;
	if constexpr (is_callable_plain_v<Method, Data1&&, Data2&&>) {
		return std::forward<Method>(method)(
			std::forward<Data1>(data1),
			std::forward<Data2>(data2));
	} else {
		return match_method2(
			std::forward<Data1>(data1),
			std::forward<Data2>(data2),
			std::forward<Methods>(methods)...);
	}
}

} // namespace base
