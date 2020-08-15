/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#define __HUGE

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

#include <QtCore/QMap>

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined _MSC_VER && _MSC_VER >= 1914 // __clang__
#pragma warning(pop)
#endif // __clang__ || _MSC_VER >= 1914

#include <QtCore/QtMath>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QMutex>
#include <QtCore/QReadWriteLock>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QThread>
#include <QtCore/QByteArray>
#include <QtCore/QChar>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QMargins>
#include <QtCore/QPair>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtGui/QPixmap>
#include <QtGui/QtEvents>
#include <QtGui/QBrush>
#include <QtGui/QColor>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QRegion>
#include <QtGui/QRgb>
#include <QtGui/QFont>
#include <QtGui/QFontInfo>

#include <QtWidgets/QWidget>

#ifndef OS_MAC_OLD
#include <QtWidgets/QOpenGLWidget>
#endif // OS_MAC_OLD

// Fix Google Breakpad build for Mac App Store and Linux version
#ifdef Q_OS_UNIX
#define __STDC_FORMAT_MACROS
#endif // Q_OS_UNIX

// Remove 'small' macro definition.
#ifdef Q_OS_WIN
#include <rpc.h>
#ifdef small
#undef small
#endif // small
#endif // Q_OS_WIN

#include <array>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>
#include <any>
#include <optional>

#include <range/v3/all.hpp>

// Redefine Ensures/Expects by our own assertions.
#include "base/assertion.h"

#include <gsl/gsl>
#include <rpl/rpl.h>
#include <crl/crl.h>

#include "base/variant.h"
#include "base/optional.h"
#include "base/algorithm.h"
#include "base/invoke_queued.h"
#include "base/flat_set.h"
#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "base/observer.h"

#include "base/basic_types.h"
#include "logs.h"
#include "core/utils.h"
#include "config.h"

#include "scheme.h"
#include "mtproto/type_utils.h"

#include "ui/style/style_core.h"
#include "styles/palette.h"
#include "styles/style_basic.h"

#include "ui/image/image_location.h"
#include "ui/text/text.h"

#include "data/data_types.h"

#endif // __cplusplus
