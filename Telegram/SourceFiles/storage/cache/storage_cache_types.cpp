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
namespace {

template <typename Packed>
inline Packed ReadTo(size_type count) {
	Expects(count >= 0 && count < (1 << (Packed().size() * 8)));

	auto result = Packed();
	for (auto &element : result) {
		element = uint8(count & 0xFF);
		count >>= 8;
	}
	return result;
}

template <typename Packed>
inline size_type ReadFrom(const Packed &count) {
	auto result = size_type();
	for (auto &element : (count | ranges::view::reverse)) {
		result <<= 8;
		result |= size_type(element);
	}
	return result;
}

template <typename Packed>
inline size_type ValidateStrictCount(const Packed &count) {
	const auto result = ReadFrom(count);
	return (result != 0) ? result : -1;
}

} // namespace

TaggedValue::TaggedValue(QByteArray &&bytes, uint8 tag)
: bytes(std::move(bytes)), tag(tag) {
}

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
: format(static_cast<uint32>(Format::Format_0))
, flags(0) {
}

void Store::setSize(size_type size) {
	this->size = ReadTo<EntrySize>(size);
}

size_type Store::getSize() const {
	return ReadFrom(size);
}

MultiStore::MultiStore(size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count)) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

size_type MultiStore::validateCount() const {
	return ValidateStrictCount(count);
}

MultiRemove::MultiRemove(size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count)) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

size_type MultiRemove::validateCount() const {
	return ValidateStrictCount(count);
}

MultiAccess::MultiAccess(
	EstimatedTimePoint time,
	size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count))
, time(time) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

size_type MultiAccess::validateCount() const {
	return ReadFrom(count);
}

} // namespace details
} // namespace Cache
} // namespace Storage
