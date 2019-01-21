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

void Database::reconfigure(const Settings &settings) {
	_wrapped.with([settings](Implementation &unwrapped) mutable {
		unwrapped.reconfigure(settings);
	});
}

void Database::updateSettings(const SettingsUpdate &update) {
	_wrapped.with([update](Implementation &unwrapped) mutable {
		unwrapped.updateSettings(update);
	});
}

void Database::open(EncryptionKey &&key, FnMut<void(Error)> &&done) {
	_wrapped.with([
		key = std::move(key),
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.open(std::move(key), std::move(done));
	});
}

void Database::close(FnMut<void()> &&done) {
	_wrapped.with([
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.close(std::move(done));
	});
}

void Database::waitForCleaner(FnMut<void()> &&done) {
	_wrapped.with([
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.waitForCleaner(std::move(done));
	});
}

void Database::put(
		const Key &key,
		QByteArray &&value,
		FnMut<void(Error)> &&done) {
	return put(key, TaggedValue(std::move(value), 0), std::move(done));
}

void Database::get(const Key &key, FnMut<void(QByteArray&&)> &&done) {
	if (done) {
		auto untag = [done = std::move(done)](TaggedValue &&value) mutable {
			done(std::move(value.bytes));
		};
		getWithTag(key, std::move(untag));
	} else {
		getWithTag(key, nullptr);
	}
}

void Database::remove(const Key &key, FnMut<void(Error)> &&done) {
	_wrapped.with([
		key,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.remove(key, std::move(done));
	});
}

void Database::putIfEmpty(
		const Key &key,
		QByteArray &&value,
		FnMut<void(Error)> &&done) {
	return putIfEmpty(
		key,
		TaggedValue(std::move(value), 0),
		std::move(done));
}

void Database::copyIfEmpty(
		const Key &from,
		const Key &to,
		FnMut<void(Error)> &&done) {
	_wrapped.with([
		from,
		to,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.copyIfEmpty(from, to, std::move(done));
	});
}

void Database::moveIfEmpty(
		const Key &from,
		const Key &to,
		FnMut<void(Error)> &&done) {
	_wrapped.with([
		from,
		to,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.moveIfEmpty(from, to, std::move(done));
	});
}

void Database::put(
		const Key &key,
		TaggedValue &&value,
		FnMut<void(Error)> &&done) {
	_wrapped.with([
		key,
		value = std::move(value),
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.put(key, std::move(value), std::move(done));
	});
}

void Database::putIfEmpty(
		const Key &key,
		TaggedValue &&value,
		FnMut<void(Error)> &&done) {
	_wrapped.with([
		key,
		value = std::move(value),
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.putIfEmpty(key, std::move(value), std::move(done));
	});
}

void Database::getWithTag(
		const Key &key,
		FnMut<void(TaggedValue&&)> &&done) {
	_wrapped.with([
		key,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.get(key, std::move(done));
	});
}

auto Database::statsOnMain() const -> rpl::producer<Stats> {
	return _wrapped.producer_on_main([](const Implementation &unwrapped) {
		return unwrapped.stats();
	});
}

void Database::clear(FnMut<void(Error)> &&done) {
	_wrapped.with([
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.clear(std::move(done));
	});
}

void Database::clearByTag(uint8 tag, FnMut<void(Error)> &&done) {
	_wrapped.with([
		tag,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.clearByTag(tag, std::move(done));
	});
}

Database::~Database() = default;

} // namespace Cache
} // namespace Storage
