/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "catch.hpp"

#include "base/index_based_iterator.h"

TEST_CASE("index_based_iterator tests", "[base::algorithm]") {
	auto v = std::vector<int>();

	v.insert(v.end(), { 1, 2, 3, 4, 5, 4, 3, 2, 1 });
	auto push_back_safe_remove_if = [](auto &v, auto predicate) {
		auto begin = base::index_based_begin(v);
		auto end = base::index_based_end(v);
		auto from = std::remove_if(begin, end, predicate);
		if (from != end) {
			auto newEnd = base::index_based_end(v);
			if (newEnd != end) {
				REQUIRE(newEnd > end);
				while (end != newEnd) {
					*from++ = *end++;
				}
			}
			v.erase(from.base(), newEnd.base());
		}
	};
	SECTION("allows to push_back from predicate") {
		push_back_safe_remove_if(v, [&v](int value) {
			v.push_back(value);
			return (value % 2) == 1;
		});
		auto expected = std::vector<int> { 2, 4, 4, 2, 1, 2, 3, 4, 5, 4, 3, 2, 1 };
		REQUIRE(v == expected);
	}

	SECTION("allows to push_back while removing all") {
		push_back_safe_remove_if(v, [&v](int value) {
			if (value == 5) {
				v.push_back(value);
			}
			return true;
		});
		auto expected = std::vector<int> { 5 };
		REQUIRE(v == expected);
	}
}