/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_types.h"

#include <QtCore/QDir>

namespace Storage {
namespace Cache {
namespace details {

QString ComputeBasePath(const QString &original) {
	const auto result = QDir(original).absolutePath();
	return result.endsWith('/') ? result : (result + '/');
}

QString VersionFilePath(const QString &base) {
	Expects(base.endsWith('/'));

	return base + QStringLiteral("version");
}

base::optional<Version> ReadVersionValue(const QString &base) {
	QFile file(VersionFilePath(base));
	if (!file.open(QIODevice::ReadOnly)) {
		return base::none;
	}
	const auto bytes = file.read(sizeof(Version));
	if (bytes.size() != sizeof(Version)) {
		return base::none;
	}
	return *reinterpret_cast<const Version*>(bytes.data());
}

bool WriteVersionValue(const QString &base, Version value) {
	if (!QDir().mkpath(base)) {
		return false;
	}
	const auto bytes = QByteArray::fromRawData(
		reinterpret_cast<const char*>(&value),
		sizeof(value));
	QFile file(VersionFilePath(base));
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	} else if (file.write(bytes) != bytes.size()) {
		return false;
	}
	return file.flush();
}

BasicHeader::BasicHeader()
: format(Format::Format_0)
, flags(0) {
}

MultiStoreHeader::MultiStoreHeader(size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count)) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

MultiRemoveHeader::MultiRemoveHeader(size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count)) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

MultiAccessHeader::MultiAccessHeader(
	EstimatedTimePoint time,
	size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count))
, time(time) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

} // namespace details
} // namespace Cache
} // namespace Storage
