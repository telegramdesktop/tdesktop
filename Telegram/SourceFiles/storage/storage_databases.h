/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/cache/storage_cache_database.h"
#include "base/binary_guard.h"

namespace Storage {
namespace Cache {
namespace details {
struct Settings;
} // namespace details
class Database;
} // namespace Cache

class Databases;

class DatabasePointer {
public:
	DatabasePointer(const DatabasePointer &other) = delete;
	DatabasePointer(DatabasePointer &&other);
	DatabasePointer &operator=(const DatabasePointer &other) = delete;
	DatabasePointer &operator=(DatabasePointer &&other);
	~DatabasePointer();

	Cache::Database *get() const;
	Cache::Database &operator*() const;
	Cache::Database *operator->() const;
	explicit operator bool() const;

private:
	friend class Databases;

	DatabasePointer(
		not_null<Databases*> owner,
		const std::unique_ptr<Cache::Database> &value);
	void destroy();

	Cache::Database *_value = nullptr;
	not_null<Databases*> _owner;

};

class Databases {
public:
	DatabasePointer get(
		const QString &path,
		const Cache::details::Settings &settings);

private:
	friend class DatabasePointer;

	struct Kept {
		Kept(std::unique_ptr<Cache::Database> &&database);

		std::unique_ptr<Cache::Database> database;
		base::binary_guard destroying;
	};

	void destroy(Cache::Database *database);

	std::map<QString, Kept> _map;

};

} // namespace Storage
