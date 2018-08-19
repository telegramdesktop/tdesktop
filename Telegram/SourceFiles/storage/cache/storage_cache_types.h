/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/optional.h"

namespace Storage {
namespace Cache {
namespace details {

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

using Version = int32;

QString ComputeBasePath(const QString &original);
QString VersionFilePath(const QString &base);
base::optional<Version> ReadVersionValue(const QString &base);
bool WriteVersionValue(const QString &base, Version value);

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

using RecordType = uint8;
using PlaceId = std::array<uint8, 7>;
using EntrySize = std::array<uint8, 3>;
using RecordsCount = std::array<uint8, 3>;

template <typename Packed>
Packed ReadTo(size_type count) {
	Expects(count >= 0 && count < (1 << (Packed().size() * 8)));

	auto result = Packed();
	for (auto &element : result) {
		element = uint8(count & 0xFF);
		count >>= 8;
	}
	return result;
}

template <typename Packed>
size_type ReadFrom(Packed count) {
	auto result = size_type();
	for (auto &element : (count | ranges::view::reverse)) {
		result <<= 8;
		result |= size_type(element);
	}
	return result;
}

constexpr auto kRecordSizeUnknown = size_type(-1);
constexpr auto kRecordSizeInvalid = size_type(-2);
constexpr auto kBundledRecordsLimit = (1 << (RecordsCount().size() * 8));
constexpr auto kDataSizeLimit = (1 << (EntrySize().size() * 8));

template <typename Record>
constexpr auto GoodForEncryption = ((sizeof(Record) & 0x0F) == 0);

enum class Format : uint32 {
	Format_0,
};

struct BasicHeader {
	BasicHeader();

	static constexpr auto kTrackEstimatedTime = 0x01U;

	Format format : 8;
	uint32 flags : 24;
	uint32 systemTime = 0;
	uint32 reserved1 = 0;
	uint32 reserved2 = 0;
};
static_assert(GoodForEncryption<BasicHeader>);

struct EstimatedTimePoint {
	uint32 system = 0;
	uint32 relativeAdvancement = 0;
};

struct Store {
	static constexpr auto kType = RecordType(0x01);

	RecordType type = kType;
	uint8 tag = 0;
	EntrySize size = { { 0 } };
	PlaceId place = { { 0 } };
	uint32 checksum = 0;
	Key key;
};
static_assert(GoodForEncryption<Store>);

struct StoreWithTime : Store {
	EstimatedTimePoint time;
	uint32 reserved1 = 0;
	uint32 reserved2 = 0;
};
static_assert(GoodForEncryption<StoreWithTime>);

struct MultiStoreHeader {
	static constexpr auto kType = RecordType(0x02);

	explicit MultiStoreHeader(size_type count = 0);

	RecordType type = kType;
	RecordsCount count = { { 0 } };
	uint32 reserved1 = 0;
	uint32 reserved2 = 0;
	uint32 reserved3 = 0;
};
using MultiStorePart = Store;
using MultiStoreWithTimePart = StoreWithTime;
static_assert(GoodForEncryption<MultiStoreHeader>);

struct MultiRemoveHeader {
	static constexpr auto kType = RecordType(0x03);

	explicit MultiRemoveHeader(size_type count = 0);

	RecordType type = kType;
	RecordsCount count = { { 0 } };
	uint32 reserved1 = 0;
	uint32 reserved2 = 0;
	uint32 reserved3 = 0;
};
struct MultiRemovePart {
	Key key;
};
static_assert(GoodForEncryption<MultiRemoveHeader>);
static_assert(GoodForEncryption<MultiRemovePart>);

struct MultiAccessHeader {
	static constexpr auto kType = RecordType(0x04);

	explicit MultiAccessHeader(
		EstimatedTimePoint time,
		size_type count = 0);

	RecordType type = kType;
	RecordsCount count = { { 0 } };
	EstimatedTimePoint time;
	uint32 reserved = 0;
};
struct MultiAccessPart {
	Key key;
};
static_assert(GoodForEncryption<MultiAccessHeader>);
static_assert(GoodForEncryption<MultiAccessPart>);

} // namespace details
} // namespace Cache
} // namespace Storage

namespace std {

template <>
struct hash<Storage::Cache::details::Key> {
	size_t operator()(const Storage::Cache::details::Key &key) const {
		return (hash<uint64>()(key.high) ^ hash<uint64>()(key.low));
	}
};

} // namespace std
