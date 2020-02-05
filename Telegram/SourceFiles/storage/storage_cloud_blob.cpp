/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_cloud_blob.h"

#include "base/zlib_help.h"

namespace Storage {

namespace {

QByteArray ReadFinalFile(const QString &path) {
	constexpr auto kMaxZipSize = 10 * 1024 * 1024;
	auto file = QFile(path);
	if (file.size() > kMaxZipSize || !file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

bool ExtractZipFile(zlib::FileToRead &zip, const QString path) {
	constexpr auto kMaxSize = 25 * 1024 * 1024;
	const auto content = zip.readCurrentFileContent(kMaxSize);
	if (content.isEmpty() || zip.error() != UNZ_OK) {
		return false;
	}
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(content) == content.size());
}

} // namespace

bool UnpackBlob(
	const QString &path,
	const QString &folder,
	Fn<bool(const QString &)> checkNameCallback) {
	const auto bytes = ReadFinalFile(path);
	if (bytes.isEmpty()) {
		return false;
	}
	auto zip = zlib::FileToRead(bytes);
	if (zip.goToFirstFile() != UNZ_OK) {
		return false;
	}
	do {
		const auto name = zip.getCurrentFileName();
		const auto path = folder + '/' + name;
		if (checkNameCallback(name) && !ExtractZipFile(zip, path)) {
			return false;
		}

		const auto jump = zip.goToNextFile();
		if (jump == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (jump != UNZ_OK) {
			return false;
		}
	} while (true);
	return true;
}

} // namespace Storage
