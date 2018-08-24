/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "catch.hpp"

#include "storage/cache/storage_cache_database.h"
#include "storage/storage_encryption.h"
#include "storage/storage_encrypted_file.h"
#include "base/concurrent_timer.h"
#include <crl/crl.h>
#include <QtCore/QFile>
#include <QtWidgets/QApplication>
#include <thread>

using namespace Storage::Cache;

const auto key = Storage::EncryptionKey(bytes::make_vector(
	bytes::make_span("\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
").subspan(0, Storage::EncryptionKey::kSize)));

const auto name = QString("test.db");

const auto SmallSleep = [] {
	static auto SleepTime = 0;
	if (SleepTime > 5000) {
		return false;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	SleepTime += 10;
	return true;
};

QString GetBinlogPath() {
	using namespace Storage;

	QFile versionFile(name + "/version");
	while (!versionFile.open(QIODevice::ReadOnly)) {
		if (!SmallSleep()) {
			return QString();
		}
	}
	const auto bytes = versionFile.readAll();
	if (bytes.size() != 4) {
		return QString();
	}
	const auto version = *reinterpret_cast<const int32*>(bytes.data());
	return name + '/' + QString::number(version) + "/binlog";
}

const auto TestValue1 = QByteArray("testbytetestbyt");
const auto TestValue2 = QByteArray("bytetestbytetestb");

crl::semaphore Semaphore;

auto Result = Error();
const auto GetResult = [](Error error) {
	Result = error;
	Semaphore.release();
};

auto Value = QByteArray();
const auto GetValue = [](QByteArray value) {
	Value = value;
	Semaphore.release();
};

Error Open(Database &db, const Storage::EncryptionKey &key) {
	db.open(key, GetResult);
	Semaphore.acquire();
	return Result;
}

void Close(Database &db) {
	db.close([&] { Semaphore.release(); });
	Semaphore.acquire();
}

Error Clear(Database &db) {
	db.clear(GetResult);
	Semaphore.acquire();
	return Result;
}

QByteArray Get(Database &db, const Key &key) {
	db.get(key, GetValue);
	Semaphore.acquire();
	return Value;
}

Error Put(Database &db, const Key &key, const QByteArray &value) {
	db.put(key, value, GetResult);
	Semaphore.acquire();
	return Result;
}

void Remove(Database &db, const Key &key) {
	db.remove(Key{ 0, 1 }, [&] { Semaphore.release(); });
	Semaphore.acquire();
}

const auto Settings = [] {
	auto result = Database::Settings();
	result.trackEstimatedTime = false;
	result.writeBundleDelay = 1 * crl::time_type(1000);
	result.pruneTimeout = 1 * crl::time_type(1500);
	result.maxDataSize = 20;
	return result;
}();

const auto AdvanceTime = [](int32 seconds) {
	std::this_thread::sleep_for(std::chrono::milliseconds(1000) * seconds);
};

TEST_CASE("encrypted cache db", "[storage_cache_database]") {
	static auto init = [] {
		int argc = 0;
		char **argv = nullptr;
		static QCoreApplication application(argc, argv);
		static base::ConcurrentTimerEnvironment environment;
		return true;
	}();
	SECTION("writing db") {
		Database db(name, Settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		REQUIRE(Put(db, Key{ 0, 1 }, TestValue1).type == Error::Type::None);
		Close(db);
	}
	SECTION("reading and writing db") {
		Database db(name, Settings);

		REQUIRE(Open(db, key).type == Error::Type::None);
		REQUIRE((Get(db, Key{ 0, 1 }) == TestValue1));
		REQUIRE(Put(db, Key{ 1, 0 }, TestValue2).type == Error::Type::None);
		REQUIRE((Get(db, Key{ 1, 0 }) == TestValue2));
		REQUIRE(Get(db, Key{ 1, 1 }).isEmpty());
		Close(db);
	}
	SECTION("reading db") {
		Database db(name, Settings);

		REQUIRE(Open(db, key).type == Error::Type::None);
		REQUIRE((Get(db, Key{ 0, 1 }) == TestValue1));
		REQUIRE((Get(db, Key{ 1, 0 }) == TestValue2));
		Close(db);
	}
	SECTION("overwriting values") {
		Database db(name, Settings);

		REQUIRE(Open(db, key).type == Error::Type::None);
		const auto path = GetBinlogPath();
		REQUIRE((Get(db, Key{ 0, 1 }) == TestValue1));
		const auto size = QFile(path).size();
		REQUIRE(Put(db, Key{ 0, 1 }, TestValue2).type == Error::Type::None);
		const auto next = QFile(path).size();
		REQUIRE(next > size);
		REQUIRE((Get(db, Key{ 0, 1 }) == TestValue2));
		REQUIRE(Put(db, Key{ 0, 1 }, TestValue2).type == Error::Type::None);
		const auto same = QFile(path).size();
		REQUIRE(same == next);
		Close(db);
	}
}

TEST_CASE("cache db remove", "[storage_cache_database]") {
	SECTION("db remove deletes value") {
		Database db(name, Settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		REQUIRE(Put(db, Key{ 0, 1 }, TestValue1).type == Error::Type::None);
		REQUIRE(Put(db, Key{ 1, 0 }, TestValue2).type == Error::Type::None);
		db.remove(Key{ 0, 1 }, nullptr);
		REQUIRE(Get(db, Key{ 0, 1 }).isEmpty());
		REQUIRE((Get(db, Key{ 1, 0 }) == TestValue2));
		Close(db);
	}
	SECTION("db remove deletes value permanently") {
		Database db(name, Settings);

		REQUIRE(Open(db, key).type == Error::Type::None);
		REQUIRE(Get(db, Key{ 0, 1 }).isEmpty());
		REQUIRE((Get(db, Key{ 1, 0 }) == TestValue2));
		Close(db);
	}
}

TEST_CASE("cache db bundled actions", "[storage_cache_database]") {
	SECTION("db touched written lazily") {
		auto settings = Settings;
		settings.trackEstimatedTime = true;
		Database db(name, settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		const auto path = GetBinlogPath();
		REQUIRE(Put(db, Key{ 0, 1 }, TestValue1).type == Error::Type::None);
		const auto size = QFile(path).size();
		REQUIRE((Get(db, Key{ 0, 1 }) == TestValue1));
		REQUIRE(QFile(path).size() == size);
		AdvanceTime(2);
		REQUIRE(QFile(path).size() > size);
		Close(db);
	}
	SECTION("db touched written on close") {
		auto settings = Settings;
		settings.trackEstimatedTime = true;
		Database db(name, settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		const auto path = GetBinlogPath();
		REQUIRE(Put(db, Key{ 0, 1 }, TestValue1).type == Error::Type::None);
		const auto size = QFile(path).size();
		REQUIRE((Get(db, Key{ 0, 1 }) == TestValue1));
		REQUIRE(QFile(path).size() == size);
		Close(db);
		REQUIRE(QFile(path).size() > size);
	}
	SECTION("db remove written lazily") {
		Database db(name, Settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		const auto path = GetBinlogPath();
		REQUIRE(Put(db, Key{ 0, 1 }, TestValue1).type == Error::Type::None);
		const auto size = QFile(path).size();
		Remove(db, Key{ 0, 1 });
		REQUIRE(QFile(path).size() == size);
		AdvanceTime(2);
		REQUIRE(QFile(path).size() > size);
		Close(db);
	}
	SECTION("db remove written on close") {
		Database db(name, Settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		const auto path = GetBinlogPath();
		REQUIRE(Put(db, Key{ 0, 1 }, TestValue1).type == Error::Type::None);
		const auto size = QFile(path).size();
		Remove(db, Key{ 0, 1 });
		REQUIRE(QFile(path).size() == size);
		Close(db);
		REQUIRE(QFile(path).size() > size);
	}
}

TEST_CASE("cache db limits", "[storage_cache_database]") {
	SECTION("db both limit") {
		auto settings = Settings;
		settings.trackEstimatedTime = true;
		settings.totalSizeLimit = 17 * 3 + 1;
		settings.totalTimeLimit = 4;
		Database db(name, settings);

		db.clear(nullptr);
		db.open(key, nullptr);
		db.put(Key{ 0, 1 }, TestValue1, nullptr);
		db.put(Key{ 1, 0 }, TestValue2, nullptr);
		AdvanceTime(2);
		db.get(Key{ 1, 0 }, nullptr);
		AdvanceTime(3);
		db.put(Key{ 1, 1 }, TestValue1, nullptr);
		db.put(Key{ 2, 0 }, TestValue2, nullptr);
		db.put(Key{ 0, 2 }, TestValue1, nullptr);
		AdvanceTime(2);
		REQUIRE(Get(db, Key{ 0, 1 }).isEmpty());
		REQUIRE(Get(db, Key{ 1, 0 }).isEmpty());
		REQUIRE((Get(db, Key{ 1, 1 }) == TestValue1));
		REQUIRE((Get(db, Key{ 2, 0 }) == TestValue2));
		REQUIRE((Get(db, Key{ 0, 2 }) == TestValue1));
		Close(db);
	}
	SECTION("db size limit") {
		auto settings = Settings;
		settings.trackEstimatedTime = true;
		settings.totalSizeLimit = 17 * 3 + 1;
		Database db(name, settings);

		db.clear(nullptr);
		db.open(key, nullptr);
		db.put(Key{ 0, 1 }, TestValue1, nullptr);
		AdvanceTime(2);
		db.put(Key{ 1, 0 }, TestValue2, nullptr);
		AdvanceTime(2);
		db.put(Key{ 1, 1 }, TestValue1, nullptr);
		db.get(Key{ 0, 1 }, nullptr);
		AdvanceTime(2);
		db.put(Key{ 2, 0 }, TestValue2, nullptr);

		// Removing { 1, 0 } will be scheduled.
		REQUIRE((Get(db, Key{ 0, 1 }) == TestValue1));
		REQUIRE((Get(db, Key{ 1, 1 }) == TestValue1));
		REQUIRE((Get(db, Key{ 2, 0 }) == TestValue2));
		AdvanceTime(2);

		// Removing { 1, 0 } performed.
		REQUIRE(Get(db, Key{ 1, 0 }).isEmpty());
		REQUIRE((Get(db, Key{ 1, 1 }) == TestValue1));
		db.put(Key{ 0, 2 }, TestValue1, nullptr);
		REQUIRE(Put(db, Key{ 2, 2 }, TestValue2).type == Error::Type::None);

		// Removing { 0, 1 } and { 2, 0 } will be scheduled.
		AdvanceTime(2);

		// Removing { 0, 1 } and { 2, 0 } performed.
		REQUIRE(Get(db, Key{ 0, 1 }).isEmpty());
		REQUIRE(Get(db, Key{ 2, 0 }).isEmpty());
		REQUIRE((Get(db, Key{ 1, 1 }) == TestValue1));
		REQUIRE((Get(db, Key{ 0, 2 }) == TestValue1));
		REQUIRE((Get(db, Key{ 2, 2 }) == TestValue2));
		Close(db);
	}
	SECTION("db time limit") {
		auto settings = Settings;
		settings.trackEstimatedTime = true;
		settings.totalTimeLimit = 3;
		Database db(name, settings);

		db.clear(nullptr);
		db.open(key, nullptr);
		db.put(Key{ 0, 1 }, TestValue1, nullptr);
		db.put(Key{ 1, 0 }, TestValue2, nullptr);
		db.put(Key{ 1, 1 }, TestValue1, nullptr);
		db.put(Key{ 2, 0 }, TestValue2, nullptr);
		AdvanceTime(1);
		db.get(Key{ 1, 0 }, nullptr);
		db.get(Key{ 1, 1 }, nullptr);
		AdvanceTime(1);
		db.get(Key{ 1, 0 }, nullptr);
		db.get(Key{ 0, 1 }, nullptr);
		AdvanceTime(1);
		db.get(Key{ 1, 0 }, nullptr);
		db.get(Key{ 0, 1 }, nullptr);
		AdvanceTime(3);
		REQUIRE(Get(db, Key{ 2, 0 }).isEmpty());
		REQUIRE(Get(db, Key{ 1, 1 }).isEmpty());
		REQUIRE((Get(db, Key{ 1, 0 }) == TestValue2));
		REQUIRE((Get(db, Key{ 0, 1 }) == TestValue1));
		Close(db);
	}
}
