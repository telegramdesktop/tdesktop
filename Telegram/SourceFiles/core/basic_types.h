/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <string>
#include <exception>
#include <memory>
#include <ctime>
#include <functional>

#include <crl/crl.h>
#include "base/build_config.h"
#include "base/ordered_set.h"
#include "base/unique_function.h"
#include "base/functors.h"

namespace func = base::functors;

using gsl::not_null;

template <typename Signature>
using Fn = std::function<Signature>;

template <typename Signature>
using FnMut = base::unique_function<Signature>;

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
#define qstr(s) QLatin1String((s), sizeof(s) - 1)
