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

#include "base/flat_map.h"
#include <string>

using namespace std;

TEST_CASE("flat_maps should keep items sorted by key", "[flat_map]") {
	base::flat_map<int, string> v;
	v.emplace(0, "a");
	v.emplace(5, "b");
	v.emplace(4, "d");
	v.emplace(2, "e");

	auto checkSorted = [&] {
		auto prev = v.begin();
		REQUIRE(prev != v.end());
		for (auto i = prev + 1; i != v.end(); prev = i, ++i) {
			REQUIRE(prev->first < i->first);
		}
	};
	REQUIRE(v.size() == 4);
	checkSorted();

	SECTION("adding item puts it in the right position") {
		v.emplace(3, "c");
		REQUIRE(v.size() == 5);
		REQUIRE(v.find(3) != v.end());
		checkSorted();
	}
}
