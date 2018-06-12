/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "catch.hpp"

#include "base/flags.h"

namespace MethodNamespace {

template <typename Enum>
void TestFlags(Enum a, Enum b, Enum c) {
	auto abc = a | b;
	abc |= c;
	auto test = abc != a;
	CHECK(abc != a);
	CHECK(abc != (a | b));
	CHECK((abc & a) == a);
	CHECK((abc & b) == b);
	CHECK((abc & c) == c);
	CHECK((abc & ~a) == (b | c));
	CHECK((abc & ~(b | c)) == a);
	CHECK((abc ^ a) == (abc & ~a));

	auto another = a | b;
	another |= c;
	CHECK(abc == another);
	another &= ~b;
	CHECK(another == (a | c));
	another ^= a;
	CHECK(another == c);
	another = 0;
	another = nullptr;
	auto is_zero = ((another & abc) == 0);
	CHECK(is_zero);
	CHECK(!(another & abc));
	auto more = a | another;
	auto just = a | 0;
	CHECK(more == just);
	CHECK(just);
}

} // namespace MethodNamespace

namespace FlagsNamespace {

enum class Flag : int {
	one = (1 << 0),
	two = (1 << 1),
	three = (1 << 2),
};
inline constexpr auto is_flag_type(Flag) { return true; }

class Class {
public:
	enum class Public : long {
		one = (1 << 2),
		two = (1 << 1),
		three = (1 << 0),
	};
	friend inline constexpr auto is_flag_type(Public) { return true; }

	static void TestPrivate();

private:
	enum class Private : long {
		one = (1 << 0),
		two = (1 << 1),
		three = (1 << 2),
	};
	friend inline constexpr auto is_flag_type(Private) { return true; }

};

void Class::TestPrivate() {
	MethodNamespace::TestFlags(Private::one, Private::two, Private::three);
}

} // namespace FlagsNamespace

namespace ExtendedNamespace {

enum class Flag : int {
	one = (1 << 3),
	two = (1 << 4),
	three = (1 << 5),
};

} // namespace ExtendedNamespace

namespace base {

template<>
struct extended_flags<ExtendedNamespace::Flag> {
	using type = FlagsNamespace::Flag;
};

} // namespace base

TEST_CASE("flags operators on scoped enums", "[flags]") {
	SECTION("testing non-member flags") {
		MethodNamespace::TestFlags(
			FlagsNamespace::Flag::one,
			FlagsNamespace::Flag::two,
			FlagsNamespace::Flag::three);
	}
	SECTION("testing public member flags") {
		MethodNamespace::TestFlags(
			FlagsNamespace::Class::Public::one,
			FlagsNamespace::Class::Public::two,
			FlagsNamespace::Class::Public::three);
	}
	SECTION("testing private member flags") {
		FlagsNamespace::Class::TestPrivate();
	}
	SECTION("testing extended flags") {
		MethodNamespace::TestFlags(
			ExtendedNamespace::Flag::one,
			ExtendedNamespace::Flag::two,
			ExtendedNamespace::Flag::three);

		auto onetwo = FlagsNamespace::Flag::one | ExtendedNamespace::Flag::two;
		auto twoone = ExtendedNamespace::Flag::two | FlagsNamespace::Flag::one;
		CHECK(onetwo == twoone);
	}
}
