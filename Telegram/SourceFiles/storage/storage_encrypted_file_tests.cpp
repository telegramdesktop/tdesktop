/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "catch.hpp"

#include "storage/storage_encrypted_file.h"

const auto key = Storage::EncryptionKey(bytes::make_vector(
	bytes::make_span("\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
").subspan(0, Storage::EncryptionKey::kSize)));

TEST_CASE("simple encrypted file", "[storage_encrypted_file]") {
	const auto name = QString("simple.test");
	const auto test = bytes::make_span("testbytetestbyte").subspan(0, 16);

	SECTION("writing file") {
		Storage::File file;
		const auto result = file.open(
			name,
			Storage::File::Mode::Write,
			key);
		REQUIRE(result == Storage::File::Result::Success);

		auto data = bytes::make_vector(test);
		const auto written = file.write(data);
		REQUIRE(written == data.size());
	}
	SECTION("reading and writing file") {
		Storage::File file;
		const auto result = file.open(
			name,
			Storage::File::Mode::ReadAppend,
			key);
		REQUIRE(result == Storage::File::Result::Success);

		auto data = bytes::vector(16);
		const auto read = file.read(data);
		REQUIRE(read == data.size());
		REQUIRE(data == bytes::make_vector(test));

		const auto written = file.write(data);
		REQUIRE(written == data.size());
	}
	SECTION("reading file") {
		Storage::File file;

		const auto result = file.open(
			name,
			Storage::File::Mode::Read,
			key);
		REQUIRE(result == Storage::File::Result::Success);

		auto data = bytes::vector(32);
		const auto read = file.read(data);
		REQUIRE(read == data.size());
		REQUIRE(data == bytes::concatenate(test, test));
	}
}

TEST_CASE("two process encrypted file", "[storage_encrypted_file]") {

}
