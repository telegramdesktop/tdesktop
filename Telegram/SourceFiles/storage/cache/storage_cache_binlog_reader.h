/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/cache/storage_cache_types.h"
#include "storage/storage_encrypted_file.h"
#include "base/bytes.h"
#include "base/match_method.h"

namespace Storage {
namespace Cache {
namespace details {

template <typename ...Records>
class BinlogReader;

class BinlogWrapper {
public:
	BinlogWrapper(File &binlog, const Settings &settings, int64 till = 0);

	bool finished() const;
	bool failed() const;

	static std::optional<BasicHeader> ReadHeader(
		File &binlog,
		const Settings &settings);

private:
	template <typename ...Records>
	friend class BinlogReader;

	bool readPart();
	void finish(size_type rollback = 0);

	using ReadRecordSize = size_type (*)(
		const BinlogWrapper &that,
		bytes::const_span data);
	bytes::const_span readRecord(ReadRecordSize readRecordSize);

	File &_binlog;
	Settings _settings;

	int64 _till = 0;
	bytes::vector _data;
	bytes::span _full;
	bytes::span _part;
	bool _finished = false;
	bool _failed = false;

};

template <typename ...Records>
class BinlogReader {
public:
	explicit BinlogReader(BinlogWrapper &wrapper);

	template <typename ...Handlers>
	bool readTillEnd(Handlers &&...handlers);

private:
	static size_type ReadRecordSize(
		const BinlogWrapper &that,
		bytes::const_span data);

	template <typename ...Handlers>
	bool handleRecord(bytes::const_span data, Handlers &&...handlers) const;

	BinlogWrapper &_wrapper;

};

template <typename Record>
struct MultiRecord {
	using true_t = char;
	using false_t = true_t(&)[2];
	static_assert(sizeof(true_t) != sizeof(false_t));

	static false_t Check(...);
	template <typename Test, typename = typename Test::Part>
	static true_t Check(const Test&);

	static constexpr bool Is = (sizeof(Check(std::declval<Record>()))
		== sizeof(true_t));
};

template <typename ...Records>
struct BinlogReaderRecursive {
	static void CheckSettings(const Settings &settings) {
	}

	static size_type ReadRecordSize(
			RecordType type,
			bytes::const_span data,
			size_type partsLimit) {
		return kRecordSizeInvalid;
	}

	template <typename ...Handlers>
	static bool HandleRecord(
			RecordType type,
			bytes::const_span data,
			Handlers &&...handlers) {
		Unexpected("Bad type in BinlogReaderRecursive::HandleRecord.");
	}
};

template <typename Record, typename ...Other>
struct BinlogReaderRecursive<Record, Other...> {
	static void CheckSettings(const Settings &settings);

	static size_type ReadRecordSize(
		RecordType type,
		bytes::const_span data,
		size_type partsLimit);

	template <typename ...Handlers>
	static bool HandleRecord(
		RecordType type,
		bytes::const_span data,
		Handlers &&...handlers);
};

template <typename Record, typename ...Other>
inline void BinlogReaderRecursive<Record, Other...>::CheckSettings(
		const Settings &settings) {
	static_assert(GoodForEncryption<Record>);
	if constexpr (MultiRecord<Record>::Is) {
		using Head = Record;
		using Part = typename Record::Part;
		static_assert(GoodForEncryption<Part>);
		Assert(settings.readBlockSize
			>= (sizeof(Head)
				+ settings.maxBundledRecords * sizeof(Part)));
	} else {
		Assert(settings.readBlockSize >= sizeof(Record));
	}
}

template <typename Record, typename ...Other>
inline size_type BinlogReaderRecursive<Record, Other...>::ReadRecordSize(
		RecordType type,
		bytes::const_span data,
		size_type partsLimit) {
	if (type != Record::kType) {
		return BinlogReaderRecursive<Other...>::ReadRecordSize(
			type,
			data,
			partsLimit);
	}
	if constexpr (MultiRecord<Record>::Is) {
		using Head = Record;
		using Part = typename Record::Part;

		if (data.size() < sizeof(Head)) {
			return kRecordSizeUnknown;
		}
		const auto head = reinterpret_cast<const Head*>(data.data());
		const auto count = head->validateCount();
		return (count >= 0 && count <= partsLimit)
				? (sizeof(Head) + count * sizeof(Part))
				: kRecordSizeInvalid;
	} else {
		return sizeof(Record);
	}
}

template <typename Record, typename ...Other>
template <typename ...Handlers>
inline bool BinlogReaderRecursive<Record, Other...>::HandleRecord(
		RecordType type,
		bytes::const_span data,
		Handlers &&...handlers) {
	if (type != Record::kType) {
		return BinlogReaderRecursive<Other...>::HandleRecord(
			type,
			data,
			std::forward<Handlers>(handlers)...);
	}
	if constexpr (MultiRecord<Record>::Is) {
		using Head = Record;
		using Part = typename Record::Part;

		Assert(data.size() >= sizeof(Head));
		const auto bytes = data.data();
		const auto head = reinterpret_cast<const Head*>(bytes);
		const auto count = head->validateCount();
		Assert(data.size() == sizeof(Head) + count * sizeof(Part));
		const auto parts = gsl::make_span(
			reinterpret_cast<const Part*>(bytes + sizeof(Head)),
			count);
		auto from = std::begin(parts);
		const auto till = std::end(parts);
		const auto element = [&] {
			return (from == till) ? nullptr : &*from++;
		};
		return base::match_method2(
			*head,
			element,
			std::forward<Handlers>(handlers)...);
	} else {
		Assert(data.size() == sizeof(Record));
		return base::match_method(
			*reinterpret_cast<const Record*>(data.data()),
			std::forward<Handlers>(handlers)...);
	}
}

template <typename ...Records>
BinlogReader<Records...>::BinlogReader(BinlogWrapper &wrapper)
: _wrapper(wrapper) {
	BinlogReaderRecursive<Records...>::CheckSettings(_wrapper._settings);
}

template <typename ...Records>
template <typename ...Handlers>
bool BinlogReader<Records...>::readTillEnd(Handlers &&...handlers) {
	if (!_wrapper.readPart()) {
		return true;
	}
	const auto readRecord = [&] {
		return _wrapper.readRecord(&BinlogReader::ReadRecordSize);
	};
	for (auto bytes = readRecord(); !bytes.empty(); bytes = readRecord()) {
		if (!handleRecord(bytes, std::forward<Handlers>(handlers)...)) {
			_wrapper.finish(bytes.size());
			return true;
		}
	}
	return false;
}

template <typename ...Records>
size_type BinlogReader<Records...>::ReadRecordSize(
		const BinlogWrapper &that,
		bytes::const_span data) {
	if (data.empty()) {
		return kRecordSizeUnknown;
	}
	return BinlogReaderRecursive<Records...>::ReadRecordSize(
		static_cast<RecordType>(data[0]),
		data,
		that._settings.maxBundledRecords);
}

template <typename ...Records>
template <typename ...Handlers>
bool BinlogReader<Records...>::handleRecord(
		bytes::const_span data,
		Handlers &&...handlers) const {
	Expects(!data.empty());

	return BinlogReaderRecursive<Records...>::HandleRecord(
		static_cast<RecordType>(data[0]),
		data,
		std::forward<Handlers>(handlers)...);
}

} // namespace details
} // namespace Cache
} // namespace Storage
