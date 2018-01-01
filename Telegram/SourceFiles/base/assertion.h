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
#pragma once

#include <cstdlib>

namespace base {
namespace assertion {

// Client must define that method.
void log(const char *message, const char *file, int line);

// Release build assertions.
inline constexpr void noop() {
}

[[noreturn]] inline void fail(const char *message, const char *file, int line) {
	log(message, file, line);

	// Crash with access violation and generate crash report.
	volatile auto nullptr_value = (int*)nullptr;
	*nullptr_value = 0;

	// Silent the possible failure to comply noreturn warning.
	std::abort();
}

#ifndef GSL_UNLIKELY
#define DEFINED_GSL_UNLIKELY_
#define GSL_UNLIKELY(expression) (expression)
#endif // GSL_UNLIKELY

inline constexpr void validate(bool condition, const char *message, const char *file, int line) {
	(GSL_UNLIKELY(!(condition))) ? fail(message, file, line) : noop();
}

#ifdef DEFINED_GSL_UNLIKELY_
#undef GSL_UNLIKELY
#undef DEFINED_GSL_UNLIKELY_
#endif // DEFINED_GSL_UNLIKELY_

} // namespace assertion
} // namespace base

#define AssertCustom(condition, message) (::base::assertion::validate(condition, message, __FILE__, __LINE__))
#define Assert(condition) AssertCustom(condition, "\"" #condition "\"")

// Define our own versions of Expects() and Ensures().
// Let them crash with reports and logging.
#ifdef Expects
#undef Expects
#endif // Expects
#define Expects(condition) (::base::assertion::validate(condition, "\"" #condition "\"", __FILE__, __LINE__))

#ifdef Ensures
#undef Ensures
#endif // Ensures
#define Ensures(condition) (::base::assertion::validate(condition, "\"" #condition "\"", __FILE__, __LINE__))

#ifdef Unexpected
#undef Unexpected
#endif // Unexpected
#define Unexpected(message) (::base::assertion::fail("Unexpected: " message, __FILE__, __LINE__))
