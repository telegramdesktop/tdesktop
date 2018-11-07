/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

#ifdef _DEBUG
#define AssertIsDebug(...)
#endif // _DEBUG
