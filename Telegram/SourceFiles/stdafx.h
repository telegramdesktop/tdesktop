/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#define NOMINMAX // no min() and max() macro declarations
#define __HUGE

// Fix Google Breakpad build for Mac App Store version
#ifdef Q_OS_MAC
#define __STDC_FORMAT_MACROS
#endif // Q_OS_MAC

#ifdef __cplusplus

#include <cmath>

// False positive warning in clang for QMap member function value:
// const T QMap<Key, T>::value(const Key &akey, const T &adefaultValue)
// fires with "Returning address of local temporary object" which is not true.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-stack-address"
#endif // __clang__

#include <QtCore/QtCore>

#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__

#if QT_VERSION < QT_VERSION_CHECK(5, 5, 0)
#define OS_MAC_OLD
#define RANGES_CXX_THREAD_LOCAL 0
#endif // QT_VERSION < 5.5.0

#ifdef OS_MAC_STORE
#define MAC_USE_BREAKPAD
#endif // OS_MAC_STORE

#include <QtWidgets/QtWidgets>
#include <QtNetwork/QtNetwork>

#include <array>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <memory>

#include <range/v3/all.hpp>
#ifdef Q_OS_WIN
#include "platform/win/windows_range_v3_helpers.h"
#endif // Q_OS_WIN

// Ensures/Expects.
#include <gsl/gsl_assert>

// Redefine Ensures/Expects by our own assertions.
#include "base/assertion.h"

#include <gsl/gsl>
#include <rpl/rpl.h>
#include <crl/crl.h>

#include "base/variant.h"
#include "base/optional.h"
#include "base/algorithm.h"
#include "base/functors.h"

namespace func = base::functors;

#include "base/flat_set.h"
#include "base/flat_map.h"

#include "core/basic_types.h"
#include "logs.h"
#include "core/utils.h"
#include "base/lambda.h"
#include "base/lambda_guard.h"
#include "config.h"

#include "mtproto/facade.h"

#include "ui/style/style_core.h"
#include "styles/palette.h"
#include "styles/style_basic.h"

#include "ui/animation.h"
#include "ui/twidget.h"
#include "ui/images.h"
#include "ui/text/text.h"

#include "data/data_types.h"
#include "app.h"
#include "facades.h"

#endif // __cplusplus
