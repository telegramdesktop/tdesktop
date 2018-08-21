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

	void open(EncryptionKey key, FnMut<void(Error)> done);
	void close(FnMut<void()> done);

	void put(const Key &key, QByteArray value, FnMut<void(Error)> done);
	void get(const Key &key, FnMut<void(QByteArray)> done);
	void remove(const Key &key, FnMut<void()> done = nullptr);

	void clear(FnMut<void(Error)> done);

	~DatabaseObject();

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
		const QByteArray &value,
		uint32 checksum);
	base::optional<QString> writeKeyPlace(
		const Key &key,
		const QByteArray &value,
		uint32 checksum);
	void writeMultiRemoveLazy();
	void writeMultiRemove();
	void writeMultiAccessLazy();
	void writeMultiAccess();
	void writeMultiAccessBlock();
	void writeBundlesLazy();
	void writeBundles();

	void createCleaner();
	void cleanerDone(Error error);

	crl::weak_on_queue<DatabaseObject> _weak;
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

	int64 _binlogExcessLength = 0;
	int64 _totalSize = 0;
	int64 _minimalEntryTime = 0;
	size_type _entriesWithMinimalTimeCount = 0;

	base::ConcurrentTimer _writeBundlesTimer;
	base::ConcurrentTimer _pruneTimer;

	CleanerWrap _cleaner;
	std::unique_ptr<Compactor> _compactor;

};

} // namespace details
} // namespace Cache
} // namespace Storage
