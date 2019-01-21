/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "catch.hpp"

#include "storage/storage_encrypted_file.h"

#include <QtCore/QThread>
#include <QtCore/QCoreApplication>

#ifdef Q_OS_WIN
#include "platform/win/windows_dlls.h"
#endif // Q_OS_WIN

#include <QtCore/QProcess>

#include <thread>
#ifdef Q_OS_MAC
#include <mach-o/dyld.h>
#elif defined Q_OS_LINUX // Q_OS_MAC
#include <unistd.h>
#endif // Q_OS_MAC || Q_OS_LINUX

extern int (*TestForkedMethod)();

const auto Key = Storage::EncryptionKey(bytes::make_vector(
	bytes::make_span("\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567\
").subspan(0, Storage::EncryptionKey::kSize)));

const auto Name = QString("test.file");

const auto Test1 = bytes::make_span("testbytetestbyte").subspan(0, 16);
const auto Test2 = bytes::make_span("bytetestbytetest").subspan(0, 16);

struct ForkInit {
	static int Method() {
		Storage::File file;
		const auto result = file.open(
			Name,
			Storage::File::Mode::ReadAppend,
			Key);
		if (result != Storage::File::Result::Success) {
			return -1;
		}

		auto data = bytes::vector(16);
		const auto read = file.read(data);
		if (read != data.size()) {
			return -1;
		} else if (data != bytes::make_vector(Test1)) {
			return -1;
		}

		if (!file.write(data) || !file.flush()) {
			return -1;
		}
#ifdef _DEBUG
		while (true) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
#else // _DEBUG
		std::this_thread::sleep_for(std::chrono::seconds(1));
		return 0;
#endif // _DEBUG
	}
	ForkInit() {
#ifdef Q_OS_WIN
		Platform::Dlls::start();
#endif // Q_OS_WIN

		TestForkedMethod = &ForkInit::Method;
	}

};

ForkInit ForkInitializer;
QProcess ForkProcess;

TEST_CASE("simple encrypted file", "[storage_encrypted_file]") {
	SECTION("writing file") {
		Storage::File file;
		const auto result = file.open(
			Name,
			Storage::File::Mode::Write,
			Key);
		REQUIRE(result == Storage::File::Result::Success);

		auto data = bytes::make_vector(Test1);
		const auto success = file.write(data);
		REQUIRE(success);
	}
	SECTION("reading and writing file") {
		Storage::File file;
		const auto result = file.open(
			Name,
			Storage::File::Mode::ReadAppend,
			Key);
		REQUIRE(result == Storage::File::Result::Success);

		auto data = bytes::vector(Test1.size());
		const auto read = file.read(data);
		REQUIRE(read == data.size());
		REQUIRE(data == bytes::make_vector(Test1));

		data = bytes::make_vector(Test2);
		const auto success = file.write(data);
		REQUIRE(success);
	}
	SECTION("offset and seek") {
		Storage::File file;
		const auto result = file.open(
			Name,
			Storage::File::Mode::ReadAppend,
			Key);
		REQUIRE(result == Storage::File::Result::Success);
		REQUIRE(file.offset() == 0);
		REQUIRE(file.size() == Test1.size() + Test2.size());

		const auto success1 = file.seek(Test1.size());
		REQUIRE(success1);
		REQUIRE(file.offset() == Test1.size());

		auto data = bytes::vector(Test2.size());
		const auto read = file.read(data);
		REQUIRE(read == data.size());
		REQUIRE(data == bytes::make_vector(Test2));
		REQUIRE(file.offset() == Test1.size() + Test2.size());
		REQUIRE(file.size() == Test1.size() + Test2.size());

		const auto success2 = file.seek(Test1.size());
		REQUIRE(success2);
		REQUIRE(file.offset() == Test1.size());

		data = bytes::make_vector(Test1);
		const auto success3 = file.write(data) && file.write(data);
		REQUIRE(success3);

		REQUIRE(file.offset() == 3 * Test1.size());
		REQUIRE(file.size() == 3 * Test1.size());
	}
	SECTION("reading file") {
		Storage::File file;

		const auto result = file.open(
			Name,
			Storage::File::Mode::Read,
			Key);
		REQUIRE(result == Storage::File::Result::Success);

		auto data = bytes::vector(32);
		const auto read = file.read(data);
		REQUIRE(read == data.size());
		REQUIRE(data == bytes::concatenate(Test1, Test1));
	}
	SECTION("moving file") {
		const auto result = Storage::File::Move(Name, "other.file");
		REQUIRE(result);
	}
}

TEST_CASE("two process encrypted file", "[storage_encrypted_file]") {
	SECTION("writing file") {
		Storage::File file;
		const auto result = file.open(
			Name,
			Storage::File::Mode::Write,
			Key);
		REQUIRE(result == Storage::File::Result::Success);

		auto data = bytes::make_vector(Test1);
		const auto success = file.write(data);
		REQUIRE(success);
	}
	SECTION("access from subprocess") {
		SECTION("start subprocess") {
			const auto application = []() -> QString {
#ifdef Q_OS_WIN
				return "tests_storage.exe";
#else // Q_OS_WIN
				constexpr auto kMaxPath = 1024;
				char result[kMaxPath] = { 0 };
				uint32_t size = kMaxPath;
#ifdef Q_OS_MAC
				if (_NSGetExecutablePath(result, &size) == 0) {
					return result;
				}
#else // Q_OS_MAC
				auto count = readlink("/proc/self/exe", result, size);
				if (count > 0) {
					return result;
				}
#endif // Q_OS_MAC
				return "tests_storage";
#endif // Q_OS_WIN
			}();

			ForkProcess.start(application + " --forked");
			const auto started = ForkProcess.waitForStarted();
			REQUIRE(started);
		}
		SECTION("read subprocess result") {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));

			Storage::File file;

			const auto result = file.open(
				Name,
				Storage::File::Mode::Read,
				Key);
			REQUIRE(result == Storage::File::Result::Success);

			auto data = bytes::vector(32);
			const auto read = file.read(data);
			REQUIRE(read == data.size());
			REQUIRE(data == bytes::concatenate(Test1, Test1));
		}
		SECTION("take subprocess result") {
			REQUIRE(ForkProcess.state() == QProcess::Running);

			Storage::File file;

			const auto result = file.open(
				Name,
				Storage::File::Mode::ReadAppend,
				Key);
			REQUIRE(result == Storage::File::Result::Success);

			auto data = bytes::vector(32);
			const auto read = file.read(data);
			REQUIRE(read == data.size());
			REQUIRE(data == bytes::concatenate(Test1, Test1));

			const auto finished = ForkProcess.waitForFinished(0);
			REQUIRE(finished);
			REQUIRE(ForkProcess.state() == QProcess::NotRunning);
		}
	}

}
