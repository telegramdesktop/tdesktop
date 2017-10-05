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

#include <rpl/producer.h>
#include <rpl/event_stream.h>

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
				auto alive = make_producer<int>([=](auto &&consumer) {
					(void)destroyCaller;
					consumer.put_next(1);
					consumer.put_next(2);
					consumer.put_next(3);
					consumer.put_done();
					return [=] {
						(void)destroyCaller;
						*lifetimeEnded = true;
					};
				}).start([=](int value) {
					(void)destroyCaller;
					*sum += value;
				}, [=](no_error) {
					(void)destroyCaller;
				}, [=]() {
					(void)destroyCaller;
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
			auto alive = make_producer<no_value, bool>([=](auto &&consumer) {
				consumer.put_error(true);
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
				auto testProducer = make_producer<no_value>([=](auto &&consumer) {
					return [=] {
						++*lifetimeEndCount;
					};
				});
				testProducer.start_copy([=](no_value) {
				}, [=](no_error) {
				}, [=] {
				}, lifetimes);
				std::move(testProducer).start([=](no_value) {
				}, [=](no_error) {
				}, [=] {
				}, lifetimes);
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
			make_producer<int>([=](auto &&consumer) {
				auto inner = make_producer<int>([=](auto &&consumer) {
					consumer.put_next(1);
					consumer.put_next(2);
					consumer.put_next(3);
					return [=] {
						++*lifetimeEndCount;
					};
				});
				auto result = lifetime([=] {
					++*lifetimeEndCount;
				});
				inner.start_copy([=](int value) {
					consumer.put_next_copy(value);
				}, [=](no_error) {
				}, [=] {
				}, result);
				std::move(inner).start([=](int value) {
					consumer.put_next_copy(value);
				}, [=](no_error) {
				}, [=] {
				}, result);
				return result;
			}).start([=](int value) {
				*sum += value;
			}, [=](no_error) {
			}, [=] {
			}, saved);
		}
		REQUIRE(*sum == 1 + 2 + 3 + 1 + 2 + 3);
		REQUIRE(*lifetimeEndCount == 0);
		saved.destroy();
		REQUIRE(*lifetimeEndCount == 3);
	}

	SECTION("tuple producer test") {
		auto result = std::make_shared<int>(0);
		{
			auto alive = make_producer<std::tuple<int, double>>([=](
					auto &&consumer) {
				consumer.put_next(std::make_tuple(1, 2.));
				return lifetime();
			}).start([=](int a, double b) {
				*result = a + int(b);
			}, [=](no_error error) {
			}, [=]() {
			});
		}
		REQUIRE(*result == 3);
	}
}

TEST_CASE("basic event_streams tests", "[rpl::event_stream]") {
	SECTION("event_stream basic test") {
		auto sum = std::make_shared<int>(0);
		event_stream<int> stream;
		stream.fire(1);
		stream.fire(2);
		stream.fire(3);
		{
			auto saved = lifetime();
			stream.events().start([=, &stream](int value) {
				*sum += value;
			}, [=](no_error) {
			}, [=] {
			}, saved);
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
		event_stream<int> stream;

		{
			auto composite = lifetime();
			stream.events().start([=, &stream, &composite](int value) {
				*sum += value;
				stream.events().start([=](int value) {
					*sum += value;
				}, [=](no_error) {
				}, [=] {
				}, composite);
			}, [=](no_error) {
			}, [=] {
			}, composite);

			{
				auto inner = lifetime();
				stream.events().start([=, &stream, &inner](int value) {
					*sum += value;
					stream.events().start([=](int value) {
						*sum += value;
					}, [=](no_error) {
					}, [=] {
					}, inner);
				}, [=](no_error) {
				}, [=] {
				}, inner);

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
		event_stream<int> stream;

		{
			auto composite = lifetime();
			stream.events().start([=, &stream, &composite](int value) {
				*sum += value;
				composite.destroy();
				stream.events().start([=](int value) {
					*sum += value;
				}, [=](no_error) {
				}, [=] {
				}, composite);
			}, [=](no_error) {
			}, [=] {
			}, composite);

			{
				auto inner = lifetime();
				stream.events().start([=, &stream, &inner](int value) {
					*sum += value;
					inner.destroy();
					stream.events().start([=](int value) {
						*sum += value;
					}, [=](no_error) {
					}, [=] {
					}, inner);
				}, [=](no_error) {
				}, [=] {
				}, inner);

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
			event_stream<int> stream;
			stream.events().start([=](int value) {
				*sum += value;
			}, [=](no_error) {
			}, [=] {
			}, extended);
			stream.fire(1);
			stream.fire(2);
			stream.fire(3);
		}
		REQUIRE(*sum == 1 + 2 + 3);
	}

	SECTION("event_stream move test") {
		auto sum = std::make_shared<int>(0);
		lifetime extended;
		{
			event_stream<int> stream;
			stream.events()
				| start_with_next([=](int value) {
					*sum += value;
				}, extended);

			stream.fire(1);
			stream.fire(2);

			auto movedStream = std::move(stream);
			movedStream.fire(3);
			movedStream.fire(4);
		}
		REQUIRE(*sum == 1 + 2 + 3 + 4);
	}
}

TEST_CASE("basic piping tests", "[rpl::producer]") {

	SECTION("start_with_*") {
		auto sum = std::make_shared<int>(0);
		auto dones = std::make_shared<int>(0);
		{
			auto alive = lifetime();
			make_producer<int, int>([=](auto &&consumer) {
				consumer.put_next(1);
				consumer.put_done();
				return lifetime();
			}) | start_with_next([=](int value) {
				*sum += value;
			}, alive);

			make_producer<int, int>([=](auto &&consumer) {
				consumer.put_next(11);
				consumer.put_error(111);
				return lifetime();
			}) | start_with_error([=](int value) {
				*sum += value;
			}, alive);

			make_producer<int, int>([=](auto &&consumer) {
				consumer.put_next(1111);
				consumer.put_done();
				return lifetime();
			}) | start_with_done([=]() {
				*dones += 1;
			}, alive);

			make_producer<int, int>([=](auto &&consumer) {
				consumer.put_next(11111);
				consumer.put_next(11112);
				consumer.put_next(11113);
				consumer.put_error(11114);
				return lifetime();
			}) | start_with_next_error([=](int value) {
				*sum += value;
			}, [=](int value) {
				*sum += value;
			}, alive);
		}

		auto alive = lifetime();
		make_producer<int, int>([=](auto &&consumer) {
			consumer.put_next(111111);
			consumer.put_next(111112);
			consumer.put_next(111113);
			consumer.put_done();
			return lifetime();
		}) | start_with_next_done([=](int value) {
			*sum += value;
		}, [=]() {
			*dones += 11;
		}, alive);

		make_producer<int, int>([=](auto &&consumer) {
			consumer.put_error(1111111);
			return lifetime();
		}) | start_with_error_done([=](int value) {
			*sum += value;
		}, [=]() {
			*dones = 0;
		}, alive);

		make_producer<int, int>([=](auto &&consumer) {
			consumer.put_next(11111111);
			consumer.put_next(11111112);
			consumer.put_next(11111113);
			consumer.put_error(11111114);
			return lifetime();
		}) | start_with_next_error_done([=](int value) {
			*sum += value;
		}, [=](int value) {
			*sum += value;
		}, [=]() {
			*dones = 0;
		}, alive);

		REQUIRE(*sum ==
			1 +
			111 +
			11111 + 11112 + 11113 + 11114 +
			111111 + 111112 + 111113 +
			1111111 +
			11111111 + 11111112 + 11111113 + 11111114);
		REQUIRE(*dones == 1 + 11);
	}

	SECTION("start_with_next should copy its callback") {
		auto sum = std::make_shared<int>(0);
		{
			auto next = [=](int value) {
				REQUIRE(sum != nullptr);
				*sum += value;
			};

			for (int i = 0; i != 3; ++i) {
				auto alive = lifetime();
				make_producer<int, int>([=](auto &&consumer) {
					consumer.put_next(1);
					consumer.put_done();
					return lifetime();
				}) | start_with_next(next, alive);
			}
		}
		REQUIRE(*sum == 3);
	}
}
