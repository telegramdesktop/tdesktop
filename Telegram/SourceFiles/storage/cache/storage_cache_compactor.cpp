/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_compactor.h"

namespace Storage {
namespace Cache {
namespace details {

class CompactorObject {
public:
	CompactorObject(
		crl::weak_on_queue<CompactorObject> weak,
		const QString &path,
		crl::weak_on_queue<DatabaseObject> database);

private:
	void start();

	crl::weak_on_queue<CompactorObject> _weak;
	crl::weak_on_queue<DatabaseObject> _database;
	QString _path;

};

CompactorObject::CompactorObject(
	crl::weak_on_queue<CompactorObject> weak,
	const QString &path,
	crl::weak_on_queue<DatabaseObject> database)
: _weak(std::move(weak))
, _database(std::move(database))
, _path(path) {
	start();
}

void CompactorObject::start() {
}

Compactor::Compactor(
	const QString &path,
	crl::weak_on_queue<DatabaseObject> database)
: _wrapped(path, std::move(database)) {
}

Compactor::~Compactor() = default;

} // namespace details
} // namespace Cache
} // namespace Storage
