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

const auto DisableLimitsTests = true;
const auto DisableCompactTests = false;
const auto DisableLargeTest = false;

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
	db.remove(key, [&] { Semaphore.release(); });
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

TEST_CASE("init timers", "[storage_cache_database]") {
	static auto init = [] {
		int argc = 0;
		char **argv = nullptr;
		static QCoreApplication application(argc, argv);
		static base::ConcurrentTimerEnvironment environment;
		return true;
	}();
}

TEST_CASE("compacting db", "[storage_cache_database]") {
	if (DisableCompactTests) {
		return;
	}
	const auto write = [](Database &db, uint32 from, uint32 till, QByteArray base) {
		for (auto i = from; i != till; ++i) {
			auto value = base;
			value[0] = char('A') + i;
			const auto result = Put(db, Key{ i, i + 1 }, value);
			REQUIRE(result.type == Error::Type::None);
		}
	};
	const auto put = [&](Database &db, uint32 from, uint32 till) {
		write(db, from, till, TestValue1);
	};
	const auto reput = [&](Database &db, uint32 from, uint32 till) {
		write(db, from, till, TestValue2);
	};
	const auto remove = [](Database &db, uint32 from, uint32 till) {
		for (auto i = from; i != till; ++i) {
			Remove(db, Key{ i, i + 1 });
		}
	};
	const auto get = [](Database &db, uint32 from, uint32 till) {
		for (auto i = from; i != till; ++i) {
			db.get(Key{ i, i + 1 }, nullptr);
		}
	};
	const auto check = [](Database &db, uint32 from, uint32 till, QByteArray base) {
		for (auto i = from; i != till; ++i) {
			auto value = base;
			if (!value.isEmpty()) {
				value[0] = char('A') + i;
			}
			const auto result = Get(db, Key{ i, i + 1 });
			REQUIRE((result == value));
		}
	};
	SECTION("simple compact with min size") {
		auto settings = Settings;
		settings.writeBundleDelay = crl::time_type(100);
		settings.readBlockSize = 512;
		settings.maxBundledRecords = 5;
		settings.compactAfterExcess = (3 * (16 * 5 + 16) + 15 * 32) / 2;
		settings.compactAfterFullSize = (sizeof(details::BasicHeader)
			+ 40 * 32) / 2
			+ settings.compactAfterExcess;
		Database db(name, settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		put(db, 0, 30);
		remove(db, 0, 15);
		put(db, 30, 40);
		reput(db, 15, 29);
		AdvanceTime(1);
		const auto path = GetBinlogPath();
		const auto size = QFile(path).size();
		reput(db, 29, 30); // starts compactor
		AdvanceTime(2);
		REQUIRE(QFile(path).size() < size);
		remove(db, 30, 35);
		reput(db, 35, 37);
		put(db, 15, 20);
		put(db, 40, 45);

		const auto fullcheck = [&] {
			check(db, 0, 15, {});
			check(db, 15, 20, TestValue1);
			check(db, 20, 30, TestValue2);
			check(db, 30, 35, {});
			check(db, 35, 37, TestValue2);
			check(db, 37, 45, TestValue1);
		};
		fullcheck();
		Close(db);

		REQUIRE(Open(db, key).type == Error::Type::None);
		fullcheck();
		Close(db);
	}
	SECTION("simple compact without min size") {
		auto settings = Settings;
		settings.writeBundleDelay = crl::time_type(100);
		settings.readBlockSize = 512;
		settings.maxBundledRecords = 5;
		settings.compactAfterExcess = 3 * (16 * 5 + 16) + 15 * 32;
		Database db(name, settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		put(db, 0, 30);
		remove(db, 0, 15);
		put(db, 30, 40);
		reput(db, 15, 29);
		AdvanceTime(1);
		const auto path = GetBinlogPath();
		const auto size = QFile(path).size();
		reput(db, 29, 30); // starts compactor
		AdvanceTime(2);
		REQUIRE(QFile(path).size() < size);
		remove(db, 30, 35);
		reput(db, 35, 37);
		put(db, 15, 20);
		put(db, 40, 45);

		const auto fullcheck = [&] {
			check(db, 0, 15, {});
			check(db, 15, 20, TestValue1);
			check(db, 20, 30, TestValue2);
			check(db, 30, 35, {});
			check(db, 35, 37, TestValue2);
			check(db, 37, 45, TestValue1);
		};
		fullcheck();
		Close(db);

		REQUIRE(Open(db, key).type == Error::Type::None);
		fullcheck();
		Close(db);
	}
	SECTION("double compact") {
		auto settings = Settings;
		settings.writeBundleDelay = crl::time_type(100);
		settings.readBlockSize = 512;
		settings.maxBundledRecords = 5;
		settings.compactAfterExcess = 3 * (16 * 5 + 16) + 15 * 32;
		Database db(name, settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		put(db, 0, 30);
		remove(db, 0, 15);
		reput(db, 15, 29);
		AdvanceTime(1);
		const auto path = GetBinlogPath();
		const auto size1 = QFile(path).size();
		reput(db, 29, 30); // starts compactor
		AdvanceTime(2);
		REQUIRE(QFile(path).size() < size1);
		put(db, 30, 45);
		remove(db, 20, 35);
		put(db, 15, 20);
		reput(db, 35, 44);
		const auto size2 = QFile(path).size();
		reput(db, 44, 45); // starts compactor
		AdvanceTime(2);
		const auto after = QFile(path).size();
		REQUIRE(after < size1);
		REQUIRE(after < size2);
		const auto fullcheck = [&] {
			check(db, 0, 15, {});
			check(db, 15, 20, TestValue1);
			check(db, 20, 35, {});
			check(db, 35, 45, TestValue2);
		};
		fullcheck();
		Close(db);

		REQUIRE(Open(db, key).type == Error::Type::None);
		fullcheck();
		Close(db);
	}
	SECTION("time tracking compact") {
		auto settings = Settings;
		settings.writeBundleDelay = crl::time_type(100);
		settings.trackEstimatedTime = true;
		settings.readBlockSize = 512;
		settings.maxBundledRecords = 5;
		settings.compactAfterExcess = 6 * (16 * 5 + 16)
			+ 3 * (16 * 5 + 16)
			+ 15 * 48
			+ 3 * (16 * 5 + 16)
			+ (16 * 1 + 16);
		Database db(name, settings);

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		put(db, 0, 30);
		get(db, 0, 30);
		//AdvanceTime(1); get's will be written instantly becase !(30 % 5)
		remove(db, 0, 15);
		reput(db, 15, 30);
		get(db, 0, 30);
		AdvanceTime(1);
		const auto path = GetBinlogPath();
		const auto size = QFile(path).size();
		get(db, 29, 30); // starts compactor delayed
		AdvanceTime(2);
		REQUIRE(QFile(path).size() < size);
		const auto fullcheck = [&] {
			check(db, 15, 30, TestValue2);
		};
		fullcheck();
		Close(db);

		REQUIRE(Open(db, key).type == Error::Type::None);
		fullcheck();
		Close(db);
	}
}

TEST_CASE("encrypted cache db", "[storage_cache_database]") {
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
	SECTION("reading db in many chunks") {
		auto settings = Settings;
		settings.readBlockSize = 512;
		settings.maxBundledRecords = 5;
		settings.trackEstimatedTime = true;
		Database db(name, settings);

		const auto count = 30U;

		REQUIRE(Clear(db).type == Error::Type::None);
		REQUIRE(Open(db, key).type == Error::Type::None);
		for (auto i = 0U; i != count; ++i) {
			auto value = TestValue1;
			value[0] = char('A') + i;
			const auto result = Put(db, Key{ i, i * 2 }, value);
			REQUIRE(result.type == Error::Type::None);
		}
		Close(db);

		REQUIRE(Open(db, key).type == Error::Type::None);
		for (auto i = 0U; i != count; ++i) {
			auto value = TestValue1;
			value[0] = char('A') + i;
			REQUIRE((Get(db, Key{ i, i * 2 }) == value));
		}
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
		Remove(db, Key{ 0, 1 });
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
	if (DisableLimitsTests) {
		return;
	}
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
