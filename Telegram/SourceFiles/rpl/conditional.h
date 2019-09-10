/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/optional.h"
#include "base/variant.h"
#include <rpl/map.h>
#include <rpl/producer.h>

namespace rpl {

template <
	typename Value,
	typename Error,
	typename GeneratorTest,
	typename GeneratorA,
	typename GeneratorB>
inline auto conditional(
		rpl::producer<bool, Error, GeneratorTest> &&test,
		rpl::producer<Value, Error, GeneratorA> &&a,
		rpl::producer<Value, Error, GeneratorB> &&b) {
	return rpl::combine(
		std::move(test),
		std::move(a),
		std::move(b)
	) | rpl::map([](bool test, Value &&a, Value &&b) {
		return test ? std::move(a) : std::move(b);
	});
	//struct conditional_state {
	//	std::optional<Value> a;
	//	std::optional<Value> b;
	//	char state = -1;
	//	int working = 3;
	//};
	//return rpl::make_producer<Value, Error>([
	//	test = std::move(test),
	//	a = std::move(a),
	//	b = std::move(b)
	//](const auto &consumer) mutable {
	//	auto result = lifetime();
	//	const auto state = result.make_state<conditional_state>();
	//	result.add(std::move(test).start())
	//	return result;
	//});
}

} // namespace rpl
