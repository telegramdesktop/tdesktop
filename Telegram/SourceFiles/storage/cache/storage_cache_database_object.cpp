/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_database_object.h"

#include "storage/cache/storage_cache_cleaner.h"
#include "storage/cache/storage_cache_compactor.h"
#include "storage/cache/storage_cache_binlog_reader.h"
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

constexpr auto kMaxDelayAfterFailure = 24 * 60 * 60 * crl::time_type(1000);

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
	uint64 useTime)
: useTime(useTime)
, size(size)
, checksum(checksum)
, place(place)
, tag(tag) {
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
	checkSettings();
}

void DatabaseObject::reconfigure(const Settings &settings) {
	Expects(_key.empty());

	_settings = settings;
	checkSettings();
}

void DatabaseObject::updateSettings(const SettingsUpdate &update) {
	_settings.totalSizeLimit = update.totalSizeLimit;
	_settings.totalTimeLimit = update.totalTimeLimit;
	checkSettings();

	optimize();
}

void DatabaseObject::checkSettings() {
	Expects(_settings.staleRemoveChunk > 0);
	Expects(_settings.maxDataSize > 0
		&& _settings.maxDataSize < kDataSizeLimit);
	Expects(_settings.maxBundledRecords > 0
		&& _settings.maxBundledRecords < kBundledRecordsLimit);
	Expects(!_settings.totalTimeLimit
		|| _settings.totalTimeLimit > 0);
	Expects(!_settings.totalSizeLimit
		|| _settings.totalSizeLimit > _settings.maxDataSize);
}

template <typename Callback, typename ...Args>
void DatabaseObject::invokeCallback(
		Callback &&callback,
		Args &&...args) const {
	if (callback) {
		callback(std::move(args)...);
	}
}

Error DatabaseObject::ioError(const QString &path) const {
	return { Error::Type::IO, path };
}

void DatabaseObject::open(EncryptionKey &&key, FnMut<void(Error)> &&done) {
	close(nullptr);

	const auto error = openSomeBinlog(std::move(key));
	if (error.type != Error::Type::None) {
		close(nullptr);
	}
	invokeCallback(done, error);
}

Error DatabaseObject::openSomeBinlog(EncryptionKey &&key) {
	const auto version = readVersion();
	const auto result = openBinlog(version, File::Mode::ReadAppend, key);
	switch (result) {
	case File::Result::Success: return Error::NoError();
	case File::Result::Failed: return openNewBinlog(key);
	case File::Result::LockFailed:
		return Error{ Error::Type::LockFailed, binlogPath(version) };
	case File::Result::WrongKey:
		return _settings.clearOnWrongKey
			? openNewBinlog(key)
			: Error{ Error::Type::WrongKey, binlogPath(version) };
	}
	Unexpected("Result from DatabaseObject::openBinlog.");
}

Error DatabaseObject::openNewBinlog(EncryptionKey &key) {
	const auto available = findAvailableVersion();
	if (!writeVersion(available)) {
		return ioError(versionPath());
	}
	const auto open = openBinlog(available, File::Mode::Write, key);
	if (open != File::Result::Success) {
		return ioError(binlogPath(available));
	}
	return Error::NoError();
}

QString DatabaseObject::computePath(Version version) const {
	return _base + QString::number(version) + '/';
}

QString DatabaseObject::BinlogFilename() {
	return QStringLiteral("binlog");
}

QString DatabaseObject::CompactReadyFilename() {
	return QStringLiteral("binlog-ready");
}

QString DatabaseObject::binlogPath(Version version) const {
	return computePath(version) + BinlogFilename();
}

QString DatabaseObject::binlogPath() const {
	return _path + BinlogFilename();
}

QString DatabaseObject::compactReadyPath(Version version) const {
	return computePath(version) + CompactReadyFilename();
}

QString DatabaseObject::compactReadyPath() const {
	return _path + CompactReadyFilename();
}

File::Result DatabaseObject::openBinlog(
		Version version,
		File::Mode mode,
		EncryptionKey &key) {
	const auto ready = compactReadyPath(version);
	const auto path = binlogPath(version);
	if (QFile(ready).exists() && !File::Move(ready, path)) {
		return File::Result::Failed;
	}
	const auto result = _binlog.open(path, mode, key);
	if (result != File::Result::Success) {
		return result;
	}
	const auto headerRequired = (mode == File::Mode::Read)
		|| (mode == File::Mode::ReadAppend && _binlog.size() > 0);
	const auto headerResult = headerRequired ? readHeader() : writeHeader();
	if (!headerResult) {
		return File::Result::Failed;
	}
	_path = computePath(version);
	_key = std::move(key);
	createCleaner();
	readBinlog();
	return File::Result::Success;
}

bool DatabaseObject::readHeader() {
	if (const auto header = BinlogWrapper::ReadHeader(_binlog, _settings)) {
		_time.setRelative((_time.system = header->systemTime));
		return true;
	}
	return false;
}

bool DatabaseObject::writeHeader() {
	auto header = BasicHeader();
	const auto now = _settings.trackEstimatedTime ? GetUnixtime() : 0;
	_time.setRelative((_time.system = header.systemTime = now));
	if (_settings.trackEstimatedTime) {
		header.flags |= header.kTrackEstimatedTime;
	}
	return _binlog.write(bytes::object_as_span(&header));
}

template <typename Reader, typename ...Handlers>
void DatabaseObject::readBinlogHelper(
		Reader &reader,
		Handlers &&...handlers) {
	while (true) {
		const auto done = reader.readTillEnd(
			std::forward<Handlers>(handlers)...);
		if (done) {
			break;
		}
	}
}

void DatabaseObject::readBinlog() {
	BinlogWrapper wrapper(_binlog, _settings);
	if (_settings.trackEstimatedTime) {
		BinlogReader<
			StoreWithTime,
			MultiStoreWithTime,
			MultiRemove,
			MultiAccess> reader(wrapper);
		readBinlogHelper(reader, [&](const StoreWithTime &record) {
			return processRecordStore(
				&record,
				std::is_class<StoreWithTime>{});
		}, [&](const MultiStoreWithTime &header, const auto &element) {
			return processRecordMultiStore(header, element);
		}, [&](const MultiRemove &header, const auto &element) {
			return processRecordMultiRemove(header, element);
		}, [&](const MultiAccess &header, const auto &element) {
			return processRecordMultiAccess(header, element);
		});
	} else {
		BinlogReader<
			Store,
			MultiStore,
			MultiRemove> reader(wrapper);
		readBinlogHelper(reader, [&](const Store &record) {
			return processRecordStore(&record, std::is_class<Store>{});
		}, [&](const MultiStore &header, const auto &element) {
			return processRecordMultiStore(header, element);
		}, [&](const MultiRemove &header, const auto &element) {
			return processRecordMultiRemove(header, element);
		});
	}
	adjustRelativeTime();
	optimize();
}

uint64 DatabaseObject::countRelativeTime() const {
	const auto now = GetUnixtime();
	const auto delta = std::max(int64(now) - int64(_time.system), 0LL);
	return _time.getRelative() + delta;
}

uint64 DatabaseObject::pruneBeforeTime() const {
	const auto relative = countRelativeTime();
	return (_settings.totalTimeLimit && relative > _settings.totalTimeLimit)
		? (relative - _settings.totalTimeLimit)
		: 0ULL;
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
	const auto before = pruneBeforeTime();
	const auto pruning = [&] {
		if (_settings.totalSizeLimit > 0
			&& _totalSize > _settings.totalSizeLimit) {
			return true;
		} else if ((!_minimalEntryTime && !_map.empty())
			|| _minimalEntryTime <= before) {
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
		Assert(_minimalEntryTime > before);
		const auto seconds = int64(_minimalEntryTime - before);
		if (!_pruneTimer.isActive()) {
			_pruneTimer.callOnce(std::min(
				crl::time_type(seconds * 1000),
				_settings.maxPruneCheckTimeout));
		}
	}
	return false;
}

void DatabaseObject::prune() {
	if (!_stale.empty()) {
		return;
	}
	auto stale = base::flat_set<Key>();
	auto staleTotalSize = int64();
	collectTimeStale(stale, staleTotalSize);
	collectSizeStale(stale, staleTotalSize);
	if (stale.size() <= _settings.staleRemoveChunk) {
		clearStaleNow(stale);
	} else {
		_stale = ranges::view::all(stale) | ranges::to_vector;
		startStaleClear();
	}
}

void DatabaseObject::startStaleClear() {
	// Report "Clearing..." status.
	pushStats();
	clearStaleChunk();
}

void DatabaseObject::clearStaleNow(const base::flat_set<Key> &stale) {
	if (stale.empty()) {
		return;
	}

	// Report "Clearing..." status.
	_stale.push_back(stale.front());
	pushStats();

	for (const auto &key : stale) {
		remove(key, nullptr);
	}

	// Report correct status async.
	_stale.clear();
	optimize();
}

void DatabaseObject::clearStaleChunkDelayed() {
	if (_clearingStale) {
		return;
	}
	_clearingStale = true;
	_weak.with([](DatabaseObject &that) {
		if (base::take(that._clearingStale)) {
			that.clearStaleChunk();
		}
	});
}

void DatabaseObject::clearStaleChunk() {
	if (_stale.empty()) {
		return;
	}
	const auto stale = gsl::make_span(_stale);
	const auto count = size_type(stale.size());
	const auto clear = std::min(count, _settings.staleRemoveChunk);
	for (const auto &key : stale.subspan(count - clear)) {
		remove(key, nullptr);
	}
	_stale.resize(count - clear);
	if (_stale.empty()) {
		base::take(_stale);
		optimize();
	} else {
		clearStaleChunkDelayed();
	}
}

void DatabaseObject::collectTimeStale(
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

void DatabaseObject::collectSizeStale(
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
	if (now < _time.system) {
		writeMultiAccessBlock();
	}
}

template <typename Record, typename Postprocess>
bool DatabaseObject::processRecordStoreGeneric(
		const Record *record,
		Postprocess &&postprocess) {
	const auto size = record->getSize();
	if (size <= 0 || size > _settings.maxDataSize) {
		return false;
	}
	auto entry = Entry(
		record->place,
		record->tag,
		record->checksum,
		size,
		_time.getRelative());
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
		entry.useTime = record->time.getRelative();
		return true;
	};
	return processRecordStoreGeneric(record, postprocess);
}

template <typename Record, typename GetElement>
bool DatabaseObject::processRecordMultiStore(
		const Record &header,
		const GetElement &element) {
	while (const auto entry = element()) {
		if (!processRecordStore(
				entry,
				std::is_class<typename Record::Part>{})) {
			return false;
		}
	}
	return true;
}

template <typename GetElement>
bool DatabaseObject::processRecordMultiRemove(
		const MultiRemove &header,
		const GetElement &element) {
	_binlogExcessLength += sizeof(header);
	while (const auto entry = element()) {
		_binlogExcessLength += sizeof(*entry);
		if (const auto i = _map.find(*entry); i != end(_map)) {
			eraseMapEntry(i);
		}
	}
	return true;
}

template <typename GetElement>
bool DatabaseObject::processRecordMultiAccess(
		const MultiAccess &header,
		const GetElement &element) {
	Expects(_settings.trackEstimatedTime);

	applyTimePoint(header.time);
	const auto relative = header.time.getRelative();

	_binlogExcessLength += sizeof(header);
	while (const auto entry = element()) {
		_binlogExcessLength += sizeof(*entry);
		if (const auto i = _map.find(*entry); i != end(_map)) {
			i->second.useTime = relative;
		}
	}
	return true;
}

void DatabaseObject::setMapEntry(const Key &key, Entry &&entry) {
	auto &already = _map[key];
	updateStats(already, entry);
	if (already.size != 0) {
		_binlogExcessLength += _settings.trackEstimatedTime
			? sizeof(StoreWithTime)
			: sizeof(Store);
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
			if (!--_entriesWithMinimalTimeCount) {
				_minimalEntryTime = 0;
			}
		}
	}
	already = std::move(entry);
}

void DatabaseObject::updateStats(const Entry &was, const Entry &now) {
	_totalSize += now.size - was.size;
	if (now.tag == was.tag) {
		if (now.tag) {
			auto &summary = _taggedStats[now.tag];
			summary.count += (now.size ? 1 : 0) - (was.size ? 1 : 0);
			summary.totalSize += now.size - was.size;
		}
	} else {
		if (now.tag) {
			auto &summary = _taggedStats[now.tag];
			summary.count += (now.size ? 1 : 0);
			summary.totalSize += now.size;
		}
		if (was.tag) {
			auto &summary = _taggedStats[was.tag];
			summary.count -= (was.size ? 1 : 0);
			summary.totalSize -= was.size;
		}
	}
	pushStatsDelayed();
}

void DatabaseObject::pushStatsDelayed() {
	if (_pushingStats) {
		return;
	}
	_pushingStats = true;
	_weak.with([](DatabaseObject &that) {
		if (base::take(that._pushingStats)) {
			that.pushStats();
		}
	});
}

void DatabaseObject::pushStats() {
	if (_stats.has_consumers()) {
		_stats.fire(collectStats());
	}
}

void DatabaseObject::eraseMapEntry(const Map::const_iterator &i) {
	if (i != end(_map)) {
		const auto &entry = i->second;
		updateStats(entry, Entry());
		if (_minimalEntryTime != 0 && entry.useTime == _minimalEntryTime) {
			Assert(_entriesWithMinimalTimeCount > 0);
			if (!--_entriesWithMinimalTimeCount) {
				_minimalEntryTime = 0;
			}
		}
		_map.erase(i);
	}
}

EstimatedTimePoint DatabaseObject::countTimePoint() const {
	const auto now = GetUnixtime();
	const auto delta = std::max(int64(now) - int64(_time.system), 0LL);
	auto result = EstimatedTimePoint();
	result.system = now;
	result.setRelative(_time.getRelative() + delta);
	return result;
}

void DatabaseObject::applyTimePoint(EstimatedTimePoint time) {
	const auto possible = time.getRelative();
	const auto current = _time.getRelative();
	if (possible > current) {
		_time = time;
	}
}

void DatabaseObject::compactorDone(
		const QString &path,
		int64 originalReadTill) {
	const auto size = _binlog.size();
	const auto binlog = binlogPath();
	const auto ready = compactReadyPath();
	if (originalReadTill != size) {
		originalReadTill = CatchUp(
			path,
			binlog,
			_key,
			originalReadTill,
			_settings.readBlockSize);
		if (originalReadTill != size) {
			compactorFail();
			return;
		}
	}
	if (!File::Move(path, ready)) {
		compactorFail();
		return;
	}
	const auto guard = gsl::finally([&] {
		_compactor = CompactorWrap();
	});
	_binlog.close();
	if (!File::Move(ready, binlog)) {
		compactorFail();
		return;
	}
	const auto result = _binlog.open(binlog, File::Mode::ReadAppend, _key);
	if (result != File::Result::Success) {
		compactorFail();
		return;
	} else if (!_binlog.seek(_binlog.size())) {
		_binlog.close();
		compactorFail();
		return;
	}
	_binlogExcessLength -= _compactor.excessLength;
	Assert(_binlogExcessLength >= 0);
}

void DatabaseObject::compactorFail() {
	const auto delay = _compactor.delayAfterFailure;
	_compactor = CompactorWrap();
	_compactor.nextAttempt = crl::time() + delay;
	_compactor.delayAfterFailure = std::min(
		delay * 2,
		kMaxDelayAfterFailure);
	QFile(compactReadyPath()).remove();
}

void DatabaseObject::close(FnMut<void()> &&done) {
	if (_binlog.isOpen()) {
		writeBundles();
		_binlog.close();
	}
	invokeCallback(done);
	clearState();
}

void DatabaseObject::clearState() {
	_path = QString();
	_key = {};
	_map = {};
	_removing = {};
	_accessed = {};
	_stale = {};
	_time = {};
	_binlogExcessLength = 0;
	_totalSize = 0;
	_minimalEntryTime = 0;
	_entriesWithMinimalTimeCount = 0;
	_taggedStats = {};
	_pushingStats = false;
	_writeBundlesTimer.cancel();
	_pruneTimer.cancel();
	_compactor = CompactorWrap();
}

void DatabaseObject::put(
		const Key &key,
		TaggedValue &&value,
		FnMut<void(Error)> &&done) {
	if (value.bytes.isEmpty()) {
		remove(key, std::move(done));
		return;
	}
	_removing.erase(key);
	_stale.erase(ranges::remove(_stale, key), end(_stale));

	const auto checksum = CountChecksum(bytes::make_span(value.bytes));
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
		remove(key, nullptr);
		invokeCallback(done, ioError(path));
		break;

	case File::Result::LockFailed:
		remove(key, nullptr);
		invokeCallback(done, Error{ Error::Type::LockFailed, path });
		break;

	case File::Result::Success: {
		const auto success = data.writeWithPadding(
			bytes::make_span(value.bytes));
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
		const TaggedValue &value,
		uint32 checksum) {
	Expects(value.bytes.size() <= _settings.maxDataSize);

	const auto size = size_type(value.bytes.size());
	record.tag = value.tag;
	record.key = key;
	record.setSize(size);
	record.checksum = checksum;
	if (const auto i = _map.find(key); i != end(_map)) {
		const auto &already = i->second;
		if (already.tag == record.tag
			&& already.size == size
			&& already.checksum == checksum
			&& readValueData(already.place, size) == value.bytes) {
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
		_binlog.close();
		return QString();
	}
	_binlog.flush();

	const auto applied = processRecordStore(
		&record,
		std::is_class<StoreRecord>{});
	Assert(applied);
	return result;
}

base::optional<QString> DatabaseObject::writeKeyPlace(
		const Key &key,
		const TaggedValue &data,
		uint32 checksum) {
	if (!_settings.trackEstimatedTime) {
		return writeKeyPlaceGeneric(Store(), key, data, checksum);
	}
	auto record = StoreWithTime();
	record.time = countTimePoint();
	const auto writing = record.time.getRelative();
	const auto current = _time.getRelative();
	Assert(writing >= current);
	if ((writing - current) * crl::time_type(1000)
		< _settings.writeBundleDelay) {
		// We don't want to produce a lot of unique _time.relative values.
		// So if change in it is not large we stick to the old value.
		record.time = _time;
	}
	return writeKeyPlaceGeneric(std::move(record), key, data, checksum);
}

template <typename StoreRecord>
Error DatabaseObject::writeExistingPlaceGeneric(
		StoreRecord &&record,
		const Key &key,
		const Entry &entry) {
	record.key = key;
	record.tag = entry.tag;
	record.setSize(entry.size);
	record.checksum = entry.checksum;
	if (const auto i = _map.find(key); i != end(_map)) {
		const auto &already = i->second;
		if (already.tag == record.tag
			&& already.size == entry.size
			&& already.checksum == entry.checksum
			&& (readValueData(already.place, already.size)
				== readValueData(entry.place, entry.size))) {
			return Error::NoError();
		}
	}
	record.place = entry.place;
	auto writeable = record;
	const auto success = _binlog.write(bytes::object_as_span(&writeable));
	if (!success) {
		_binlog.close();
		return ioError(binlogPath());
	}
	_binlog.flush();

	const auto applied = processRecordStore(
		&record,
		std::is_class<StoreRecord>{});
	Assert(applied);
	return Error::NoError();
}

Error DatabaseObject::writeExistingPlace(
		const Key &key,
		const Entry &entry) {
	if (!_settings.trackEstimatedTime) {
		return writeExistingPlaceGeneric(Store(), key, entry);
	}
	auto record = StoreWithTime();
	record.time = countTimePoint();
	const auto writing = record.time.getRelative();
	const auto current = _time.getRelative();
	Assert(writing >= current);
	if ((writing - current) * crl::time_type(1000)
		< _settings.writeBundleDelay) {
		// We don't want to produce a lot of unique _time.relative values.
		// So if change in it is not large we stick to the old value.
		record.time = _time;
	}
	return writeExistingPlaceGeneric(std::move(record), key, entry);
}

void DatabaseObject::get(
		const Key &key,
		FnMut<void(TaggedValue&&)> &&done) {
	const auto i = _map.find(key);
	if (i == _map.end()) {
		invokeCallback(done, TaggedValue());
		return;
	}
	const auto &entry = i->second;

	auto bytes = readValueData(entry.place, entry.size);
	if (bytes.isEmpty()) {
		remove(key, nullptr);
		invokeCallback(done, TaggedValue());
	} else if (CountChecksum(bytes::make_span(bytes)) != entry.checksum) {
		remove(key, nullptr);
		invokeCallback(done, TaggedValue());
	} else {
		invokeCallback(done, TaggedValue(std::move(bytes), entry.tag));
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
	}
	Unexpected("Result in DatabaseObject::get.");
}

void DatabaseObject::recordEntryAccess(const Key &key) {
	if (!_settings.trackEstimatedTime) {
		return;
	}
	_accessed.emplace(key);
	writeMultiAccessLazy();
	optimize();
}

void DatabaseObject::remove(const Key &key, FnMut<void(Error)> &&done) {
	const auto i = _map.find(key);
	if (i != _map.end()) {
		_removing.emplace(key);
		writeMultiRemoveLazy();

		const auto path = placePath(i->second.place);
		eraseMapEntry(i);
		if (QFile(path).remove() || !QFile(path).exists()) {
			invokeCallback(done, Error::NoError());
		} else {
			invokeCallback(done, ioError(path));
		}
	} else {
		invokeCallback(done, Error::NoError());
	}
}

void DatabaseObject::putIfEmpty(
		const Key &key,
		TaggedValue &&value,
		FnMut<void(Error)> &&done) {
	if (_map.find(key) != end(_map)) {
		invokeCallback(done, Error::NoError());
		return;
	}
	put(key, std::move(value), std::move(done));
}

void DatabaseObject::copyIfEmpty(
		const Key &from,
		const Key &to,
		FnMut<void(Error)> &&done) {
	if (_map.find(to) != end(_map)) {
		invokeCallback(done, Error::NoError());
		return;
	}
	get(from, [&](TaggedValue &&value) {
		put(to, std::move(value), std::move(done));
	});
}

void DatabaseObject::moveIfEmpty(
		const Key &from,
		const Key &to,
		FnMut<void(Error)> &&done) {
	if (_map.find(to) != end(_map)) {
		invokeCallback(done, Error::NoError());
		return;
	}
	const auto i = _map.find(from);
	if (i == _map.end()) {
		invokeCallback(done, Error::NoError());
		return;
	}
	_removing.emplace(from);

	const auto entry = i->second;
	eraseMapEntry(i);

	const auto result = writeMultiRemove();
	if (result.type != Error::Type::None) {
		invokeCallback(done, result);
		return;
	}
	_removing.erase(to);
	_stale.erase(ranges::remove(_stale, to), end(_stale));
	invokeCallback(done, writeExistingPlace(to, entry));
}

rpl::producer<Stats> DatabaseObject::stats() const {
	return _stats.events_starting_with(collectStats());
}

Stats DatabaseObject::collectStats() const {
	auto result = Stats();
	result.tagged = _taggedStats;
	result.full.count = _map.size();
	result.full.totalSize = _totalSize;
	result.clearing = (_cleaner.object != nullptr) || !_stale.empty();
	return result;
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

Error DatabaseObject::writeMultiRemove() {
	Expects(_removing.size() <= _settings.maxBundledRecords);

	if (_removing.empty()) {
		return Error::NoError();
	}
	const auto size = _removing.size();
	auto header = MultiRemove(size);
	auto list = std::vector<MultiRemove::Part>();
	list.reserve(size);
	for (const auto &key : base::take(_removing)) {
		list.push_back(key);
	}
	if (_binlog.write(bytes::object_as_span(&header))
		&& _binlog.write(bytes::make_span(list))) {
		_binlog.flush();
		_binlogExcessLength += bytes::object_as_span(&header).size()
			+ bytes::make_span(list).size();
		return Error::NoError();
	}
	_binlog.close();
	return ioError(binlogPath());
}

void DatabaseObject::writeMultiAccessLazy() {
	if (_accessed.size() == _settings.maxBundledRecords) {
		writeMultiAccess();
	} else {
		writeBundlesLazy();
	}
}

Error DatabaseObject::writeMultiAccess() {
	if (_accessed.empty()) {
		return Error::NoError();
	}
	return writeMultiAccessBlock();
}

Error DatabaseObject::writeMultiAccessBlock() {
	Expects(_settings.trackEstimatedTime);
	Expects(_accessed.size() <= _settings.maxBundledRecords);

	const auto time = countTimePoint();
	const auto size = _accessed.size();
	auto header = MultiAccess(time, size);
	auto list = std::vector<MultiAccess::Part>();
	if (size > 0) {
		list.reserve(size);
		for (const auto &key : base::take(_accessed)) {
			list.push_back(key);
		}
	}
	_time = time;
	for (const auto &entry : list) {
		if (const auto i = _map.find(entry); i != end(_map)) {
			i->second.useTime = _time.getRelative();
		}
	}

	if (_binlog.write(bytes::object_as_span(&header))
		&& (!size || _binlog.write(bytes::make_span(list)))) {
		_binlog.flush();
		_binlogExcessLength += bytes::object_as_span(&header).size()
			+ bytes::make_span(list).size();
		return Error::NoError();
	}
	_binlog.close();
	return ioError(binlogPath());
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
	pushStatsDelayed();
}

void DatabaseObject::cleanerDone(Error error) {
	invokeCallback(_cleaner.done);
	_cleaner = CleanerWrap();
	pushStatsDelayed();
}

void DatabaseObject::checkCompactor() {
	if (_compactor.object
		|| !_settings.compactAfterExcess
		|| _binlogExcessLength < _settings.compactAfterExcess) {
		return;
	} else if (_settings.compactAfterFullSize
		&& (_binlogExcessLength * _settings.compactAfterFullSize
			< _settings.compactAfterExcess * _binlog.size())) {
		return;
	} else if (crl::time() < _compactor.nextAttempt || !_binlog.isOpen()) {
		return;
	}
	auto info = Compactor::Info();
	info.till = _binlog.size();
	info.systemTime = _time.system;
	info.keysCount = _map.size();
	auto [first, second] = base::make_binary_guard();
	_compactor.guard = std::move(first);
	_compactor.object = std::make_unique<Compactor>(
		_weak,
		std::move(second),
		_path,
		_settings,
		base::duplicate(_key),
		info);
	_compactor.excessLength = _binlogExcessLength;
}

void DatabaseObject::clear(FnMut<void(Error)> &&done) {
	auto key = std::move(_key);
	if (!key.empty()) {
		close(nullptr);
	}
	const auto version = findAvailableVersion();
	if (!writeVersion(version)) {
		invokeCallback(done, ioError(versionPath()));
		return;
	}
	if (key.empty()) {
		invokeCallback(done, Error::NoError());
		createCleaner();
		return;
	}
	open(std::move(key), std::move(done));
}

void DatabaseObject::clearByTag(uint8 tag, FnMut<void(Error)> &&done) {
	const auto hadStale = !_stale.empty();
	for (const auto &[key, entry] : _map) {
		if (entry.tag == tag) {
			_stale.push_back(key);
		}
	}
	if (!hadStale) {
		startStaleClear();
	}
	invokeCallback(done, Error::NoError());
}

void DatabaseObject::waitForCleaner(FnMut<void()> &&done) {
	while (!_stale.empty()) {
		clearStaleChunk();
	}
	if (_cleaner.object) {
		_cleaner.done = std::move(done);
	} else {
		invokeCallback(done);
	}
}

auto DatabaseObject::getManyRaw(const std::vector<Key> &keys) const
-> std::vector<Raw> {
	auto result = std::vector<Raw>();
	result.reserve(keys.size());
	for (const auto &key : keys) {
		if (const auto i = _map.find(key); i != end(_map)) {
			result.push_back(*i);
		}
	}
	return result;
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
