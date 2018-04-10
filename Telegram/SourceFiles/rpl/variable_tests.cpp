/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "catch.hpp"

#include <rpl/rpl.h>
#include <string>

using namespace rpl;

TEST_CASE("basic variable tests", "[rpl::variable]") {
	SECTION("simple test") {
		auto sum = std::make_shared<int>(0);
		{
			auto var = variable<int>(1);
			auto lifeftime = var.value()
				| start_with_next([=](int value) {
					*sum += value;
				});
			var = 1;
			var = 11;
			var = 111;
			var = 111;
		}
		REQUIRE(*sum == 1 + 11 + 111);
	}
}

