/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace rpl {

template <typename Value1, typename Value2>
struct superset_type;

template <typename Value1, typename Value2>
using superset_type_t = typename superset_type<Value1, Value2>::type;

template <typename Value>
struct superset_type<Value, Value> {
	using type = Value;
};

} // namespace rpl
