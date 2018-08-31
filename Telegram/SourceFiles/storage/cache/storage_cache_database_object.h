/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/cache/storage_cache_database.h"
#include "storage/storage_encrypted_file.h"
#include "base/binary_guard.h"
#include "base/concurrent_timer.h"
#include "base/bytes.h"
#include "base/flat_set.h"
#include <set>
#include <rpl/event_stream.h>

namespace Storage {
namespace Cache {
namespace details {

class Cleaner;
class Compactor;

class DatabaseObject {
public:
	using Settings = Cache::Database::Settings;
	DatabaseObject(
		crl::weak_on_queue<DatabaseObject> weak,
		const QString &path,
		const Settings &settings);
	void reconfigure(const Settings &settings);
	void updateSettings(const SettingsUpdate &update);

	void open(EncryptionKey &&key, FnMut<void(Error)> &&done);
	void close(FnMut<void()> &&done);

	void put(
		const Key &key,
		TaggedValue &&value,
		FnMut<void(Error)> &&done);
	void get(const Key &key, FnMut<void(TaggedValue&&)> &&done);
	void remove(const Key &key, FnMut<void(Error)> &&done);

	void putIfEmpty(
		const Key &key,
		TaggedValue &&value,
		FnMut<void(Error)> &&done);
	void copyIfEmpty(
		const Key &from,
		const Key &to,
		FnMut<void(Error)> &&done);
	void moveIfEmpty(
		const Key &from,
		const Key &to,
		FnMut<void(Error)> &&done);

	rpl::producer<Stats> stats() const;

	void clear(FnMut<void(Error)> &&done);
	void clearByTag(uint8 tag, FnMut<void(Error)> &&done);
	void waitForCleaner(FnMut<void()> &&done);

	static QString BinlogFilename();
	static QString CompactReadyFilename();

	void compactorDone(const QString &path, int64 originalReadTill);
	void compactorFail();

	struct Entry {
		Entry() = default;
		Entry(
			PlaceId place,
			uint8 tag,
			uint32 checksum,
			size_type size,
			uint64 useTime);

		uint64 useTime = 0;
		size_type size = 0;
		uint32 checksum = 0;
		PlaceId place = { { 0 } };
		uint8 tag = 0;
	};
	using Raw = std::pair<Key, Entry>;
	std::vector<Raw> getManyRaw(const std::vector<Key> &keys) const;

	~DatabaseObject();

private:
	struct CleanerWrap {
		std::unique_ptr<Cleaner> object;
		base::binary_guard guard;
		FnMut<void()> done;
	};
	struct CompactorWrap {
		std::unique_ptr<Compactor> object;
		int64 excessLength = 0;
		crl::time_type nextAttempt = 0;
		crl::time_type delayAfterFailure = 10 * crl::time_type(1000);
		base::binary_guard guard;
	};
	using Map = std::unordered_map<Key, Entry>;

	template <typename Callback, typename ...Args>
	void invokeCallback(Callback &&callback, Args &&...args) const;

	Error ioError(const QString &path) const;

	void checkSettings();
	QString computePath(Version version) const;
	QString binlogPath(Version version) const;
	QString binlogPath() const;
	QString compactReadyPath(Version version) const;
	QString compactReadyPath() const;
	Error openSomeBinlog(EncryptionKey &&key);
	Error openNewBinlog(EncryptionKey &key);
	File::Result openBinlog(
		Version version,
		File::Mode mode,
		EncryptionKey &key);
	bool readHeader();
	bool writeHeader();

	void readBinlog();
	template <typename Reader, typename ...Handlers>
	void readBinlogHelper(Reader &reader, Handlers &&...handlers);
	template <typename Record, typename Postprocess>
	bool processRecordStoreGeneric(
		const Record *record,
		Postprocess &&postprocess);
	bool processRecordStore(const Store *record, std::is_class<Store>);
	bool processRecordStore(
		const StoreWithTime *record,
		std::is_class<StoreWithTime>);
	template <typename Record, typename GetElement>
	bool processRecordMultiStore(
		const Record &header,
		const GetElement &element);
	template <typename GetElement>
	bool processRecordMultiRemove(
		const MultiRemove &header,
		const GetElement &element);
	template <typename GetElement>
	bool processRecordMultiAccess(
		const MultiAccess &header,
		const GetElement &element);

	void optimize();
	void checkCompactor();
	void adjustRelativeTime();
	bool startDelayedPruning();
	uint64 countRelativeTime() const;
	EstimatedTimePoint countTimePoint() const;
	void applyTimePoint(EstimatedTimePoint time);

	uint64 pruneBeforeTime() const;
	void prune();
	void collectTimeStale(
		base::flat_set<Key> &stale,
		int64 &staleTotalSize);
	void collectSizeStale(
		base::flat_set<Key> &stale,
		int64 &staleTotalSize);
	void startStaleClear();
	void clearStaleNow(const base::flat_set<Key> &stale);
	void clearStaleChunkDelayed();
	void clearStaleChunk();

	void updateStats(const Entry &was, const Entry &now);
	Stats collectStats() const;
	void pushStatsDelayed();
	void pushStats();

	void setMapEntry(const Key &key, Entry &&entry);
	void eraseMapEntry(const Map::const_iterator &i);
	void recordEntryAccess(const Key &key);
	QByteArray readValueData(PlaceId place, size_type size) const;

	Version findAvailableVersion() const;
	QString versionPath() const;
	bool writeVersion(Version version);
	Version readVersion() const;

	QString placePath(PlaceId place) const;
	bool isFreePlace(PlaceId place) const;

	template <typename StoreRecord>
	base::optional<QString> writeKeyPlaceGeneric(
		StoreRecord &&record,
		const Key &key,
		const TaggedValue &value,
		uint32 checksum);
	base::optional<QString> writeKeyPlace(
		const Key &key,
		const TaggedValue &value,
		uint32 checksum);
	template <typename StoreRecord>
	Error writeExistingPlaceGeneric(
		StoreRecord &&record,
		const Key &key,
		const Entry &entry);
	Error writeExistingPlace(
		const Key &key,
		const Entry &entry);
	void writeMultiRemoveLazy();
	Error writeMultiRemove();
	void writeMultiAccessLazy();
	Error writeMultiAccess();
	Error writeMultiAccessBlock();
	void writeBundlesLazy();
	void writeBundles();

	void createCleaner();
	void cleanerDone(Error error);
	void clearState();

	crl::weak_on_queue<DatabaseObject> _weak;
	QString _base, _path;
	Settings _settings;
	EncryptionKey _key;
	File _binlog;
	Map _map;
	std::set<Key> _removing;
	std::set<Key> _accessed;
	std::vector<Key> _stale;

	EstimatedTimePoint _time;

	int64 _binlogExcessLength = 0;
	int64 _totalSize = 0;
	uint64 _minimalEntryTime = 0;
	size_type _entriesWithMinimalTimeCount = 0;

	base::flat_map<uint8, TaggedSummary> _taggedStats;
	rpl::event_stream<Stats> _stats;
	bool _pushingStats = false;
	bool _clearingStale = false;

	base::ConcurrentTimer _writeBundlesTimer;
	base::ConcurrentTimer _pruneTimer;

	CleanerWrap _cleaner;
	CompactorWrap _compactor;

};

} // namespace details
} // namespace Cache
} // namespace Storage
