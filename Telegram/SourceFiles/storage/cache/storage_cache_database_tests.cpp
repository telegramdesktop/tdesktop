/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "catch.hpp"

#include "storage/cache/storage_cache_database.h"
#include "storage/storage_encryption.h"
#include <crl/crl.h>
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

const auto Settings = Database::Settings{ 1024 };

TEST_CASE("encrypted cache db", "[storage_cache_database]") {
	SECTION("writing db") {
		Database db(name, Settings);

		db.clear(GetResult);
		Semaphore.acquire();
		REQUIRE(Result.type == Error::Type::None);

		db.open(key, GetResult);
		Semaphore.acquire();
		REQUIRE(Result.type == Error::Type::None);

		db.put(Key{ 0, 1 }, TestValue1, GetResult);
		Semaphore.acquire();
		REQUIRE(Result.type == Error::Type::None);

		db.close([&] { Semaphore.release(); });
		Semaphore.acquire();
	}
	SECTION("reading and writing db") {
		Database db(name, Settings);

		db.open(key, GetResult);
		Semaphore.acquire();
		REQUIRE(Result.type == Error::Type::None);

		db.get(Key{ 0, 1 }, GetValue);
		Semaphore.acquire();
		REQUIRE((Value == TestValue1));

		db.put(Key{ 1, 0 }, TestValue2, GetResult);
		Semaphore.acquire();
		REQUIRE(Result.type == Error::Type::None);

		db.get(Key{ 1, 0 }, GetValue);
		Semaphore.acquire();
		REQUIRE((Value == TestValue2));

		db.get(Key{ 1, 1 }, GetValue);
		Semaphore.acquire();
		REQUIRE(Value.isEmpty());

		db.close([&] { Semaphore.release(); });
		Semaphore.acquire();
	}
	SECTION("reading db") {
		Database db(name, Settings);

		db.open(key, GetResult);
		Semaphore.acquire();
		REQUIRE(Result.type == Error::Type::None);

		db.get(Key{ 0, 1 }, GetValue);
		Semaphore.acquire();
		REQUIRE((Value == TestValue1));

		db.get(Key{ 1, 0 }, GetValue);
		Semaphore.acquire();
		REQUIRE((Value == TestValue2));
	}
}
