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

class OnDestructor {
public:
	OnDestructor(std::function<void()> callback)
	: _callback(std::move(callback)) {
	}
	~OnDestructor() {
		if (_callback) {
			_callback();
		}
	}

private:
	std::function<void()> _callback;

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
		: _copyCounter(details::take(other._copyCounter))
		, _moveCounter(details::take(other._moveCounter)) {
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
		return *this;
	}
	InvokeCounter &operator=(InvokeCounter &&other) {
		_copyCounter = details::take(other._copyCounter);
		_moveCounter = details::take(other._moveCounter);
		if (_moveCounter) {
			++*_moveCounter;
		}
		return *this;
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
				| start_with_next_error_done([=](InvokeCounter&&) {
					(void)destroyCalled;
					++*sum;
				}, [=](no_error) {
					(void)destroyCalled;
				}, [=] {
					(void)destroyCalled;
					*doneGenerated = true;
				}, lifetime);
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
			auto testing = complete<InvokeCounter>() | type_erased();
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
				| start_with_next_error_done([=](InvokeCounter&&) {
					(void)destroyCalled;
					++*sum;
				}, [=](no_error) {
					(void)destroyCalled;
				}, [=] {
					(void)destroyCalled;
					*doneGenerated = true;
				}, lifetime);
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
				})
				| start_with_next([=](std::string &&value) {
					*sum += std::move(value) + ' ';
				}, lifetime);
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
				| start_with_next([=](int value) {
					REQUIRE(++*checked == *launched);
					REQUIRE(*checked == value);
				}, lifetime);
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
				})
				| start_with_next([=](std::string &&value) {
					*sum += std::move(value) + ' ';
				}, lifetime);
		}
		REQUIRE(*sum == "1 1 3 ");
	}

	SECTION("filter tuple test") {
		auto sum = std::make_shared<std::string>("");
		{
			auto lifetime = single(std::make_tuple(1, 2))
				| then(single(std::make_tuple(1, 2)))
				| then(single(std::make_tuple(2, 3)))
				| then(single(std::make_tuple(2, 3)))
				| then(single(std::make_tuple(3, 4)))
				| filter([](auto first, auto second) { return first != 2; })
				| map([](auto first, auto second) {
					return std::to_string(second);
				})
				| start_with_next([=](std::string &&value) {
					*sum += std::move(value) + ' ';
				});
		}
		REQUIRE(*sum == "2 2 4 ");
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
				})
				| start_with_next([=](std::string &&value) {
					*sum += std::move(value) + ' ';
				}, lifetime);
		}
		REQUIRE(*sum == "1 2 3 ");
	}

	SECTION("flatten_latest test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			{
				event_stream<int> stream;
				single(single(1) | then(single(2)))
					| then(single(single(3) | then(single(4))))
					| then(single(single(5) | then(stream.events())))
					| flatten_latest()
					| map([](int value) {
						return std::to_string(value);
					})
					| start_with_next_done([=](std::string &&value) {
						*sum += std::move(value) + ' ';
					}, [=] {
						*sum += "done ";
					}, lifetime);
				stream.fire(6);
			}
			single(single(1))
				| then(single(single(2) | then(single(3))))
				| then(single(single(4) | then(single(5)) | then(single(6))))
				| flatten_latest()
				| map([](int value) {
					return std::to_string(value);
				})
				| start_with_next([=](std::string &&value) {
					*sum += std::move(value) + ' ';
				}, lifetime);
		}
		REQUIRE(*sum == "1 2 3 4 5 6 done 1 2 3 4 5 6 ");
	}

	SECTION("combine vector test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			event_stream<bool> a;
			event_stream<bool> b;
			event_stream<bool> c;

			std::vector<producer<bool>> v;
			v.push_back(a.events());
			v.push_back(b.events());
			v.push_back(c.events());

			combine(std::move(v), [](const auto &values) {
					return values[0] && values[1] && !values[2];
				})
				| start_with_next([=](bool value) {
					*sum += std::to_string(value ? 1 : 0);
				}, lifetime);

			a.fire(true);
			b.fire(true);
			c.fire(false);
			a.fire(false);
			b.fire(true);
			a.fire(true);
			c.fire(true);
		}
		REQUIRE(*sum == "10010");
	}

	SECTION("combine test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			event_stream<int> a;
			event_stream<short> b;
			event_stream<char> c;

			combine(
				a.events(),
				b.events(),
				c.events(),
				[](long a, long b, long c) {
					return a;
				})
				| start_with_next([=](int value) {
					*sum += std::to_string(value);
				}, lifetime);

			combine(
				a.events(),
				b.events(),
				c.events(),
				[](auto &&value) {
					return std::get<1>(value);
				})
				| start_with_next([=](int value) {
					*sum += std::to_string(value);
				}, lifetime);

			combine(a.events(), b.events(), c.events())
				| map([](auto &&value) {
					return std::make_tuple(
						std::to_string(std::get<0>(value)),
						std::to_string(std::get<1>(value)),
						std::to_string(std::get<2>(value)));
				})
				| start_with_next([=](auto &&value) {
					*sum += std::get<0>(value) + ' '
						+ std::get<1>(value) + ' '
						+ std::get<2>(value) + ' ';
				}, lifetime);
			a.fire(1);
			b.fire(2);
			c.fire(3);
			a.fire(4);
			b.fire(5);
			c.fire(6);
		}
		REQUIRE(*sum == "121 2 3 424 2 3 454 5 3 454 5 6 ");
	}

	SECTION("mappers test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			event_stream<int> a;
			event_stream<short> b;
			event_stream<char> c;

			using namespace mappers;

			// MSVC BUG + REGRESSION rpl::mappers::tuple :(
			//combine(
			//	a.events(),
			//	b.events(),
			//	tuple(_1, _1 + _2)
			//) | rpl::start_with_next([=](int a, int a_plus_b) {
			//	*sum += std::to_string(a * a_plus_b);
			//}, lifetime);

			combine(
				a.events(),
				b.events(),
				c.events(),
				_1 + _2 + _3 + 10)
				| start_with_next([=](int value) {
					*sum += std::to_string(value);
				}, lifetime);

			a.fire(1);
			b.fire(2);
			c.fire(3);
			a.fire(4);
			b.fire(5);
			c.fire(6);
		}
		REQUIRE(*sum == "16192225");

		// MSVC BUG + REGRESSION rpl::mappers::tuple :(
		//REQUIRE(*sum == "3162419362225");
	}

	SECTION("after_next test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			ints(3)
				| after_next([=](int value) {
					*sum += std::to_string(-value-1);
				})
				| start_with_next([=](int value) {
					*sum += std::to_string(value);
				}, lifetime);
		}
		REQUIRE(*sum == "0-11-22-3");
	}

	SECTION("combine_previous test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			event_stream<int> a;

			a.events(
			) | combine_previous(
			) | start_with_next([=](int previous, int next) {
				*sum += std::to_string(previous) + ' ';
				*sum += std::to_string(next) + ' ';
			}, lifetime);

			a.events(
			) | combine_previous(
				5
			) | start_with_next([=](int previous, int next) {
				*sum += std::to_string(10 + previous) + ' ';
				*sum += std::to_string(next) + ' ';
			}, lifetime);

			a.fire(1);
			a.fire(2);
			a.fire(3);
			a.fire(4);
		}
		REQUIRE(*sum == "15 1 1 2 11 2 2 3 12 3 3 4 13 4 ");
	}

	SECTION("take test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			ints(10) | take(3)
				| start_with_next_done([=](int value) {
					*sum += std::to_string(value);
				}, [=] {
					*sum += "done";
				}, lifetime);
		}
		{
			rpl::lifetime lifetime;
			ints(3) | take(3)
				| start_with_next_done([=](int value) {
					*sum += std::to_string(value);
				}, [=] {
					*sum += "done";
				}, lifetime);
		}
		{
			rpl::lifetime lifetime;
			ints(3) | take(10)
				| start_with_next_done([=](int value) {
					*sum += std::to_string(value);
				}, [=] {
					*sum += "done";
				}, lifetime);
		}
		REQUIRE(*sum == "012done012done012done");
	}

	SECTION("skip test") {
		auto sum = std::make_shared<std::string>("");
		{
			rpl::lifetime lifetime;
			ints(10) | skip(5)
				| start_with_next_done([=](int value) {
					*sum += std::to_string(value);
				}, [=] {
					*sum += "done";
				}, lifetime);
		}
		{
			rpl::lifetime lifetime;
			ints(3) | skip(3)
				| start_with_next_done([=](int value) {
					*sum += std::to_string(value);
				}, [=] {
					*sum += "done";
				}, lifetime);
		}
		{
			rpl::lifetime lifetime;
			ints(3) | skip(10)
				| start_with_next_done([=](int value) {
					*sum += std::to_string(value);
				}, [=] {
					*sum += "done";
				}, lifetime);
		}
		REQUIRE(*sum == "56789donedonedone");
	}
}
