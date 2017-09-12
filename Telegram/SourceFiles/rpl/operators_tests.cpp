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

class OnDestructor {
public:
	OnDestructor(base::lambda_once<void()> callback)
		: _callback(std::move(callback)) {
	}
	~OnDestructor() {
		if (_callback) {
			_callback();
		}
	}

private:
	base::lambda_once<void()> _callback;

};

class InvokeCounter {
public:
	InvokeCounter(
		const std::shared_ptr<int> &copyCounter,
		const std::shared_ptr<int> &moveCounter)
		: _copyCounter(copyCounter)
		, _moveCounter(moveCounter) {
	}
	InvokeCounter(const InvokeCounter &other)
		: _copyCounter(other._copyCounter)
		, _moveCounter(other._moveCounter) {
		if (_copyCounter) {
			++*_copyCounter;
		}
	}
	InvokeCounter(InvokeCounter &&other)
		: _copyCounter(base::take(other._copyCounter))
		, _moveCounter(base::take(other._moveCounter)) {
		if (_moveCounter) {
			++*_moveCounter;
		}
	}
	InvokeCounter &operator=(const InvokeCounter &other) {
		_copyCounter = other._copyCounter;
		_moveCounter = other._moveCounter;
		if (_copyCounter) {
			++*_copyCounter;
		}
	}
	InvokeCounter &operator=(InvokeCounter &&other) {
		_copyCounter = base::take(other._copyCounter);
		_moveCounter = base::take(other._moveCounter);
		if (_moveCounter) {
			++*_moveCounter;
		}
	}

private:
	std::shared_ptr<int> _copyCounter;
	std::shared_ptr<int> _moveCounter;

};

TEST_CASE("basic operators tests", "[rpl::operators]") {
	SECTION("single test") {
		auto sum = std::make_shared<int>(0);
		auto doneGenerated = std::make_shared<bool>(false);
		auto destroyed = std::make_shared<bool>(false);
		auto copyCount = std::make_shared<int>(0);
		auto moveCount = std::make_shared<int>(0);
		{
			InvokeCounter counter(copyCount, moveCount);
			auto destroyCalled = std::make_shared<OnDestructor>([=] {
				*destroyed = true;
			});
			rpl::lifetime lifetime;
			single(std::move(counter))
				| on_next([=](InvokeCounter&&) {
					(void)destroyCalled;
					++*sum;
				}) | on_error([=](no_error) {
					(void)destroyCalled;
				}) | on_done([=] {
					(void)destroyCalled;
					*doneGenerated = true;
				}) | start(lifetime);
		}
		REQUIRE(*sum == 1);
		REQUIRE(*doneGenerated);
		REQUIRE(*destroyed);
		REQUIRE(*copyCount == 0);
	}

	SECTION("then test") {
		auto sum = std::make_shared<int>(0);
		auto doneGenerated = std::make_shared<bool>(false);
		auto destroyed = std::make_shared<bool>(false);
		auto copyCount = std::make_shared<int>(0);
		auto moveCount = std::make_shared<int>(0);
		{
			auto testing = complete<InvokeCounter>();
			for (auto i = 0; i != 5; ++i) {
				InvokeCounter counter(copyCount, moveCount);
				testing = std::move(testing)
					| then(single(std::move(counter)));
			}
			auto destroyCalled = std::make_shared<OnDestructor>([=] {
				*destroyed = true;
			});

			rpl::lifetime lifetime;
			std::move(testing)
			| then(complete<InvokeCounter>())
			| on_next([=](InvokeCounter&&) {
				(void)destroyCalled;
				++*sum;
			}) | on_error([=](no_error) {
				(void)destroyCalled;
			}) | on_done([=] {
				(void)destroyCalled;
				*doneGenerated = true;
			}) | start(lifetime);
		}
		REQUIRE(*sum == 5);
		REQUIRE(*doneGenerated);
		REQUIRE(*destroyed);
		REQUIRE(*copyCount == 0);
	}

	SECTION("map test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			single(1)
				| then(single(2))
				| then(single(3))
				| then(single(4))
				| then(single(5))
				| map([](int value) {
				return std::to_string(value);
			}) | on_next([=](std::string &&value) {
				*sum += std::move(value) + ' ';
			}) | start(lifetime);
		}
		REQUIRE(*sum == "1 2 3 4 5 ");
	}

	SECTION("deferred test") {
		auto launched = std::make_shared<int>(0);
		auto checked = std::make_shared<int>(0);
		{
			rpl::lifetime lifetime;
			auto make_next = [=] {
				return deferred([=] {
					return single(++*launched);
				});
			};
			make_next()
				| then(make_next())
				| then(make_next())
				| then(make_next())
				| then(make_next())
				| on_next([=](int value) {
					REQUIRE(++*checked == *launched);
					REQUIRE(*checked == value);
				}) | start(lifetime);
			REQUIRE(*launched == 5);
		}
	}

	SECTION("filter test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			single(1)
				| then(single(1))
				| then(single(2))
				| then(single(2))
				| then(single(3))
				| filter([](int value) { return value != 2; })
				| map([](int value) {
					return std::to_string(value);
				}) | on_next([=](std::string &&value) {
					*sum += std::move(value) + ' ';
				}) | start(lifetime);
		}
		REQUIRE(*sum == "1 1 3 ");
	}

	SECTION("distinct_until_changed test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			single(1)
				| then(single(1))
				| then(single(2))
				| then(single(2))
				| then(single(3))
				| distinct_until_changed()
				| map([](int value) {
				return std::to_string(value);
			}) | on_next([=](std::string &&value) {
				*sum += std::move(value) + ' ';
			}) | start(lifetime);
		}
		REQUIRE(*sum == "1 2 3 ");
	}

	SECTION("flatten_latest test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			single(single(1) | then(single(2)))
				| then(single(single(3) | then(single(4))))
				| then(single(single(5) | then(single(6))))
				| flatten_latest()
				| map([](int value) {
				return std::to_string(value);
			}) | on_next([=](std::string &&value) {
				*sum += std::move(value) + ' ';
			}) | start(lifetime);
		}
		REQUIRE(*sum == "1 2 3 4 5 6 ");
	}
}
