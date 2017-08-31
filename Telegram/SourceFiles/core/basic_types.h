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

#include <string>
#include <exception>
#include <memory>
#include <ctime>

#include "base/build_config.h"

template <typename Type>
using not_null = gsl::not_null<Type>;

// Custom libc++ build used for old OS X versions already has this.
#ifndef OS_MAC_OLD

#if defined COMPILER_CLANG || defined COMPILER_GCC
namespace std {

template <typename T>
constexpr std::add_const_t<T>& as_const(T& t) noexcept {
    return t;
}

template <typename T>
void as_const(const T&&) = delete;

} // namespace std
#endif // COMPILER_CLANG || COMPILER_GCC

#endif // OS_MAC_OLD

#include "base/ordered_set.h"

//using uchar = unsigned char; // Qt has uchar
using int16 = qint16;
using uint16 = quint16;
using int32 = qint32;
using uint32 = quint32;
using int64 = qint64;
using uint64 = quint64;
using float32 = float;
using float64 = double;

#define qsl(s) QStringLiteral(s)
#define qstr(s) QLatin1String(s, sizeof(s) - 1)
