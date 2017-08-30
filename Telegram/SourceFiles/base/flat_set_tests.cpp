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

#include "base/flat_set.h"

TEST_CASE("flat_sets should keep items sorted", "[flat_set]") {
	base::flat_set<int> v;
	v.insert(0);
	v.insert(5);
	v.insert(4);
	v.insert(2);

	auto checkSorted = [&] {
		auto prev = v.begin();
		REQUIRE(prev != v.end());
		for (auto i = prev + 1; i != v.end(); prev = i, ++i) {
			REQUIRE(*prev < *i);
		}
	};
	REQUIRE(v.size() == 4);
	checkSorted();

	SECTION("adding item puts it in the right position") {
		v.insert(3);
		REQUIRE(v.size() == 5);
		REQUIRE(v.find(3) != v.end());
		checkSorted();
	}
}
