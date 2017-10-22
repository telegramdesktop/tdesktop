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

