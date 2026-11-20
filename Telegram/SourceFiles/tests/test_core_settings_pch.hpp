/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
// WHY: unchanged Telegram TUs take these from stdafx.h; its config.h needs API keys.
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLocale>
#include <QtCore/QUrl>

#include <crl/crl.h>

#include "base/bytes.h"
#include "base/const_string.h"
#include "base/debug_log.h"
#include "scheme.h"
#include "mtproto/type_utils.h"
#include "data/data_types.h"
#include "settings.h"
