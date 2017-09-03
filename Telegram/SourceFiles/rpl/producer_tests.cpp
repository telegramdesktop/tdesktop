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
#include "rpl/event_stream.h"

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

	SECTION("event_stream basic test") {
		auto sum = std::make_shared<int>(0);
		rpl::event_stream<int> stream;
		stream.fire(1);
		stream.fire(2);
		stream.fire(3);
		{
			auto lifetime = stream.events().start([=, &stream](int value) {
				*sum += value;
			}, [=](no_error) {
			}, [=] {
			});
			stream.fire(11);
			stream.fire(12);
			stream.fire(13);
		}
		stream.fire(21);
		stream.fire(22);
		stream.fire(23);

		REQUIRE(11 + 12 + 13);
	}

	SECTION("event_stream add in handler test") {
		auto sum = std::make_shared<int>(0);
		rpl::event_stream<int> stream;

		{
			auto composite = lifetime();
			composite = stream.events().start([=, &stream, &composite](int value) {
				*sum += value;
				composite.add(stream.events().start([=](int value) {
					*sum += value;
				}, [=](no_error) {
				}, [=] {
				}));
			}, [=](no_error) {
			}, [=] {
			});

			{
				auto inner = lifetime();
				inner = stream.events().start([=, &stream, &inner](int value) {
					*sum += value;
					inner.add(stream.events().start([=](int value) {
						*sum += value;
					}, [=](no_error) {
					}, [=] {
					}));
				}, [=](no_error) {
				}, [=] {
				});

				stream.fire(1);
				stream.fire(2);
				stream.fire(3);
			}
			stream.fire(11);
			stream.fire(12);
			stream.fire(13);
		}
		stream.fire(21);
		stream.fire(22);
		stream.fire(23);

		REQUIRE(*sum ==
			(1 + 1) +
			((2 + 2) + (2 + 2)) +
			((3 + 3 + 3) + (3 + 3 + 3)) +
			(11 + 11 + 11 + 11) +
			(12 + 12 + 12 + 12 + 12) +
			(13 + 13 + 13 + 13 + 13 + 13));
	}

	SECTION("event_stream add and remove in handler test") {
		auto sum = std::make_shared<int>(0);
		rpl::event_stream<int> stream;

		{
			auto composite = lifetime();
			composite = stream.events().start([=, &stream, &composite](int value) {
				*sum += value;
				composite = stream.events().start([=](int value) {
					*sum += value;
				}, [=](no_error) {
				}, [=] {
				});
			}, [=](no_error) {
			}, [=] {
			});

			{
				auto inner = lifetime();
				inner = stream.events().start([=, &stream, &inner](int value) {
					*sum += value;
					inner = stream.events().start([=](int value) {
						*sum += value;
					}, [=](no_error) {
					}, [=] {
					});
				}, [=](no_error) {
				}, [=] {
				});

				stream.fire(1);
				stream.fire(2);
				stream.fire(3);
			}
			stream.fire(11);
			stream.fire(12);
			stream.fire(13);
		}
		stream.fire(21);
		stream.fire(22);
		stream.fire(23);

		REQUIRE(*sum ==
			(1 + 1) +
			(2 + 2) +
			(3 + 3) +
			(11) +
			(12) +
			(13));
	}

	SECTION("event_stream ends before handler lifetime") {
		auto sum = std::make_shared<int>(0);
		lifetime extended;
		{
			rpl::event_stream<int> stream;
			extended = stream.events().start([=](int value) {
				*sum += value;
			}, [=](no_error) {
			}, [=] {
			});
			stream.fire(1);
			stream.fire(2);
			stream.fire(3);
		}
		REQUIRE(*sum == 1 + 2 + 3);
	}
}

