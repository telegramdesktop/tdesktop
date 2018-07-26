/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/basic_types.h"

#include <crl/crl_object_on_queue.h>

#include <QtCore/QString>

namespace Storage {
class EncryptionKey;
namespace Cache {
namespace details {
class Database;
} // namespace details

class Database {
public:
	Database(const QString &path, EncryptionKey key);

private:
	using Implementation = details::Database;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace Cache
} // namespace Storage
