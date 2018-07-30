/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/cache/storage_cache_types.h"
#include "base/basic_types.h"
#include <crl/crl_object_on_queue.h>
#include <QtCore/QString>

namespace Storage {
class EncryptionKey;
namespace Cache {
namespace details {
class Database;
} // namespace details

struct Key {
	uint64 high = 0;
	uint64 low = 0;
};

inline bool operator==(const Key &a, const Key &b) {
	return (a.high == b.high) && (a.low == b.low);
}

inline bool operator!=(const Key &a, const Key &b) {
	return !(a == b);
}

inline bool operator<(const Key &a, const Key &b) {
	return std::tie(a.high, a.low) < std::tie(b.high, b.low);
}

class Database {
public:
	struct Settings {
		size_type sizeLimit = 0;
	};
	Database(const QString &path, const Settings &settings);

	void open(EncryptionKey key, FnMut<void(Error)> done);
	void close(FnMut<void()> done);

	void put(const Key &key, QByteArray value, FnMut<void(Error)> done);
	void get(const Key &key, FnMut<void(QByteArray)> done);
	void remove(const Key &key, FnMut<void()> done);

	void clear(FnMut<void(Error)> done);

	~Database();

private:
	using Implementation = details::Database;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace Cache
} // namespace Storage
