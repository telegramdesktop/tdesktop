/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/flat_map.h"
#include "base/optional.h"
#include <crl/crl_time.h>
#include <QtCore/QString>
#include <QtCore/QByteArray>

namespace Storage {
namespace Cache {

struct Key {
	uint64 high = 0;
	uint64 low = 0;
};

inline bool operator==(const Key &a, const Key &b) {
	return (a.high == b.high) && (a.low == b.low);
}

inline bool operator!=(const Key &a, const Key &b) {
	return !(a == b);
}

inline bool operator<(const Key &a, const Key &b) {
	return std::tie(a.high, a.low) < std::tie(b.high, b.low);
}

struct Error {
	enum class Type {
		None,
		IO,
		WrongKey,
		LockFailed,
	};
	Type type = Type::None;
	QString path;

	static Error NoError();
};

inline Error Error::NoError() {
	return Error();
}

namespace details {

using RecordType = uint8;
using PlaceId = std::array<uint8, 7>;
using EntrySize = std::array<uint8, 3>;
using RecordsCount = std::array<uint8, 3>;

constexpr auto kRecordSizeUnknown = size_type(-1);
constexpr auto kRecordSizeInvalid = size_type(-2);
constexpr auto kBundledRecordsLimit
	= size_type(1 << (RecordsCount().size() * 8));
constexpr auto kDataSizeLimit = size_type(1 << (EntrySize().size() * 8));

struct Settings {
	size_type maxBundledRecords = 16 * 1024;
	size_type readBlockSize = 8 * 1024 * 1024;
	size_type maxDataSize = (kDataSizeLimit - 1);
	crl::time_type writeBundleDelay = 15 * 60 * crl::time_type(1000);
	size_type staleRemoveChunk = 256;

	int64 compactAfterExcess = 8 * 1024 * 1024;
	int64 compactAfterFullSize = 0;
	size_type compactChunkSize = 16 * 1024;

	bool trackEstimatedTime = true;
	int64 totalSizeLimit = 1024 * 1024 * 1024;
	size_type totalTimeLimit = 31 * 24 * 60 * 60; // One month in seconds.
	crl::time_type pruneTimeout = 5 * crl::time_type(1000);
	crl::time_type maxPruneCheckTimeout = 3600 * crl::time_type(1000);

	bool clearOnWrongKey = false;
};

struct SettingsUpdate {
	int64 totalSizeLimit = Settings().totalSizeLimit;
	size_type totalTimeLimit = Settings().totalTimeLimit;
};

struct TaggedValue {
	TaggedValue() = default;
	TaggedValue(QByteArray &&bytes, uint8 tag);

	QByteArray bytes;
	uint8 tag = 0;
};

struct TaggedSummary {
	size_type count = 0;
	size_type totalSize = 0;
};
struct Stats {
	TaggedSummary full;
	base::flat_map<uint8, TaggedSummary> tagged;
	bool clearing = false;
};

using Version = int32;

QString ComputeBasePath(const QString &original);
QString VersionFilePath(const QString &base);
std::optional<Version> ReadVersionValue(const QString &base);
bool WriteVersionValue(const QString &base, Version value);

template <typename Record>
constexpr auto GoodForEncryption = ((sizeof(Record) & 0x0F) == 0);

enum class Format : uint32 {
	Format_0,
};

struct BasicHeader {
	BasicHeader();

	static constexpr auto kTrackEstimatedTime = 0x01U;

	Format getFormat() const {
		return static_cast<Format>(format);
	}
	void setFormat(Format format) {
		this->format = static_cast<uint32>(format);
	}

	uint32 format : 8;
	uint32 flags : 24;
	uint32 systemTime = 0;
	uint32 reserved1 = 0;
	uint32 reserved2 = 0;
};

struct EstimatedTimePoint {
	uint32 relative1 = 0;
	uint32 relative2 = 0;
	uint32 system = 0;

	void setRelative(uint64 value) {
		relative1 = uint32(value & 0xFFFFFFFFU);
		relative2 = uint32((value >> 32) & 0xFFFFFFFFU);
	}
	uint64 getRelative() const {
		return uint64(relative1) | (uint64(relative2) << 32);
	}
};

struct Store {
	static constexpr auto kType = RecordType(0x01);

	void setSize(size_type size);
	size_type getSize() const;

	RecordType type = kType;
	uint8 tag = 0;
	EntrySize size = { { 0 } };
	PlaceId place = { { 0 } };
	uint32 checksum = 0;
	Key key;
};

struct StoreWithTime : Store {
	EstimatedTimePoint time;
	uint32 reserved = 0;
};

struct MultiStore {
	static constexpr auto kType = RecordType(0x02);

	explicit MultiStore(size_type count = 0);

	RecordType type = kType;
	RecordsCount count = { { 0 } };
	uint32 reserved1 = 0;
	uint32 reserved2 = 0;
	uint32 reserved3 = 0;

	using Part = Store;
	size_type validateCount() const;
};
struct MultiStoreWithTime : MultiStore {
	using MultiStore::MultiStore;

	using Part = StoreWithTime;
};

struct MultiRemove {
	static constexpr auto kType = RecordType(0x03);

	explicit MultiRemove(size_type count = 0);

	RecordType type = kType;
	RecordsCount count = { { 0 } };
	uint32 reserved1 = 0;
	uint32 reserved2 = 0;
	uint32 reserved3 = 0;

	using Part = Key;
	size_type validateCount() const;
};

struct MultiAccess {
	static constexpr auto kType = RecordType(0x04);

	explicit MultiAccess(
		EstimatedTimePoint time,
		size_type count = 0);

	RecordType type = kType;
	RecordsCount count = { { 0 } };
	EstimatedTimePoint time;

	using Part = Key;
	size_type validateCount() const;
};

} // namespace details
} // namespace Cache
} // namespace Storage

namespace std {

template <>
struct hash<Storage::Cache::Key> {
	size_t operator()(const Storage::Cache::Key &key) const {
		return (hash<uint64>()(key.high) ^ hash<uint64>()(key.low));
	}
};

} // namespace std
