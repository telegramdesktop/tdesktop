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

#include "rpl/producer.h"

using namespace rpl;

class OnDestructor {
public:
	OnDestructor(base::lambda_once<void()> callback) : _callback(std::move(callback)) {
	}
	~OnDestructor() {
		if (_callback) {
			_callback();
		}
	}

private:
	base::lambda_once<void()> _callback;

};

TEST_CASE("basic producer tests", "[rpl::producer]") {
	SECTION("producer next, done and lifetime end test") {
		auto lifetimeEnded = std::make_shared<bool>(false);
		auto sum = std::make_shared<int>(0);
		auto doneGenerated = std::make_shared<bool>(false);
		auto destroyed = std::make_shared<bool>(false);
		{
			auto destroyCaller = std::make_shared<OnDestructor>([=] {
				*destroyed = true;
			});
			{
				producer<int, no_error>([=](auto consumer) {
					destroyCaller;
					consumer.putNext(1);
					consumer.putNext(2);
					consumer.putNext(3);
					consumer.putDone();
					return [=] {
						destroyCaller;
						*lifetimeEnded = true;
					};
				}).start([=](int value) {
					destroyCaller;
					*sum += value;
				}, [=](no_error) {
					destroyCaller;
				}, [=]() {
					destroyCaller;
					*doneGenerated = true;
				});
			}
		}
		REQUIRE(*sum == 1 + 2 + 3);
		REQUIRE(*doneGenerated);
		REQUIRE(*lifetimeEnded);
		REQUIRE(*destroyed);
	}

	SECTION("producer error test") {
		auto errorGenerated = std::make_shared<bool>(false);
		{
			producer<no_value, bool>([=](auto consumer) {
				consumer.putError(true);
				return lifetime();
			}).start([=](no_value) {
			}, [=](bool error) {
				*errorGenerated = error;
			}, [=]() {
			});
		}
		REQUIRE(*errorGenerated);
	}

	SECTION("nested lifetimes test") {
		auto lifetimeEndCount = std::make_shared<int>(0);
		{
			auto lifetimes = lifetime();
			{
				auto testProducer = producer<no_value, no_error>([=](auto consumer) {
					return [=] {
						++*lifetimeEndCount;
					};
				});
				lifetimes.add(testProducer.start([=](no_value) {
				}, [=](no_error) {
				}, [=] {
				}));
				lifetimes.add(testProducer.start([=](no_value) {
				}, [=](no_error) {
				}, [=] {
				}));
			}
			REQUIRE(*lifetimeEndCount == 0);
		}
		REQUIRE(*lifetimeEndCount == 2);
	}

	SECTION("nested producers test") {
		auto sum = std::make_shared<int>(0);
		auto lifetimeEndCount = std::make_shared<int>(0);
		auto saved = lifetime();
		{
			saved = producer<int, no_error>([=](auto consumer) {
				auto inner = producer<int, no_error>([=](auto consumer) {
					consumer.putNext(1);
					consumer.putNext(2);
					consumer.putNext(3);
					return [=] {
						++*lifetimeEndCount;
					};
				});
				auto result = lifetime([=] {
					++*lifetimeEndCount;
				});
				result.add(inner.start([=](int value) {
					consumer.putNext(value);
				}, [=](no_error) {
				}, [=] {
				}));
				result.add(inner.start([=](int value) {
					consumer.putNext(value);
				}, [=](no_error) {
				}, [=] {
				}));
				return result;
			}).start([=](int value) {
				*sum += value;
			}, [=](no_error) {
			}, [=] {
			});
		}
		REQUIRE(*sum == 1 + 2 + 3 + 1 + 2 + 3);
		REQUIRE(*lifetimeEndCount == 0);
		saved.destroy();
		REQUIRE(*lifetimeEndCount == 3);
	}
}

