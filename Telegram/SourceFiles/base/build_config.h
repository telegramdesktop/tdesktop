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

#include <stdint.h>

// thanks Chromium

#if defined(__APPLE__)
#define OS_MAC 1
#elif defined(__linux__) // __APPLE__
#define OS_LINUX 1
#elif defined(_WIN32) // __APPLE__ || __linux__
#define OS_WIN 1
#else // __APPLE__ || __linux__ || _WIN32
#error Please add support for your platform in base/build_config.h
#endif // else for __APPLE__ || __linux__ || _WIN32

// For access to standard POSIXish features, use OS_POSIX instead of a
// more specific macro.
#if defined(OS_MAC) || defined(OS_LINUX)
#define OS_POSIX 1
#endif // OS_MAC || OS_LINUX

// Compiler detection.
#if defined(__clang__)
#define COMPILER_CLANG 1
#elif defined(__GNUC__) // __clang__
#define COMPILER_GCC 1
#elif defined(_MSC_VER) // __clang__ || __GNUC__
#define COMPILER_MSVC 1
#else // _MSC_VER || __clang__ || __GNUC__
#error Please add support for your compiler in base/build_config.h
#endif // else for _MSC_VER || __clang__ || __GNUC__

// Processor architecture detection.
#if defined(_M_X64) || defined(__x86_64__)
#define ARCH_CPU_X86_FAMILY 1
#define ARCH_CPU_X86_64 1
#define ARCH_CPU_64_BITS 1
#elif defined(_M_IX86) || defined(__i386__)
#define ARCH_CPU_X86_FAMILY 1
#define ARCH_CPU_X86 1
#define ARCH_CPU_32_BITS 1
#else
#error Please add support for your architecture in base/build_config.h
#endif

#if defined(__GNUC__)
#define FORCE_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline
#endif

#include <climits>
static_assert(CHAR_BIT == 8, "Not supported char size.");
