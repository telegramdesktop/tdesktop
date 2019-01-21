/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_databases.h"

#include "storage/cache/storage_cache_database.h"

namespace Storage {

DatabasePointer::DatabasePointer(
	not_null<Databases*> owner,
	const std::unique_ptr<Cache::Database> &value)
: _value(value.get())
, _owner(owner) {
}

DatabasePointer::DatabasePointer(DatabasePointer &&other)
: _value(base::take(other._value))
, _owner(other._owner) {
}

DatabasePointer &DatabasePointer::operator=(DatabasePointer &&other) {
	if (this != &other) {
		destroy();
		_owner = other._owner;
		_value = base::take(other._value);
	}
	return *this;
}

DatabasePointer::~DatabasePointer() {
	destroy();
}

Cache::Database *DatabasePointer::get() const {
	return _value;
}

Cache::Database &DatabasePointer::operator*() const {
	Expects(_value != nullptr);

	return *get();
}

Cache::Database *DatabasePointer::operator->() const {
	Expects(_value != nullptr);

	return get();
}

DatabasePointer::operator bool() const {
	return get() != nullptr;
}

void DatabasePointer::destroy() {
	if (const auto value = base::take(_value)) {
		_owner->destroy(value);
	}
}

Databases::Kept::Kept(std::unique_ptr<Cache::Database> &&database)
: database(std::move(database)) {
}

DatabasePointer Databases::get(
		const QString &path,
		const Cache::details::Settings &settings) {
	if (const auto i = _map.find(path); i != end(_map)) {
		auto &kept = i->second;
		Assert(kept.destroying.alive());
		kept.destroying.kill();
		kept.database->reconfigure(settings);
		return DatabasePointer(this, kept.database);
	}
	const auto [i, ok] = _map.emplace(
		path,
		std::make_unique<Cache::Database>(path, settings));
	return DatabasePointer(this, i->second.database);
}

void Databases::destroy(Cache::Database *database) {
	for (auto &entry : _map) {
		const auto &path = entry.first; // Need to capture it in lambda.
		auto &kept = entry.second;
		if (kept.database.get() == database) {
			Assert(!kept.destroying.alive());
			auto [first, second] = base::make_binary_guard();
			kept.destroying = std::move(first);
			database->close();
			database->waitForCleaner([=, guard = std::move(second)]() mutable {
				crl::on_main([=, guard = std::move(guard)]{
					if (!guard.alive()) {
						return;
					}
					_map.erase(path);
				});
			});
		}
	}
}

} // namespace Storage
