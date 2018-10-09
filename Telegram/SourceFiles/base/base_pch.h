/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QMutex>
#include <QtCore/QRegularExpression>
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>

#include <crl/crl.h>
#include <rpl/rpl.h>

#include <vector>
#include <unordered_map>
#include <set>

#include <range/v3/all.hpp>
#ifdef Q_OS_WIN
#include "platform/win/windows_range_v3_helpers.h"
#endif // Q_OS_WIN

#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/optional.h"
#include "base/openssl_help.h"
