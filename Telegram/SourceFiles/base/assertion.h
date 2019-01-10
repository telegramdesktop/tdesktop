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

[[noreturn]] inline void fail(
		const char *message,
		const char *file,
		int line) {
	log(message, file, line);

	// Crash with access violation and generate crash report.
	volatile auto nullptr_value = (int*)nullptr;
	*nullptr_value = 0;

	// Silent the possible failure to comply noreturn warning.
	std::abort();
}

constexpr const char* extract_basename(const char* path, size_t size) {
	while (size != 0 && path[size - 1] != '/' && path[size - 1] != '\\') {
		--size;
	}
	return path + size;
}

} // namespace assertion
} // namespace base

#if defined(__clang__) || defined(__GNUC__)
#define AssertUnlikelyHelper(x) __builtin_expect(!!(x), 0)
#else
#define AssertUnlikelyHelper(x) (!!(x))
#endif

#define AssertValidationCondition(condition, message, file, line)\
	((AssertUnlikelyHelper(!(condition)))\
		? ::base::assertion::fail(message, file, line)\
		: ::base::assertion::noop())

#define SOURCE_FILE_BASENAME (::base::assertion::extract_basename(\
	__FILE__,\
	sizeof(__FILE__)))

#define AssertCustom(condition, message) (AssertValidationCondition(\
	condition,\
	message,\
	SOURCE_FILE_BASENAME,\
	__LINE__))
#define Assert(condition) AssertCustom(condition, "\"" #condition "\"")

// Define our own versions of Expects() and Ensures().
// Let them crash with reports and logging.
#ifdef Expects
#undef Expects
#endif // Expects
#define Expects(condition) (AssertValidationCondition(\
	condition,\
	"\"" #condition "\"",\
	SOURCE_FILE_BASENAME,\
	__LINE__))

#ifdef Ensures
#undef Ensures
#endif // Ensures
#define Ensures(condition) (AssertValidationCondition(\
	condition,\
	"\"" #condition "\"",\
	SOURCE_FILE_BASENAME,\
	__LINE__))

#ifdef Unexpected
#undef Unexpected
#endif // Unexpected
#define Unexpected(message) (::base::assertion::fail(\
	"Unexpected: " message,\
	SOURCE_FILE_BASENAME,\
	__LINE__))

#ifdef _DEBUG
#define AssertIsDebug(...)
#endif // _DEBUG
