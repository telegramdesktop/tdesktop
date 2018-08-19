/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_cleaner.h"

#include <crl/crl.h>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <unordered_map>
#include <set>

namespace Storage {
namespace Cache {
namespace details {

class CleanerObject {
public:
	CleanerObject(
		crl::weak_on_queue<CleanerObject> weak,
		const QString &base,
		base::binary_guard &&guard,
		FnMut<void(Error)> done);

private:
	void start();
	void scheduleNext();
	void cleanNext();
	void done();

	crl::weak_on_queue<CleanerObject> _weak;
	QString _base, _errorPath;
	std::vector<QString> _queue;
	base::binary_guard _guard;
	FnMut<void(Error)> _done;

};

CleanerObject::CleanerObject(
	crl::weak_on_queue<CleanerObject> weak,
	const QString &base,
	base::binary_guard &&guard,
	FnMut<void(Error)> done)
: _weak(std::move(weak))
, _base(base)
, _guard(std::move(guard))
, _done(std::move(done)) {
	start();
}

void CleanerObject::start() {
	const auto entries = QDir(_base).entryList(
		QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto entry : entries) {
		_queue.push_back(entry);
	}
	if (const auto version = ReadVersionValue(_base)) {
		_queue.erase(
			ranges::remove(_queue, QString::number(*version)),
			end(_queue));
		scheduleNext();
	} else {
		_errorPath = VersionFilePath(_base);
		done();
	}
}

void CleanerObject::scheduleNext() {
	if (_queue.empty()) {
		done();
		return;
	}
	_weak.with([](CleanerObject &that) {
		if (that._guard.alive()) {
			that.cleanNext();
		}
	});
}

void CleanerObject::cleanNext() {
	const auto path = _base + _queue.back();
	_queue.pop_back();
	if (!QDir(path).removeRecursively()) {
		_errorPath = path;
	}
	scheduleNext();
}

void CleanerObject::done() {
	if (_done) {
		_done(_errorPath.isEmpty()
			? Error::NoError()
			: Error{ Error::Type::IO, _errorPath });
	}
}

Cleaner::Cleaner(
	const QString &base,
	base::binary_guard &&guard,
	FnMut<void(Error)> done)
: _wrapped(base, std::move(guard), std::move(done)) {
}

Cleaner::~Cleaner() = default;

} // namespace details
} // namespace Cache
} // namespace Storage
