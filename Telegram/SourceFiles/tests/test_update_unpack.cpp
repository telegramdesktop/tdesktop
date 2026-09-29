/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_unpack.h"

#include "core/update_verify.h"

#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>

#include <cstring>
#include <iostream>
#include <vector>

#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED
#include <LzmaLib.h>
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
#include <lzma.h>
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED

namespace {

using namespace Core::Updates;

constexpr auto kVersion = quint32(7002009);
constexpr auto kSignedVersion = MakeUpdateVersion(kVersion, 0);

int FailedChecks = 0;
int TotalChecks = 0;
int TotalCases = 0;

struct Entry {
	QString name;
	QByteArray bytes;
	bool executable = false;
};

void Check(bool condition, const char *name) {
	++TotalChecks;
	if (!condition) {
		++FailedChecks;
		std::cout << "FAILED: " << name << std::endl;
	}
}

void Case(const char *name) {
	++TotalCases;
	std::cout << "CASE: " << name << std::endl;
}

[[nodiscard]] QByteArray Serialize(const std::vector<Entry> &entries) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kVersion << quint32(entries.size());
	for (const auto &entry : entries) {
		stream << entry.name << quint32(entry.bytes.size()) << entry.bytes;
#ifndef Q_OS_WIN
		stream << entry.executable;
#endif // !Q_OS_WIN
	}
	Check(stream.status() == QDataStream::Ok, "fixture serialization");
	return result;
}

[[nodiscard]] QByteArray Compress(const QByteArray &input) {
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED
	constexpr auto kPropsSize = LZMA_PROPS_SIZE;
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	constexpr auto kPropsSize = 0;
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	constexpr auto kHeaderSize = kPropsSize + sizeof(qint32);
	auto result = QByteArray(input.size() * 2 + 1024, Qt::Uninitialized);
	const auto originalSize = qint32(input.size());
	memcpy(result.data() + kPropsSize, &originalSize, sizeof(originalSize));
	auto compressedSize = size_t(result.size() - kHeaderSize);
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED
	auto propsSize = size_t(kPropsSize);
	const auto status = LzmaCompress(
		reinterpret_cast<uchar*>(result.data() + kHeaderSize),
		&compressedSize,
		reinterpret_cast<const uchar*>(input.constData()),
		input.size(),
		reinterpret_cast<uchar*>(result.data()),
		&propsSize,
		5,
		0,
		-1,
		-1,
		-1,
		-1,
		1);
	Check(status == SZ_OK, "fixture LZMA compression");
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	auto position = size_t(0);
	const auto status = lzma_easy_buffer_encode(
		6,
		LZMA_CHECK_CRC64,
		nullptr,
		reinterpret_cast<const uint8_t*>(input.constData()),
		input.size(),
		reinterpret_cast<uint8_t*>(result.data() + kHeaderSize),
		&position,
		compressedSize);
	Check(status == LZMA_OK, "fixture XZ compression");
	compressedSize = position;
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	result.resize(kHeaderSize + compressedSize);
	return result;
}

[[nodiscard]] QByteArray Read(const QString &path) {
	auto file = QFile(path);
	Check(file.open(QIODevice::ReadOnly), "open extracted file or sentinel");
	return file.readAll();
}

[[nodiscard]] QString NewDirectory(const QString &parent) {
	static auto serial = 0;
	const auto result = parent + '/' + QString::number(++serial);
	Check(QDir().mkdir(result), "fresh extraction directory");
	return result;
}

void Reject(
		const QByteArray &payload,
		const QString &parent,
		const QString &sentinel,
		const char *label) {
	Case(label);
	auto error = QString();
	Check(!ExtractUpdateFiles(
		payload,
		kSignedVersion,
		NewDirectory(parent),
		&error), "malformed payload rejected");
	Check(!error.isEmpty(), "rejection provides an error");
	Check(Read(sentinel) == "outside unchanged", "external sentinel unchanged");
}

void ReplaceWord(QByteArray &data, qsizetype offset, quint32 value) {
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << value;
	data.replace(offset, bytes.size(), bytes);
}

void TestFiles(const QString &parent, const QString &sentinel) {
	Case("valid multiple files, empty file, nested paths and executable modes");
	const auto entries = std::vector<Entry>{
		{ QString::fromLatin1("Telegram"), "main program", true },
		{ QString::fromLatin1("Updater"), "helper program", true },
		{ QString::fromLatin1("resources/a file"), QByteArray("a\0b", 3) },
		{ QString::fromLatin1("resources/empty"), QByteArray() },
	};
	const auto payload = Serialize(entries);
	const auto directory = NewDirectory(parent);
	auto verified = VerifiedUpdate();
	verified.envelope.version = kSignedVersion;
	verified.envelope.payload = Compress(payload);
	auto error = QString();
	const auto decoded = DecompressUpdatePayload(verified, &error);
	Check(decoded && *decoded == payload, "exact decompressed fixture bytes");
	Check(decoded && ExtractUpdateFiles(
		*decoded,
		kSignedVersion,
		directory,
		&error), "valid fixture extracts");
	for (const auto &entry : entries) {
		const auto path = directory + '/' + entry.name;
		Check(Read(path) == entry.bytes, "extracted bytes match independent fixture");
#ifndef Q_OS_WIN
		const auto permissions = QFileInfo(path).permissions();
		const auto executable = QFileDevice::ExeOwner
			| QFileDevice::ExeGroup
			| QFileDevice::ExeOther;
		Check((permissions & executable) == (entry.executable
			? executable
			: QFileDevice::Permissions()), "all executable mode bits match");
#endif // !Q_OS_WIN
	}
	Check(!QFileInfo::exists(directory + QLatin1String("/ready")),
		"extractor does not emit ready marker");
	Check(!QFileInfo::exists(directory + QLatin1String("/tdata/version")),
		"extractor does not emit version marker");

	auto changed = payload;
	ReplaceWord(changed, 0, kVersion + 1);
	Reject(changed, parent, sentinel, "inner version mismatch");
	changed = payload;
	ReplaceWord(changed, 4, 0);
	Reject(changed, parent, sentinel, "empty file count");
	changed = payload;
	ReplaceWord(changed, 4, 0xFFFFFFFF);
	Reject(changed, parent, sentinel, "absurd file count");
	changed = payload;
	ReplaceWord(changed, 8, 0xFFFFFFFE);
	Reject(changed, parent, sentinel, "oversized filename allocation");
	changed = payload;
	ReplaceWord(changed, 8, 3);
	Reject(changed, parent, sentinel, "odd UTF-16 filename size");
	changed = payload;
	const auto sizeOffset = 12 + entries.front().name.size() * 2;
	ReplaceWord(changed, sizeOffset, 0xFFFFFFFE);
	Reject(changed, parent, sentinel, "inconsistent declared file size");
	changed = payload;
	ReplaceWord(changed, sizeOffset, 0xFFFFFFFE);
	ReplaceWord(changed, sizeOffset + 4, 0xFFFFFFFE);
	Reject(changed, parent, sentinel, "oversized byte array allocation");
	Reject(payload.first(3), parent, sentinel, "truncated header");
	Reject(payload.first(12), parent, sentinel, "truncated entry metadata");
	Reject(payload.chopped(1), parent, sentinel, "truncated final entry");
	Reject(payload + 'x', parent, sentinel, "trailing payload bytes");
#ifndef Q_OS_WIN
	changed = payload;
	changed[sizeOffset + 8 + entries.front().bytes.size()] = char(2);
	Reject(changed, parent, sentinel, "invalid executable flag");
#endif // !Q_OS_WIN

	const auto names = std::vector<QString>{
		QString(),
		QString::fromLatin1("/absolute"),
		sentinel,
		QString::fromLatin1("../sentinel"),
		QString::fromLatin1("nested/../../sentinel"),
		QString::fromLatin1("./file"),
		QString::fromLatin1("nested/./file"),
		QString::fromLatin1("nested/../file"),
		QString::fromLatin1("nested//file"),
		QString::fromLatin1("nested/"),
		QString::fromLatin1("C:/file"),
		QString::fromLatin1("C:file"),
		QString::fromLatin1("//server/share/file"),
		QString::fromLatin1("\\\\server\\share\\file"),
		QString::fromLatin1("nested\\file"),
		QString::fromLatin1("file:stream"),
		QString::fromLatin1("file."),
		QString::fromLatin1("file "),
		QString::fromLatin1("NUL.txt"),
		QString::fromLatin1("COM1"),
		QString::fromLatin1("CON .txt"),
		QString::fromLatin1("CONOUT$"),
		QString::fromUtf8("COM\xC2\xB9"),
		QString::fromLatin1("file\0alias", 10),
		QString(QChar(0xD800)),
	};
	for (const auto &name : names) {
		Reject(Serialize({ { name, "unsafe" } }),
			parent,
			sentinel,
			"unsafe relative name");
	}
	const auto pairs = std::vector<std::pair<QString, QString>>{
		{ QString::fromLatin1("same"), QString::fromLatin1("same") },
		{ QString::fromLatin1("Name"), QString::fromLatin1("name") },
		{ QString::fromLatin1("file"), QString::fromLatin1("file/child") },
		{ QString::fromLatin1("dir/child"), QString::fromLatin1("dir") },
		{ QString::fromLatin1("Dir/child"), QString::fromLatin1("dir") },
		{ QString::fromUtf8("\xC3\xA9"), QString::fromUtf8("e\xCC\x81") },
	};
	for (const auto &[first, second] : pairs) {
		const auto duplicateDirectory = NewDirectory(parent);
		Case("duplicate or conflicting file and directory names");
		Check(!ExtractUpdateFiles(Serialize({
			{ first, "first" },
			{ second, "second" },
		}), kSignedVersion, duplicateDirectory), "conflict rejected");
		Check(Read(duplicateDirectory + '/' + first) == "first",
			"conflict did not overwrite the first file");
		Check(Read(sentinel) == "outside unchanged", "external sentinel unchanged");
	}
	Case("existing output is never overwritten");
	Check(!ExtractUpdateFiles(payload, kSignedVersion, directory),
		"nonempty destination rejected");
	Check(Read(directory + QLatin1String("/Telegram")) == "main program",
		"existing output is unchanged");
	Case("destination parent is a file");
	Check(!ExtractUpdateFiles(payload, kSignedVersion, sentinel),
		"mkdir failure returns failure");
	Check(Read(sentinel) == "outside unchanged", "external sentinel unchanged");
}

void TestCompression() {
	const auto plain = Serialize({ { QString::fromLatin1("file"), "bytes" } });
	const auto compressed = Compress(plain);
	const auto reject = [](QByteArray payload, const char *label) {
		Case(label);
		auto verified = VerifiedUpdate();
		verified.envelope.payload = std::move(payload);
		auto error = QString();
		Check(!DecompressUpdatePayload(verified, &error),
			"invalid compressed payload rejected");
		Check(!error.isEmpty(), "decompression error provided");
	};
	reject(QByteArray(), "empty compressed payload");
	reject(compressed.first(3), "truncated compression header");
	reject(compressed.chopped(8), "truncated compressed bytes");
	reject(compressed + 'x', "trailing compressed bytes");
	auto corrupt = compressed;
	corrupt[corrupt.size() / 2] ^= char(0x80);
	reject(corrupt, "corrupt compressed bytes");
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED
	constexpr auto kSizeOffset = LZMA_PROPS_SIZE;
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	constexpr auto kSizeOffset = 0;
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	for (const auto size : { qint32(0), qint32(-1), qint32(1024 * 1024 * 1024 + 1),
		qint32(plain.size() - 1), qint32(plain.size() + 1) }) {
		auto payload = compressed;
		memcpy(payload.data() + kSizeOffset, &size, sizeof(size));
		reject(payload, "invalid declared decompressed size");
	}
	Case("repeated decoder failure cleanup");
	auto verified = VerifiedUpdate();
	verified.envelope.payload = compressed.chopped(1);
	for (auto i = 0; i != 200; ++i) {
		Check(!DecompressUpdatePayload(verified), "repeated failed decoder");
	}
}

} // namespace

int main() {
	auto temporary = QTemporaryDir();
	Check(temporary.isValid(), "temporary directory created");
	if (!temporary.isValid()) {
		return 1;
	}
	const auto sentinel = temporary.path() + QLatin1String("/sentinel");
	{
		auto file = QFile(sentinel);
		Check(file.open(QIODevice::WriteOnly), "sentinel created");
		Check(file.write("outside unchanged") == 17, "sentinel written");
		Check(file.flush(), "sentinel flushed");
	}
	TestFiles(temporary.path(), sentinel);
	TestCompression();
	std::cout << "Cases: " << TotalCases << ", checks: " << TotalChecks
		<< ", failures: " << FailedChecks << std::endl;
	return FailedChecks ? 1 : 0;
}
