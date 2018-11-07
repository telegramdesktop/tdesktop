/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

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
#elif defined _MSC_VER && _MSC_VER >= 1914 // __clang__
#pragma warning(push)
#pragma warning(disable:4180)
#endif // __clang__ || _MSC_VER >= 1914

#include <QtCore/QtCore>

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined _MSC_VER && _MSC_VER >= 1914 // __clang__
#pragma warning(pop)
#endif // __clang__ || _MSC_VER >= 1914

#ifdef OS_MAC_STORE
#define MAC_USE_BREAKPAD
#endif // OS_MAC_STORE

#include <QtWidgets/QtWidgets>
#include <QtNetwork/QtNetwork>

#include <array>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>
#include <optional>

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
#include "base/flat_set.h"
#include "base/flat_map.h"
#include "base/weak_ptr.h"

#include "base/basic_types.h"
#include "logs.h"
#include "core/utils.h"
#include "config.h"

#include "mtproto/facade.h"

#include "ui/style/style_core.h"
#include "styles/palette.h"
#include "styles/style_basic.h"

#include "ui/animation.h"
#include "ui/twidget.h"
#include "ui/image/image_location.h"
#include "ui/text/text.h"

#include "data/data_types.h"
#include "app.h"
#include "facades.h"

#endif // __cplusplus
