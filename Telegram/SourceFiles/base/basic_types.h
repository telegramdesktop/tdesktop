/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/build_config.h"
#include "base/ordered_set.h"
#include "base/unique_function.h"
#include "base/functors.h"

#include <QtGlobal>

#include <string>
#include <exception>
#include <memory>
#include <ctime>
#include <functional>
#include <gsl/gsl>

namespace func = base::functors;

using gsl::not_null;
using index_type = gsl::index;
using size_type = gsl::index;

template <typename Signature>
using Fn = std::function<Signature>;

template <typename Signature>
using FnMut = base::unique_function<Signature>;

//using uchar = unsigned char; // Qt has uchar
using int8 = qint8;
using uint8 = quint8;
using int16 = qint16;
using uint16 = quint16;
using int32 = qint32;
using uint32 = quint32;
using int64 = qint64;
using uint64 = quint64;
using float32 = float;
using float64 = double;

using TimeMs = int64;
using TimeId = int32;

// Define specializations for QByteArray for Qt 5.3.2, because
// QByteArray in Qt 5.3.2 doesn't declare "pointer" subtype.
#ifdef OS_MAC_OLD
namespace gsl {

template <>
inline span<char> make_span<QByteArray>(QByteArray &cont) {
	return span<char>(cont.data(), cont.size());
}

template <>
inline span<const char> make_span(const QByteArray &cont) {
	return span<const char>(cont.constData(), cont.size());
}

} // namespace gsl
#endif // OS_MAC_OLD
