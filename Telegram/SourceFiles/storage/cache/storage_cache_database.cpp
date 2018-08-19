/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_database.h"

#include "storage/cache/storage_cache_database_object.h"

namespace Storage {
namespace Cache {

Database::Database(const QString &path, const Settings &settings)
: _wrapped(path, settings) {
}

void Database::open(EncryptionKey key, FnMut<void(Error)> done) {
	_wrapped.with([
		key,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.open(key, std::move(done));
	});
}

void Database::close(FnMut<void()> done) {
	_wrapped.with([
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.close(std::move(done));
	});
}

void Database::put(
		const Key &key,
		QByteArray value,
		FnMut<void(Error)> done) {
	_wrapped.with([
		key,
		value = std::move(value),
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.put(key, std::move(value), std::move(done));
	});
}

void Database::get(const Key &key, FnMut<void(QByteArray)> done) {
	_wrapped.with([
		key,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.get(key, std::move(done));
	});
}

void Database::remove(const Key &key, FnMut<void()> done) {
	_wrapped.with([
		key,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.remove(key, std::move(done));
	});
}

void Database::clear(FnMut<void(Error)> done) {
	_wrapped.with([
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.clear(std::move(done));
	});
}

Database::~Database() = default;

} // namespace Cache
} // namespace Storage
