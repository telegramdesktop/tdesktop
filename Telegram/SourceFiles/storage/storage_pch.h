/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <crl/crl.h>
#include <rpl/rpl.h>

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>

#include <range/v3/all.hpp>

#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/optional.h"
#include "base/openssl_help.h"

#include "logs.h"
