/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_database.h"

#include "storage/cache/storage_cache_cleaner.h"
#include "storage/storage_encryption.h"
#include "storage/storage_encrypted_file.h"
#include "base/flat_set.h"
#include "base/flat_map.h"
#include "base/algorithm.h"
#include "base/concurrent_timer.h"
#include <crl/crl.h>
#include <xxhash.h>
#include <QtCore/QDir>
#include <unordered_map>
#include <set>

namespace std {

template <>
struct hash<Storage::Cache::Key> {
	size_t operator()(const Storage::Cache::Key &key) const {
		return (hash<uint64>()(key.high) ^ hash<uint64>()(key.low));
	}
};

} // namespace std

namespace Storage {
namespace Cache {
namespace details {
namespace {

using RecordType = uint8;
using PlaceId = std::array<uint8, 7>;
using EntrySize = std::array<uint8, 3>;
using RecordsCount = std::array<uint8, 3>;

constexpr auto kRecordSizeUnknown = size_type(-1);
constexpr auto kRecordSizeInvalid = size_type(-2);
constexpr auto kBundledRecordsLimit = (1 << (RecordsCount().size() * 8));
constexpr auto kDataSizeLimit = (1 << (EntrySize().size() * 8));

template <typename Record>
constexpr auto GoodForEncryption = ((sizeof(Record) & 0x0F) == 0);

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
	RecordsCount a;
	for (auto &element : (count | ranges::view::reverse)) {
		result <<= 8;
		result |= size_type(element);
	}
	return result;
}

uint32 CountChecksum(bytes::const_span data) {
	const auto seed = uint32(0);
	return XXH32(data.data(), data.size(), seed);
}

QString PlaceFromId(PlaceId place) {
	auto result = QString();
	result.reserve(15);
	const auto pushDigit = [&](uint8 digit) {
		const auto hex = (digit < 0x0A)
			? char('0' + digit)
			: char('A' + (digit - 0x0A));
		result.push_back(hex);
	};
	const auto push = [&](uint8 value) {
		pushDigit(value & 0x0F);
		pushDigit(value >> 4);
	};
	for (auto i = 0; i != place.size(); ++i) {
		push(place[i]);
		if (!i) {
			result.push_back('/');
		}
	}
	return result;
}

int32 GetUnixtime() {
	return std::max(int32(time(nullptr)), 1);
}

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

BasicHeader::BasicHeader()
: format(Format::Format_0)
, flags(0) {
}

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

MultiStoreHeader::MultiStoreHeader(size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count)) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

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

MultiRemoveHeader::MultiRemoveHeader(size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count)) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

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

MultiAccessHeader::MultiAccessHeader(
	EstimatedTimePoint time,
	size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count))
, time(time) {
	Expects(count >= 0 && count < kBundledRecordsLimit);
}

} // namespace

class Database {
public:
	using Wrapper = Cache::Database;
	using Settings = Wrapper::Settings;
	Database(
		crl::weak_on_queue<Database> weak,
		const QString &path,
		const Settings &settings);

	void open(EncryptionKey key, FnMut<void(Error)> done);
	void close(FnMut<void()> done);

	void put(const Key &key, QByteArray value, FnMut<void(Error)> done);
	void get(const Key &key, FnMut<void(QByteArray)> done);
	void remove(const Key &key, FnMut<void()> done = nullptr);

	void clear(FnMut<void(Error)> done);

	~Database();

private:
	struct Entry {
		Entry() = default;
		Entry(
			PlaceId place,
			uint8 tag,
			uint32 checksum,
			size_type size,
			int64 useTime);

		int64 useTime = 0;
		size_type size = 0;
		uint32 checksum = 0;
		PlaceId place = { { 0 } };
		uint8 tag = 0;
	};
	struct CleanerWrap {
		std::unique_ptr<Cleaner> object;
		base::binary_guard guard;
	};
	using Map = std::unordered_map<Key, Entry>;

	template <typename Callback, typename ...Args>
	void invokeCallback(Callback &&callback, Args &&...args);

	Error ioError(const QString &path) const;

	QString computePath(Version version) const;
	QString binlogPath(Version version) const;
	QString binlogPath() const;
	QString binlogFilename() const;
	File::Result openBinlog(
		Version version,
		File::Mode mode,
		EncryptionKey &key);
	bool readHeader();
	bool writeHeader();
	void readBinlog();
	size_type readBinlogRecords(bytes::const_span data);
	size_type readBinlogRecordSize(bytes::const_span data) const;
	bool readBinlogRecord(bytes::const_span data);
	template <typename RecordStore>
	bool readRecordStoreGeneric(bytes::const_span data);
	bool readRecordStore(bytes::const_span data);
	template <typename StorePart>
	bool readRecordMultiStoreGeneric(bytes::const_span data);
	bool readRecordMultiStore(bytes::const_span data);
	bool readRecordMultiRemove(bytes::const_span data);
	bool readRecordMultiAccess(bytes::const_span data);
	template <typename RecordStore, typename Postprocess>
	bool processRecordStoreGeneric(
		const RecordStore *record,
		Postprocess &&postprocess);
	bool processRecordStore(const Store *record, std::is_class<Store>);
	bool processRecordStore(
		const StoreWithTime *record,
		std::is_class<StoreWithTime>);

	void adjustRelativeTime();
	void startDelayedPruning();
	int64 countRelativeTime() const;
	EstimatedTimePoint countTimePoint() const;
	void applyTimePoint(EstimatedTimePoint time);
	int64 pruneBeforeTime() const;
	void prune();
	void collectTimePrune(
		base::flat_set<Key> &stale,
		int64 &staleTotalSize);
	void collectSizePrune(
		base::flat_set<Key> &stale,
		int64 &staleTotalSize);

	void setMapEntry(const Key &key, Entry &&entry);
	void eraseMapEntry(const Map::const_iterator &i);

	Version findAvailableVersion() const;
	QString versionPath() const;
	bool writeVersion(Version version);
	Version readVersion() const;

	QString placePath(PlaceId place) const;
	bool isFreePlace(PlaceId place) const;
	template <typename StoreRecord>
	QString writeKeyPlaceGeneric(
		StoreRecord &&record,
		const Key &key,
		size_type size,
		uint32 checksum);
	QString writeKeyPlace(const Key &key, size_type size, uint32 checksum);
	void writeMultiRemoveLazy();
	void writeMultiRemove();
	void writeMultiAccessLazy();
	void writeMultiAccess();
	void writeMultiAccessBlock();
	void writeBundlesLazy();
	void writeBundles();

	void createCleaner();
	void cleanerDone(Error error);

	crl::weak_on_queue<Database> _weak;
	QString _base, _path;
	const Settings _settings;
	EncryptionKey _key;
	File _binlog;
	Map _map;
	std::set<Key> _removing;
	std::set<Key> _accessed;

	int64 _relativeTime = 0;
	int64 _timeCorrection = 0;
	uint32 _latestSystemTime = 0;

	int64 _totalSize = 0;
	int64 _minimalEntryTime = 0;
	size_type _entriesWithMinimalTimeCount = 0;

	base::ConcurrentTimer _writeBundlesTimer;
	base::ConcurrentTimer _pruneTimer;

	CleanerWrap _cleaner;

};

Database::Entry::Entry(
	PlaceId place,
	uint8 tag,
	uint32 checksum,
	size_type size,
	int64 useTime)
: useTime(useTime)
, size(size)
, checksum(checksum)
, tag(tag)
, place(place) {
}

Database::Database(
	crl::weak_on_queue<Database> weak,
	const QString &path,
	const Settings &settings)
: _weak(std::move(weak))
, _base(ComputeBasePath(path))
, _settings(settings)
, _writeBundlesTimer(_weak, [=] { writeBundles(); })
, _pruneTimer(_weak, [=] { prune(); }) {
	Expects(_settings.maxDataSize < kDataSizeLimit);
	Expects(_settings.maxBundledRecords < kBundledRecordsLimit);
	Expects(!_settings.totalTimeLimit
		|| _settings.totalTimeLimit > 0);
	Expects(!_settings.totalSizeLimit
		|| _settings.totalSizeLimit > _settings.maxDataSize);
}

template <typename Callback, typename ...Args>
void Database::invokeCallback(Callback &&callback, Args &&...args) {
	if (callback) {
		callback(std::move(args)...);
	}
}

Error Database::ioError(const QString &path) const {
	return { Error::Type::IO, path };
}

void Database::open(EncryptionKey key, FnMut<void(Error)> done) {
	const auto version = readVersion();
	const auto result = openBinlog(version, File::Mode::ReadAppend, key);
	switch (result) {
	case File::Result::Success:
		invokeCallback(done, Error::NoError());
		break;
	case File::Result::LockFailed:
		invokeCallback(
			done,
			Error{ Error::Type::LockFailed, binlogPath(version) });
		break;
	case File::Result::WrongKey:
		invokeCallback(
			done,
			Error{ Error::Type::WrongKey, binlogPath(version) });
		break;
	case File::Result::Failed: {
		const auto available = findAvailableVersion();
		if (writeVersion(available)) {
			const auto open = openBinlog(available, File::Mode::Write, key);
			if (open == File::Result::Success) {
				invokeCallback(done, Error::NoError());
			} else {
				invokeCallback(done, ioError(binlogPath(available)));
			}
		} else {
			invokeCallback(done, ioError(versionPath()));
		}
	} break;
	default: Unexpected("Result from Database::openBinlog.");
	}
}

QString Database::computePath(Version version) const {
	return _base + QString::number(version) + '/';
}

QString Database::binlogFilename() const {
	return QStringLiteral("binlog");
}

QString Database::binlogPath(Version version) const {
	return computePath(version) + binlogFilename();
}

QString Database::binlogPath() const {
	return _path + binlogFilename();
}

File::Result Database::openBinlog(
		Version version,
		File::Mode mode,
		EncryptionKey &key) {
	const auto path = binlogPath(version);
	const auto result = _binlog.open(path, mode, key);
	if (result == File::Result::Success) {
		const auto headerRequired = (mode == File::Mode::Read)
			|| (mode == File::Mode::ReadAppend && _binlog.size() > 0);
		if (headerRequired ? readHeader() : writeHeader()) {
			_path = computePath(version);
			_key = std::move(key);
			createCleaner();
			readBinlog();
		} else {
			return File::Result::Failed;
		}
	}
	return result;
}

bool Database::readHeader() {
	auto header = BasicHeader();
	if (_binlog.read(bytes::object_as_span(&header)) != sizeof(header)) {
		return false;
	} else if (header.format != Format::Format_0) {
		return false;
	} else if (_settings.trackEstimatedTime
		!= !!(header.flags & header.kTrackEstimatedTime)) {
		return false;
	}
	_relativeTime = _latestSystemTime = header.systemTime;
	return true;
}

bool Database::writeHeader() {
	auto header = BasicHeader();
	const auto now = _settings.trackEstimatedTime ? GetUnixtime() : 0;
	_relativeTime = _latestSystemTime = header.systemTime = now;
	if (_settings.trackEstimatedTime) {
		header.flags |= header.kTrackEstimatedTime;
	}
	return _binlog.write(bytes::object_as_span(&header));
}

void Database::readBinlog() {
	auto data = bytes::vector(_settings.readBlockSize);
	const auto full = bytes::make_span(data);
	auto notParsedBytes = index_type(0);
	while (true) {
		Assert(notParsedBytes < full.size());
		const auto readBytes = _binlog.read(full.subspan(notParsedBytes));
		if (!readBytes) {
			break;
		}
		notParsedBytes += readBytes;
		const auto bytes = full.subspan(0, notParsedBytes);
		const auto parsedTill = readBinlogRecords(bytes);
		if (parsedTill == kRecordSizeInvalid) {
			break;
		}
		Assert(parsedTill >= 0 && parsedTill <= notParsedBytes);
		notParsedBytes -= parsedTill;
		if (parsedTill > 0 && parsedTill < bytes.size()) {
			bytes::move(full, bytes.subspan(parsedTill));
		}
	}
	_binlog.seek(_binlog.offset() - notParsedBytes);

	adjustRelativeTime();
	startDelayedPruning();
}

int64 Database::countRelativeTime() const {
	const auto now = GetUnixtime();
	const auto delta = std::max(int64(now) - int64(_latestSystemTime), 0LL);
	return _relativeTime + delta;
}

int64 Database::pruneBeforeTime() const {
	return _settings.totalTimeLimit
		? (countRelativeTime() - _settings.totalTimeLimit)
		: 0LL;
}

void Database::startDelayedPruning() {
	if (!_settings.trackEstimatedTime || _map.empty()) {
		return;
	}
	const auto pruning = [&] {
		if (_settings.totalSizeLimit > 0
			&& _totalSize > _settings.totalSizeLimit) {
			return true;
		} else if (_minimalEntryTime != 0
			&& _minimalEntryTime <= pruneBeforeTime()) {
			return true;
		}
		return false;
	}();
	if (pruning) {
		if (!_pruneTimer.isActive()
			|| _pruneTimer.remainingTime() > _settings.pruneTimeout) {
			_pruneTimer.callOnce(_settings.pruneTimeout);
		}
	} else if (_minimalEntryTime != 0) {
		const auto before = pruneBeforeTime();
		const auto seconds = (_minimalEntryTime - before);
		if (!_pruneTimer.isActive()) {
			_pruneTimer.callOnce(std::min(
				seconds * crl::time_type(1000),
				_settings.maxPruneCheckTimeout));
		}
	}
}

void Database::prune() {
	auto stale = base::flat_set<Key>();
	auto staleTotalSize = int64();
	collectTimePrune(stale, staleTotalSize);
	collectSizePrune(stale, staleTotalSize);
	for (const auto &key : stale) {
		remove(key);
	}
	startDelayedPruning();
}

void Database::collectTimePrune(
		base::flat_set<Key> &stale,
		int64 &staleTotalSize) {
	if (!_settings.totalTimeLimit) {
		return;
	}
	const auto before = pruneBeforeTime();
	if (!_minimalEntryTime || _minimalEntryTime > before) {
		return;
	}
	_minimalEntryTime = 0;
	_entriesWithMinimalTimeCount = 0;
	for (const auto &[key, entry] : _map) {
		if (entry.useTime <= before) {
			stale.emplace(key);
			staleTotalSize += entry.size;
		} else if (!_minimalEntryTime
			|| _minimalEntryTime > entry.useTime) {
			_minimalEntryTime = entry.useTime;
			_entriesWithMinimalTimeCount = 1;
		} else if (_minimalEntryTime == entry.useTime) {
			++_entriesWithMinimalTimeCount;
		}
	}
}

void Database::collectSizePrune(
		base::flat_set<Key> &stale,
		int64 &staleTotalSize) {
	const auto removeSize = (_settings.totalSizeLimit > 0)
		? (_totalSize - staleTotalSize - _settings.totalSizeLimit)
		: 0;
	if (removeSize <= 0) {
		return;
	}

	using Bucket = std::pair<const Key, Entry>;
	auto oldest = base::flat_multi_map<
		int64,
		const Bucket*,
		std::greater<>>();
	auto oldestTotalSize = int64();

	const auto canRemoveFirst = [&](const Entry &adding) {
		const auto totalSizeAfterAdd = oldestTotalSize + adding.size;
		const auto &first = oldest.begin()->second->second;
		return (adding.useTime <= first.useTime
			&& (totalSizeAfterAdd - removeSize >= first.size));
	};

	for (const auto &bucket : _map) {
		const auto &entry = bucket.second;
		if (stale.contains(bucket.first)) {
			continue;
		}
		const auto add = (oldestTotalSize < removeSize)
			? true
			: (entry.useTime < oldest.begin()->second->second.useTime);
		if (!add) {
			continue;
		}
		while (!oldest.empty() && canRemoveFirst(entry)) {
			oldestTotalSize -= oldest.begin()->second->second.size;
			oldest.erase(oldest.begin());
		}
		oldestTotalSize += entry.size;
		oldest.emplace(entry.useTime, &bucket);
	}

	for (const auto &pair : oldest) {
		stale.emplace(pair.second->first);
	}
	staleTotalSize += oldestTotalSize;
}

void Database::adjustRelativeTime() {
	if (!_settings.trackEstimatedTime) {
		return;
	}
	const auto now = GetUnixtime();
	if (now < _latestSystemTime) {
		writeMultiAccessBlock();
	}
}

size_type Database::readBinlogRecords(bytes::const_span data) {
	auto result = 0;
	while (true) {
		const auto size = readBinlogRecordSize(data);
		if (size == kRecordSizeUnknown || size > data.size()) {
			return result;
		} else if (size == kRecordSizeInvalid || !readBinlogRecord(data)) {
			return (result > 0) ? result : kRecordSizeInvalid;
		} else {
			result += size;
			data = data.subspan(size);
		}
	}
}

size_type Database::readBinlogRecordSize(bytes::const_span data) const {
	if (data.empty()) {
		return kRecordSizeUnknown;
	}

	switch (static_cast<RecordType>(data[0])) {
	case Store::kType:
		return _settings.trackEstimatedTime
			? sizeof(StoreWithTime)
			: sizeof(Store);

	case MultiStoreHeader::kType:
		if (data.size() >= sizeof(MultiStoreHeader)) {
			const auto header = reinterpret_cast<const MultiStoreHeader*>(
				data.data());
			const auto count = ReadFrom(header->count);
			const auto size = _settings.trackEstimatedTime
				? sizeof(MultiStoreWithTimePart)
				: sizeof(MultiStorePart);
			return (count > 0 && count < _settings.maxBundledRecords)
				? (sizeof(MultiStoreHeader) + count * size)
				: kRecordSizeInvalid;
		}
		return kRecordSizeUnknown;

	case MultiRemoveHeader::kType:
		if (data.size() >= sizeof(MultiRemoveHeader)) {
			const auto header = reinterpret_cast<const MultiRemoveHeader*>(
				data.data());
			const auto count = ReadFrom(header->count);
			return (count > 0 && count < _settings.maxBundledRecords)
				? (sizeof(MultiRemoveHeader)
					+ count * sizeof(MultiRemovePart))
				: kRecordSizeInvalid;
		}
		return kRecordSizeUnknown;

	case MultiAccessHeader::kType:
		if (!_settings.trackEstimatedTime) {
			return kRecordSizeInvalid;
		} else if (data.size() >= sizeof(MultiAccessHeader)) {
			const auto header = reinterpret_cast<const MultiAccessHeader*>(
				data.data());
			const auto count = ReadFrom(header->count);
			return (count >= 0 && count < _settings.maxBundledRecords)
				? (sizeof(MultiAccessHeader)
					+ count * sizeof(MultiAccessPart))
				: kRecordSizeInvalid;
		}
		return kRecordSizeUnknown;

	}
	return kRecordSizeInvalid;
}

bool Database::readBinlogRecord(bytes::const_span data) {
	Expects(!data.empty());

	switch (static_cast<RecordType>(data[0])) {
	case Store::kType:
		return readRecordStore(data);

	case MultiStoreHeader::kType:
		return readRecordMultiStore(data);

	case MultiRemoveHeader::kType:
		return readRecordMultiRemove(data);

	case MultiAccessHeader::kType:
		return readRecordMultiAccess(data);

	}
	Unexpected("Bad type in Database::readBinlogRecord.");
}

template <typename RecordStore>
bool Database::readRecordStoreGeneric(bytes::const_span data) {
	Expects(data.size() >= sizeof(RecordStore));

	return processRecordStore(
		reinterpret_cast<const RecordStore*>(data.data()),
		std::is_class<RecordStore>{});
}

template <typename RecordStore, typename Postprocess>
bool Database::processRecordStoreGeneric(
		const RecordStore *record,
		Postprocess &&postprocess) {
	const auto size = ReadFrom(record->size);
	if (size <= 0 || size > _settings.maxDataSize) {
		return false;
	}
	auto entry = Entry(
		record->place,
		record->tag,
		record->checksum,
		size,
		_relativeTime);
	if (!postprocess(entry, record)) {
		return false;
	}
	setMapEntry(record->key, std::move(entry));
	return true;
}

bool Database::processRecordStore(
		const Store *record,
		std::is_class<Store>) {
	const auto postprocess = [](auto&&...) { return true; };
	return processRecordStoreGeneric(record, postprocess);
}

bool Database::processRecordStore(
		const StoreWithTime *record,
		std::is_class<StoreWithTime>) {
	const auto postprocess = [&](
			Entry &entry,
			not_null<const StoreWithTime*> record) {
		applyTimePoint(record->time);
		entry.useTime = _relativeTime;
		return true;
	};
	return processRecordStoreGeneric(record, postprocess);
}

bool Database::readRecordStore(bytes::const_span data) {
	if (!_settings.trackEstimatedTime) {
		return readRecordStoreGeneric<Store>(data);
	}
	return readRecordStoreGeneric<StoreWithTime>(data);
}

template <typename StorePart>
bool Database::readRecordMultiStoreGeneric(bytes::const_span data) {
	Expects(data.size() >= sizeof(MultiStoreHeader));

	const auto bytes = data.data();
	const auto record = reinterpret_cast<const MultiStoreHeader*>(bytes);
	const auto count = ReadFrom(record->count);
	Assert(data.size() >= sizeof(MultiStoreHeader)
		+ count * sizeof(StorePart));
	const auto parts = gsl::make_span(
		reinterpret_cast<const StorePart*>(
			bytes + sizeof(MultiStoreHeader)),
		count);
	for (const auto &part : parts) {
		if (!processRecordStore(&part, std::is_class<StorePart>{})) {
			return false;
		}
	}
	return true;
}

bool Database::readRecordMultiStore(bytes::const_span data) {
	if (!_settings.trackEstimatedTime) {
		return readRecordMultiStoreGeneric<MultiStorePart>(data);
	}
	return readRecordMultiStoreGeneric<MultiStoreWithTimePart>(data);
}

void Database::setMapEntry(const Key &key, Entry &&entry) {
	auto &already = _map[key];
	_totalSize += entry.size - already.size;
	if (entry.useTime != 0
		&& (entry.useTime < _minimalEntryTime || !_minimalEntryTime)) {
		_minimalEntryTime = entry.useTime;
		_entriesWithMinimalTimeCount = 1;
	} else if (_minimalEntryTime != 0 && already.useTime != entry.useTime) {
		if (entry.useTime == _minimalEntryTime) {
			Assert(_entriesWithMinimalTimeCount > 0);
			++_entriesWithMinimalTimeCount;
		} else if (already.useTime == _minimalEntryTime) {
			Assert(_entriesWithMinimalTimeCount > 0);
			--_entriesWithMinimalTimeCount;
		}
	}
	already = std::move(entry);
}

void Database::eraseMapEntry(const Map::const_iterator &i) {
	if (i != end(_map)) {
		const auto &entry = i->second;
		_totalSize -= entry.size;
		if (_minimalEntryTime != 0 && entry.useTime == _minimalEntryTime) {
			Assert(_entriesWithMinimalTimeCount > 0);
			--_entriesWithMinimalTimeCount;
		}
		_map.erase(i);
	}
}

bool Database::readRecordMultiRemove(bytes::const_span data) {
	Expects(data.size() >= sizeof(MultiRemoveHeader));

	const auto bytes = data.data();
	const auto record = reinterpret_cast<const MultiRemoveHeader*>(bytes);
	const auto count = ReadFrom(record->count);
	Assert(data.size() >= sizeof(MultiRemoveHeader)
		+ count * sizeof(MultiRemovePart));
	const auto parts = gsl::make_span(
		reinterpret_cast<const MultiRemovePart*>(
			bytes + sizeof(MultiRemoveHeader)),
		count);
	for (const auto &part : parts) {
		eraseMapEntry(_map.find(part.key));
	}
	return true;
}

EstimatedTimePoint Database::countTimePoint() const {
	const auto now = std::max(GetUnixtime(), 1);
	const auto delta = std::max(int64(now) - int64(_latestSystemTime), 0LL);
	auto result = EstimatedTimePoint();
	result.system = now;
	result.relativeAdvancement = std::min(
		delta,
		int64(_settings.maxTimeAdvancement));
	return result;
}

void Database::applyTimePoint(EstimatedTimePoint time) {
	_relativeTime += time.relativeAdvancement;
	_latestSystemTime = time.system;
}

bool Database::readRecordMultiAccess(bytes::const_span data) {
	Expects(data.size() >= sizeof(MultiAccessHeader));
	Expects(_settings.trackEstimatedTime);

	const auto bytes = data.data();
	const auto record = reinterpret_cast<const MultiAccessHeader*>(bytes);
	if (record->time.relativeAdvancement > _settings.maxTimeAdvancement) {
		return false;
	}
	applyTimePoint(record->time);
	const auto count = ReadFrom(record->count);
	Assert(data.size() >= sizeof(MultiAccessHeader)
		+ count * sizeof(MultiAccessPart));
	const auto parts = gsl::make_span(
		reinterpret_cast<const MultiAccessPart*>(
			bytes + sizeof(MultiAccessHeader)),
		count);
	for (const auto &part : parts) {
		if (const auto i = _map.find(part.key); i != end(_map)) {
			i->second.useTime = _relativeTime;
		}
	}
	return true;
}

void Database::close(FnMut<void()> done) {
	writeBundles();
	_cleaner = CleanerWrap();
	_binlog.close();
	invokeCallback(done);
}

void Database::put(
		const Key &key,
		QByteArray value,
		FnMut<void(Error)> done) {
	if (value.isEmpty()) {
		remove(key, [done = std::move(done)]() mutable {
			done(Error::NoError());
		});
		return;
	}
	_removing.erase(key);

	const auto checksum = CountChecksum(bytes::make_span(value));
	const auto path = writeKeyPlace(key, value.size(), checksum);
	if (path.isEmpty()) {
		invokeCallback(done, ioError(binlogPath()));
		return;
	}
	File data;
	const auto result = data.open(path, File::Mode::Write, _key);
	switch (result) {
	case File::Result::Failed:
		invokeCallback(done, ioError(path));
		break;

	case File::Result::LockFailed:
		invokeCallback(done, Error{ Error::Type::LockFailed, path });
		break;

	case File::Result::Success: {
		const auto success = data.writeWithPadding(bytes::make_span(value));
		if (!success) {
			data.close();
			remove(key, nullptr);
			invokeCallback(done, ioError(path));
		} else {
			data.flush();
			invokeCallback(done, Error::NoError());
			startDelayedPruning();
		}
	} break;

	default: Unexpected("Result in Database::put.");
	}
}

template <typename StoreRecord>
QString Database::writeKeyPlaceGeneric(
		StoreRecord &&record,
		const Key &key,
		size_type size,
		uint32 checksum) {
	Expects(size <= _settings.maxDataSize);

	record.key = key;
	record.size = ReadTo<EntrySize>(size);
	record.checksum = checksum;
	if (const auto i = _map.find(key); i != end(_map)) {
		record.place = i->second.place;
	} else {
		do {
			bytes::set_random(bytes::object_as_span(&record.place));
		} while (!isFreePlace(record.place));
	}
	const auto result = placePath(record.place);
	auto writeable = record;
	const auto success = _binlog.write(bytes::object_as_span(&writeable));
	if (!success) {
		return QString();
	}
	_binlog.flush();
	readRecordStore(bytes::object_as_span(&record));
	return result;
}

QString Database::writeKeyPlace(
		const Key &key,
		size_type size,
		uint32 checksum) {
	if (!_settings.trackEstimatedTime) {
		return writeKeyPlaceGeneric(Store(), key, size, checksum);
	}
	auto record = StoreWithTime();
	record.time = countTimePoint();
	if (record.time.relativeAdvancement * crl::time_type(1000)
		< _settings.writeBundleDelay) {
		// We don't want to produce a lot of unique relativeTime values.
		// So if change in it is not large we stick to the old value.
		record.time.system = _latestSystemTime;
		record.time.relativeAdvancement = 0;
	}
	return writeKeyPlaceGeneric(std::move(record), key, size, checksum);
}

void Database::get(const Key &key, FnMut<void(QByteArray)> done) {
	if (_removing.find(key) != end(_removing)) {
		invokeCallback(done, QByteArray());
		return;
	}
	const auto i = _map.find(key);
	if (i == _map.end()) {
		invokeCallback(done, QByteArray());
		return;
	}
	const auto &entry = i->second;

	const auto path = placePath(entry.place);
	File data;
	const auto result = data.open(path, File::Mode::Read, _key);
	switch (result) {
	case File::Result::Failed:
		invokeCallback(done, QByteArray());
		break;

	case File::Result::WrongKey:
		invokeCallback(done, QByteArray());
		break;

	case File::Result::Success: {
		auto result = QByteArray(entry.size, Qt::Uninitialized);
		const auto bytes = bytes::make_span(result);
		const auto read = data.readWithPadding(bytes);
		if (read != entry.size || CountChecksum(bytes) != entry.checksum) {
			invokeCallback(done, QByteArray());
		} else {
			invokeCallback(done, std::move(result));
			if (_settings.trackEstimatedTime) {
				_accessed.emplace(key);
				writeMultiAccessLazy();
			}
			startDelayedPruning();
		}
	} break;

	default: Unexpected("Result in Database::get.");
	}
}

void Database::remove(const Key &key, FnMut<void()> done) {
	const auto i = _map.find(key);
	if (i != _map.end()) {
		_removing.emplace(key);
		writeMultiRemoveLazy();

		const auto path = placePath(i->second.place);
		eraseMapEntry(i);
		QFile(path).remove();
	}
	invokeCallback(done);
}

void Database::writeBundlesLazy() {
	if (!_writeBundlesTimer.isActive()) {
		_writeBundlesTimer.callOnce(_settings.writeBundleDelay);
	}
}

void Database::writeMultiRemoveLazy() {
	if (_removing.size() == _settings.maxBundledRecords) {
		writeMultiRemove();
	} else {
		writeBundlesLazy();
	}
}

void Database::writeMultiRemove() {
	Expects(_removing.size() <= _settings.maxBundledRecords);

	if (_removing.empty()) {
		return;
	}
	const auto size = _removing.size();
	auto header = MultiRemoveHeader(size);
	auto list = std::vector<MultiRemovePart>();
	list.reserve(size);
	for (const auto &key : base::take(_removing)) {
		list.push_back({ key });
	}
	if (_binlog.write(bytes::object_as_span(&header))) {
		_binlog.write(bytes::make_span(list));
		_binlog.flush();
	}
}

void Database::writeMultiAccessLazy() {
	if (_accessed.size() == _settings.maxBundledRecords) {
		writeMultiAccess();
	} else {
		writeBundlesLazy();
	}
}

void Database::writeMultiAccess() {
	if (!_accessed.empty()) {
		writeMultiAccessBlock();
	}
}

void Database::writeMultiAccessBlock() {
	Expects(_settings.trackEstimatedTime);
	Expects(_accessed.size() <= _settings.maxBundledRecords);

	const auto time = countTimePoint();
	const auto size = _accessed.size();
	auto header = MultiAccessHeader(time, size);
	auto list = std::vector<MultiAccessPart>();
	if (size > 0) {
		list.reserve(size);
		for (const auto &key : base::take(_accessed)) {
			list.push_back({ key });
		}
	}
	applyTimePoint(time);
	for (const auto &entry : list) {
		if (const auto i = _map.find(entry.key); i != end(_map)) {
			i->second.useTime = _relativeTime;
		}
	}

	if (_binlog.write(bytes::object_as_span(&header))) {
		if (size > 0) {
			_binlog.write(bytes::make_span(list));
		}
		_binlog.flush();
	}
}

void Database::writeBundles() {
	writeMultiRemove();
	if (_settings.trackEstimatedTime) {
		writeMultiAccess();
	}
}

void Database::createCleaner() {
	auto [left, right] = base::make_binary_guard();
	_cleaner.guard = std::move(left);
	auto done = [weak = _weak](Error error) {
		weak.with([=](Database &that) {
			that.cleanerDone(error);
		});
	};
	_cleaner.object = std::make_unique<Cleaner>(
		_base,
		std::move(right),
		std::move(done));
}

void Database::cleanerDone(Error error) {
	_cleaner = CleanerWrap();
}

void Database::clear(FnMut<void(Error)> done) {
	Expects(_key.empty());

	const auto version = findAvailableVersion();
	invokeCallback(
		done,
		writeVersion(version) ? Error::NoError() : ioError(versionPath()));
}

Database::~Database() {
	close(nullptr);
}

auto Database::findAvailableVersion() const -> Version {
	const auto entries = QDir(_base).entryList(
		QDir::Dirs | QDir::NoDotAndDotDot);
	auto versions = base::flat_set<Version>();
	for (const auto entry : entries) {
		versions.insert(entry.toInt());
	}
	auto result = Version();
	for (const auto version : versions) {
		if (result != version) {
			break;
		}
		++result;
	}
	return result;
}

QString Database::versionPath() const {
	return VersionFilePath(_base);
}

bool Database::writeVersion(Version version) {
	return WriteVersionValue(_base, version);
}

auto Database::readVersion() const -> Version {
	if (const auto result = ReadVersionValue(_base)) {
		return *result;
	}
	return Version();
}

QString Database::placePath(PlaceId place) const {
	return _path + PlaceFromId(place);
}

bool Database::isFreePlace(PlaceId place) const {
	return !QFile(placePath(place)).exists();
}

} // namespace details

Database::Database(const QString &path, const Settings &settings)
: _wrapped(path, settings) {
}

void Database::open(EncryptionKey key, FnMut<void(Error)> done) {
	_wrapped.with([
		key,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.open(key, std::move(done));
	});
}

void Database::close(FnMut<void()> done) {
	_wrapped.with([
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.close(std::move(done));
	});
}

void Database::put(
		const Key &key,
		QByteArray value,
		FnMut<void(Error)> done) {
	_wrapped.with([
		key,
		value = std::move(value),
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.put(key, std::move(value), std::move(done));
	});
}

void Database::get(const Key &key, FnMut<void(QByteArray)> done) {
	_wrapped.with([
		key,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.get(key, std::move(done));
	});
}

void Database::remove(const Key &key, FnMut<void()> done) {
	_wrapped.with([
		key,
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.remove(key, std::move(done));
	});
}

void Database::clear(FnMut<void(Error)> done) {
	_wrapped.with([
		done = std::move(done)
	](Implementation &unwrapped) mutable {
		unwrapped.clear(std::move(done));
	});
}

Database::~Database() = default;

} // namespace Cache
} // namespace Storage
