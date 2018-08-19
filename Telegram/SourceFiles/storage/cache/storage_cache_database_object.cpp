/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_database_object.h"

#include "storage/cache/storage_cache_cleaner.h"
#include "storage/cache/storage_cache_compactor.h"
#include "storage/storage_encryption.h"
#include "storage/storage_encrypted_file.h"
#include "base/flat_map.h"
#include "base/algorithm.h"
#include <crl/crl.h>
#include <xxhash.h>
#include <QtCore/QDir>
#include <unordered_map>
#include <set>

namespace Storage {
namespace Cache {
namespace details {
namespace {

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

} // namespace

DatabaseObject::Entry::Entry(
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

DatabaseObject::DatabaseObject(
	crl::weak_on_queue<DatabaseObject> weak,
	const QString &path,
	const Settings &settings)
: _weak(std::move(weak))
, _base(ComputeBasePath(path))
, _settings(settings)
, _writeBundlesTimer(_weak, [=] { writeBundles(); checkCompactor(); })
, _pruneTimer(_weak, [=] { prune(); }) {
	Expects(_settings.maxDataSize < kDataSizeLimit);
	Expects(_settings.maxBundledRecords < kBundledRecordsLimit);
	Expects(!_settings.totalTimeLimit
		|| _settings.totalTimeLimit > 0);
	Expects(!_settings.totalSizeLimit
		|| _settings.totalSizeLimit > _settings.maxDataSize);
}

template <typename Callback, typename ...Args>
void DatabaseObject::invokeCallback(Callback &&callback, Args &&...args) {
	if (callback) {
		callback(std::move(args)...);
	}
}

Error DatabaseObject::ioError(const QString &path) const {
	return { Error::Type::IO, path };
}

void DatabaseObject::open(EncryptionKey key, FnMut<void(Error)> done) {
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
	default: Unexpected("Result from DatabaseObject::openBinlog.");
	}
}

QString DatabaseObject::computePath(Version version) const {
	return _base + QString::number(version) + '/';
}

QString DatabaseObject::binlogFilename() const {
	return QStringLiteral("binlog");
}

QString DatabaseObject::binlogPath(Version version) const {
	return computePath(version) + binlogFilename();
}

QString DatabaseObject::binlogPath() const {
	return _path + binlogFilename();
}

File::Result DatabaseObject::openBinlog(
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

bool DatabaseObject::readHeader() {
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

bool DatabaseObject::writeHeader() {
	auto header = BasicHeader();
	const auto now = _settings.trackEstimatedTime ? GetUnixtime() : 0;
	_relativeTime = _latestSystemTime = header.systemTime = now;
	if (_settings.trackEstimatedTime) {
		header.flags |= header.kTrackEstimatedTime;
	}
	return _binlog.write(bytes::object_as_span(&header));
}

void DatabaseObject::readBinlog() {
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
	optimize();
}

int64 DatabaseObject::countRelativeTime() const {
	const auto now = GetUnixtime();
	const auto delta = std::max(int64(now) - int64(_latestSystemTime), 0LL);
	return _relativeTime + delta;
}

int64 DatabaseObject::pruneBeforeTime() const {
	return _settings.totalTimeLimit
		? (countRelativeTime() - _settings.totalTimeLimit)
		: 0LL;
}

void DatabaseObject::optimize() {
	if (!startDelayedPruning()) {
		checkCompactor();
	}
}

bool DatabaseObject::startDelayedPruning() {
	if (!_settings.trackEstimatedTime || _map.empty()) {
		return false;
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
		return true;
	} else if (_minimalEntryTime != 0) {
		const auto before = pruneBeforeTime();
		const auto seconds = (_minimalEntryTime - before);
		if (!_pruneTimer.isActive()) {
			_pruneTimer.callOnce(std::min(
				seconds * crl::time_type(1000),
				_settings.maxPruneCheckTimeout));
		}
	}
	return false;
}

void DatabaseObject::prune() {
	auto stale = base::flat_set<Key>();
	auto staleTotalSize = int64();
	collectTimePrune(stale, staleTotalSize);
	collectSizePrune(stale, staleTotalSize);
	for (const auto &key : stale) {
		remove(key);
	}
	optimize();
}

void DatabaseObject::collectTimePrune(
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

void DatabaseObject::collectSizePrune(
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

void DatabaseObject::adjustRelativeTime() {
	if (!_settings.trackEstimatedTime) {
		return;
	}
	const auto now = GetUnixtime();
	if (now < _latestSystemTime) {
		writeMultiAccessBlock();
	}
}

size_type DatabaseObject::readBinlogRecords(bytes::const_span data) {
	auto result = 0;
	while (true) {
		const auto size = readBinlogRecordSize(data);
		if (size == kRecordSizeUnknown || size > data.size()) {
			return result;
		} else if (size == kRecordSizeInvalid
			|| !readBinlogRecord(data.subspan(0, size))) {
			return (result > 0) ? result : kRecordSizeInvalid;
		} else {
			result += size;
			data = data.subspan(size);
		}
	}
}

size_type DatabaseObject::readBinlogRecordSize(bytes::const_span data) const {
	if (data.empty()) {
		return kRecordSizeUnknown;
	}

	switch (static_cast<RecordType>(data[0])) {
	case Store::kType:
		return storeRecordSize();

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

bool DatabaseObject::readBinlogRecord(bytes::const_span data) {
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
	Unexpected("Bad type in DatabaseObject::readBinlogRecord.");
}

template <typename RecordStore>
bool DatabaseObject::readRecordStoreGeneric(bytes::const_span data) {
	Expects(data.size() == sizeof(RecordStore));

	return processRecordStore(
		reinterpret_cast<const RecordStore*>(data.data()),
		std::is_class<RecordStore>{});
}

template <typename RecordStore, typename Postprocess>
bool DatabaseObject::processRecordStoreGeneric(
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

bool DatabaseObject::processRecordStore(
		const Store *record,
		std::is_class<Store>) {
	const auto postprocess = [](auto&&...) { return true; };
	return processRecordStoreGeneric(record, postprocess);
}

bool DatabaseObject::processRecordStore(
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

bool DatabaseObject::readRecordStore(bytes::const_span data) {
	if (!_settings.trackEstimatedTime) {
		return readRecordStoreGeneric<Store>(data);
	}
	return readRecordStoreGeneric<StoreWithTime>(data);
}

template <typename StorePart>
bool DatabaseObject::readRecordMultiStoreGeneric(bytes::const_span data) {
	Expects(data.size() >= sizeof(MultiStoreHeader));

	const auto bytes = data.data();
	const auto record = reinterpret_cast<const MultiStoreHeader*>(bytes);
	const auto count = ReadFrom(record->count);
	Assert(data.size() == sizeof(MultiStoreHeader)
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

bool DatabaseObject::readRecordMultiStore(bytes::const_span data) {
	if (!_settings.trackEstimatedTime) {
		return readRecordMultiStoreGeneric<MultiStorePart>(data);
	}
	return readRecordMultiStoreGeneric<MultiStoreWithTimePart>(data);
}

size_type DatabaseObject::storeRecordSize() const {
	return _settings.trackEstimatedTime
		? sizeof(StoreWithTime)
		: sizeof(Store);
}

void DatabaseObject::setMapEntry(const Key &key, Entry &&entry) {
	auto &already = _map[key];
	_totalSize += entry.size - already.size;
	if (already.size != 0) {
		_binlogExcessLength += storeRecordSize();
	}
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

void DatabaseObject::eraseMapEntry(const Map::const_iterator &i) {
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

bool DatabaseObject::readRecordMultiRemove(bytes::const_span data) {
	Expects(data.size() >= sizeof(MultiRemoveHeader));

	const auto bytes = data.data();
	const auto record = reinterpret_cast<const MultiRemoveHeader*>(bytes);
	const auto count = ReadFrom(record->count);
	Assert(data.size() == sizeof(MultiRemoveHeader)
		+ count * sizeof(MultiRemovePart));
	const auto parts = gsl::make_span(
		reinterpret_cast<const MultiRemovePart*>(
			bytes + sizeof(MultiRemoveHeader)),
		count);
	for (const auto &part : parts) {
		if (const auto i = _map.find(part.key); i != end(_map)) {
			eraseMapEntry(i);
		}
	}
	_binlogExcessLength += data.size();
	return true;
}

EstimatedTimePoint DatabaseObject::countTimePoint() const {
	const auto now = std::max(GetUnixtime(), 1);
	const auto delta = std::max(int64(now) - int64(_latestSystemTime), 0LL);
	auto result = EstimatedTimePoint();
	result.system = now;
	result.relativeAdvancement = std::min(
		delta,
		int64(_settings.maxTimeAdvancement));
	return result;
}

void DatabaseObject::applyTimePoint(EstimatedTimePoint time) {
	_relativeTime += time.relativeAdvancement;
	_latestSystemTime = time.system;
}

bool DatabaseObject::readRecordMultiAccess(bytes::const_span data) {
	Expects(data.size() >= sizeof(MultiAccessHeader));
	Expects(_settings.trackEstimatedTime);

	const auto bytes = data.data();
	const auto record = reinterpret_cast<const MultiAccessHeader*>(bytes);
	if (record->time.relativeAdvancement > _settings.maxTimeAdvancement) {
		return false;
	}
	applyTimePoint(record->time);
	const auto count = ReadFrom(record->count);
	Assert(data.size() == sizeof(MultiAccessHeader)
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
	_binlogExcessLength += data.size();
	return true;
}

void DatabaseObject::close(FnMut<void()> done) {
	writeBundles();
	_cleaner = CleanerWrap();
	_compactor = nullptr;
	_binlog.close();
	invokeCallback(done);
	_map.clear();
	_binlogExcessLength = 0;
}

void DatabaseObject::put(
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
	const auto maybepath = writeKeyPlace(key, value, checksum);
	if (!maybepath) {
		invokeCallback(done, ioError(binlogPath()));
		return;
	} else if (maybepath->isEmpty()) {
		// Nothing changed.
		invokeCallback(done, Error::NoError());
		recordEntryAccess(key);
		return;
	}
	const auto path = *maybepath;
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
			optimize();
		}
	} break;

	default: Unexpected("Result in DatabaseObject::put.");
	}
}

template <typename StoreRecord>
base::optional<QString> DatabaseObject::writeKeyPlaceGeneric(
		StoreRecord &&record,
		const Key &key,
		const QByteArray &value,
		uint32 checksum) {
	Expects(value.size() <= _settings.maxDataSize);

	const auto size = size_type(value.size());
	record.key = key;
	record.size = ReadTo<EntrySize>(size);
	record.checksum = checksum;
	if (const auto i = _map.find(key); i != end(_map)) {
		const auto &already = i->second;
		if (already.tag == record.tag
			&& already.size == size
			&& already.checksum == checksum
			&& readValueData(already.place, size) == value) {
			return QString();
		}
		record.place = already.place;
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

	const auto applied = readRecordStore(bytes::object_as_span(&record));
	Assert(applied);
	return result;
}

base::optional<QString> DatabaseObject::writeKeyPlace(
		const Key &key,
		const QByteArray &data,
		uint32 checksum) {
	if (!_settings.trackEstimatedTime) {
		return writeKeyPlaceGeneric(Store(), key, data, checksum);
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
	return writeKeyPlaceGeneric(std::move(record), key, data, checksum);
}

void DatabaseObject::get(const Key &key, FnMut<void(QByteArray)> done) {
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

	auto result = readValueData(entry.place, entry.size);
	if (result.isEmpty()) {
		invokeCallback(done, QByteArray());
	} else if (CountChecksum(bytes::make_span(result)) != entry.checksum) {
		invokeCallback(done, QByteArray());
	} else {
		invokeCallback(done, std::move(result));
		recordEntryAccess(key);
	}
}

QByteArray DatabaseObject::readValueData(PlaceId place, size_type size) const {
	const auto path = placePath(place);
	File data;
	const auto result = data.open(path, File::Mode::Read, _key);
	switch (result) {
	case File::Result::Failed:
	case File::Result::WrongKey: return QByteArray();
	case File::Result::Success: {
		auto result = QByteArray(size, Qt::Uninitialized);
		const auto bytes = bytes::make_span(result);
		const auto read = data.readWithPadding(bytes);
		if (read != size) {
			return QByteArray();
		}
		return result;
	} break;
	default: Unexpected("Result in DatabaseObject::get.");
	}
}

void DatabaseObject::recordEntryAccess(const Key &key) {
	if (!_settings.trackEstimatedTime) {
		return;
	}
	_accessed.emplace(key);
	writeMultiAccessLazy();
	optimize();
}

void DatabaseObject::remove(const Key &key, FnMut<void()> done) {
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

void DatabaseObject::writeBundlesLazy() {
	if (!_writeBundlesTimer.isActive()) {
		_writeBundlesTimer.callOnce(_settings.writeBundleDelay);
	}
}

void DatabaseObject::writeMultiRemoveLazy() {
	if (_removing.size() == _settings.maxBundledRecords) {
		writeMultiRemove();
	} else {
		writeBundlesLazy();
	}
}

void DatabaseObject::writeMultiRemove() {
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

		_binlogExcessLength += bytes::object_as_span(&header).size()
			+ bytes::make_span(list).size();
	}
}

void DatabaseObject::writeMultiAccessLazy() {
	if (_accessed.size() == _settings.maxBundledRecords) {
		writeMultiAccess();
	} else {
		writeBundlesLazy();
	}
}

void DatabaseObject::writeMultiAccess() {
	if (!_accessed.empty()) {
		writeMultiAccessBlock();
	}
}

void DatabaseObject::writeMultiAccessBlock() {
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

		_binlogExcessLength += bytes::object_as_span(&header).size()
			+ bytes::make_span(list).size();
	}
}

void DatabaseObject::writeBundles() {
	writeMultiRemove();
	if (_settings.trackEstimatedTime) {
		writeMultiAccess();
	}
}

void DatabaseObject::createCleaner() {
	auto [left, right] = base::make_binary_guard();
	_cleaner.guard = std::move(left);
	auto done = [weak = _weak](Error error) {
		weak.with([=](DatabaseObject &that) {
			that.cleanerDone(error);
		});
	};
	_cleaner.object = std::make_unique<Cleaner>(
		_base,
		std::move(right),
		std::move(done));
}

void DatabaseObject::cleanerDone(Error error) {
	_cleaner = CleanerWrap();
}

void DatabaseObject::checkCompactor() {
	if (_compactor
		|| !_settings.compactAfterExcess
		|| _binlogExcessLength < _settings.compactAfterExcess) {
		return;
	} else if (_settings.compactAfterFullSize) {
		if (_binlogExcessLength * _settings.compactAfterFullSize
			< _settings.compactAfterExcess * _binlog.size()) {
			return;
		}
	}
	_compactor = std::make_unique<Compactor>(_path, _weak);
}

void DatabaseObject::clear(FnMut<void(Error)> done) {
	Expects(_key.empty());

	const auto version = findAvailableVersion();
	invokeCallback(
		done,
		writeVersion(version) ? Error::NoError() : ioError(versionPath()));
}

DatabaseObject::~DatabaseObject() {
	close(nullptr);
}

auto DatabaseObject::findAvailableVersion() const -> Version {
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

QString DatabaseObject::versionPath() const {
	return VersionFilePath(_base);
}

bool DatabaseObject::writeVersion(Version version) {
	return WriteVersionValue(_base, version);
}

auto DatabaseObject::readVersion() const -> Version {
	if (const auto result = ReadVersionValue(_base)) {
		return *result;
	}
	return Version();
}

QString DatabaseObject::placePath(PlaceId place) const {
	return _path + PlaceFromId(place);
}

bool DatabaseObject::isFreePlace(PlaceId place) const {
	return !QFile(placePath(place)).exists();
}

} // namespace details
} // namespace Cache
} // namespace Storage
