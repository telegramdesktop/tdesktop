/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/cache/storage_cache_compactor.h"

#include "storage/cache/storage_cache_database_object.h"
#include "storage/cache/storage_cache_binlog_reader.h"
#include <unordered_set>

namespace Storage {
namespace Cache {
namespace details {

class CompactorObject {
public:
	using Info = Compactor::Info;

	CompactorObject(
		crl::weak_on_queue<CompactorObject> weak,
		crl::weak_on_queue<DatabaseObject> database,
		base::binary_guard guard,
		const QString &base,
		const Settings &settings,
		EncryptionKey &&key,
		const Info &info);

private:
	using Entry = DatabaseObject::Entry;
	using Raw = DatabaseObject::Raw;
	using RawSpan = gsl::span<const Raw>;
	static QString CompactFilename();

	void start();
	QString binlogPath() const;
	QString compactPath() const;
	bool openBinlog();
	bool readHeader();
	bool openCompact();
	void parseChunk();
	void fail();
	void done(int64 till);
	void finish();
	void finalize();

	std::vector<Key> readChunk();
	bool readBlock(std::vector<Key> &result);
	void processValues(const std::vector<Raw> &values);

	template <typename MultiRecord>
	void initList();
	RawSpan fillList(RawSpan values);
	template <typename RecordStore>
	RawSpan fillList(std::vector<RecordStore> &list, RawSpan values);
	template <typename RecordStore>
	void addListRecord(
		std::vector<RecordStore> &list,
		const Raw &raw);
	bool writeList();
	template <typename MultiRecord>
	bool writeMultiStore();

	crl::weak_on_queue<CompactorObject> _weak;
	crl::weak_on_queue<DatabaseObject> _database;
	base::binary_guard _guard;
	QString _base;
	Settings _settings;
	EncryptionKey _key;
	BasicHeader _header;
	Info _info;
	File _binlog;
	File _compact;
	BinlogWrapper _wrapper;
	size_type _partSize = 0;
	std::unordered_set<Key> _written;
	base::variant<
		std::vector<MultiStore::Part>,
		std::vector<MultiStoreWithTime::Part>> _list;

};

CompactorObject::CompactorObject(
	crl::weak_on_queue<CompactorObject> weak,
	crl::weak_on_queue<DatabaseObject> database,
	base::binary_guard guard,
	const QString &base,
	const Settings &settings,
	EncryptionKey &&key,
	const Info &info)
: _weak(std::move(weak))
, _database(std::move(database))
, _guard(std::move(guard))
, _base(base)
, _settings(settings)
, _key(std::move(key))
, _info(info)
, _wrapper(_binlog, _settings, _info.till)
, _partSize(_settings.maxBundledRecords) { // Perhaps a better estimate?
	Expects(_settings.compactChunkSize > 0);

	_written.reserve(_info.keysCount);
	start();
}

template <typename MultiRecord>
void CompactorObject::initList() {
	using Part = typename MultiRecord::Part;
	auto list = std::vector<Part>();
	list.reserve(_partSize);
	_list = std::move(list);
}

void CompactorObject::start() {
	if (!openBinlog() || !readHeader() || !openCompact()) {
		fail();
	}
	if (_settings.trackEstimatedTime) {
		initList<MultiStoreWithTime>();
	} else {
		initList<MultiStore>();
	}
	parseChunk();
}

QString CompactorObject::CompactFilename() {
	return QStringLiteral("binlog-temp");
}

QString CompactorObject::binlogPath() const {
	return _base + DatabaseObject::BinlogFilename();
}

QString CompactorObject::compactPath() const {
	return _base + CompactFilename();
}

bool CompactorObject::openBinlog() {
	const auto path = binlogPath();
	const auto result = _binlog.open(path, File::Mode::Read, _key);
	return (result == File::Result::Success)
		&& (_binlog.size() >= _info.till);
}

bool CompactorObject::readHeader() {
	const auto header = BinlogWrapper::ReadHeader(_binlog, _settings);
	if (!header) {
		return false;
	}
	_header = *header;
	return true;
}

bool CompactorObject::openCompact() {
	const auto path = compactPath();
	const auto result = _compact.open(path, File::Mode::Write, _key);
	if (result != File::Result::Success) {
		return false;
	} else if (!_compact.write(bytes::object_as_span(&_header))) {
		return false;
	}
	return true;
}

void CompactorObject::fail() {
	_compact.close();
	QFile(compactPath()).remove();
	_database.with([](DatabaseObject &database) {
		database.compactorFail();
	});
}

void CompactorObject::done(int64 till) {
	const auto path = compactPath();
	_database.with([=, good = std::move(_guard)](DatabaseObject &database) {
		if (good.alive()) {
			database.compactorDone(path, till);
		}
	});
}

void CompactorObject::finish() {
	if (writeList()) {
		finalize();
	} else {
		fail();
	}
}

void CompactorObject::finalize() {
	_binlog.close();
	_compact.close();

	auto lastCatchUp = 0;
	auto from = _info.till;
	while (true) {
		const auto till = CatchUp(
			compactPath(),
			binlogPath(),
			_key,
			from,
			_settings.readBlockSize);
		if (!till) {
			fail();
			return;
		} else if (till == from
			|| (lastCatchUp > 0 && (till - from) >= lastCatchUp)) {
			done(till);
			return;
		}
		lastCatchUp = (till - from);
		from = till;
	}
}

bool CompactorObject::writeList() {
	if (_list.is<std::vector<MultiStore::Part>>()) {
		return writeMultiStore<MultiStore>();
	} else if (_list.is<std::vector<MultiStoreWithTime::Part>>()) {
		return writeMultiStore<MultiStoreWithTime>();
	} else {
		Unexpected("List type in CompactorObject::writeList.");
	}
}

template <typename MultiRecord>
bool CompactorObject::writeMultiStore() {
	using Part = typename MultiRecord::Part;
	Assert(_list.is<std::vector<Part>>());
	auto &list = _list.get_unchecked<std::vector<Part>>();
	if (list.empty()) {
		return true;
	}
	const auto guard = gsl::finally([&] { list.clear(); });
	const auto size = list.size();
	auto header = MultiRecord(size);
	if (_compact.write(bytes::object_as_span(&header))
		&& _compact.write(bytes::make_span(list))) {
		_compact.flush();
		return true;
	}
	return false;
}

std::vector<Key> CompactorObject::readChunk() {
	const auto limit = _settings.compactChunkSize;
	auto result = std::vector<Key>();
	while (result.size() < limit) {
		if (!readBlock(result)) {
			break;
		}
	}
	return result;
}

bool CompactorObject::readBlock(std::vector<Key> &result) {
	const auto push = [&](const Store &store) {
		result.push_back(store.key);
		return true;
	};
	const auto pushMulti = [&](const auto &element) {
		while (const auto record = element()) {
			push(*record);
		}
		return true;
	};
	if (_settings.trackEstimatedTime) {
		BinlogReader<
			StoreWithTime,
			MultiStoreWithTime,
			MultiRemove,
			MultiAccess> reader(_wrapper);
		return !reader.readTillEnd([&](const StoreWithTime &record) {
			return push(record);
		}, [&](const MultiStoreWithTime &header, const auto &element) {
			return pushMulti(element);
		}, [&](const MultiRemove &header, const auto &element) {
			return true;
		}, [&](const MultiAccess &header, const auto &element) {
			return true;
		});
	} else {
		BinlogReader<
			Store,
			MultiStore,
			MultiRemove> reader(_wrapper);
		return !reader.readTillEnd([&](const Store &record) {
			return push(record);
		}, [&](const MultiStore &header, const auto &element) {
			return pushMulti(element);
		}, [&](const MultiRemove &header, const auto &element) {
			return true;
		});
	}
}

void CompactorObject::parseChunk() {
	auto keys = readChunk();
	if (_wrapper.failed()) {
		fail();
		return;
	} else if (keys.empty()) {
		finish();
		return;
	}
	_database.with([
		weak = _weak,
		keys = std::move(keys)
	](DatabaseObject &database) {
		auto result = database.getManyRaw(keys);
		weak.with([result = std::move(result)](CompactorObject &that) {
			that.processValues(result);
		});
	});
}

void CompactorObject::processValues(
		const std::vector<std::pair<Key, Entry>> &values) {
	auto left = gsl::make_span(values);
	while (true) {
		left = fillList(left);
		if (left.empty()) {
			break;
		} else if (!writeList()) {
			fail();
			return;
		}
	}
	parseChunk();
}

auto CompactorObject::fillList(RawSpan values) -> RawSpan {
	return _list.match([&](auto &list) {
		return fillList(list, values);
	});
}

template <typename RecordStore>
auto CompactorObject::fillList(
	std::vector<RecordStore> &list,
	RawSpan values
) -> RawSpan {
	const auto b = std::begin(values);
	const auto e = std::end(values);
	auto i = b;
	while (i != e && list.size() != _partSize) {
		addListRecord(list, *i++);
	}
	return values.subspan(i - b);
}

template <typename RecordStore>
void CompactorObject::addListRecord(
		std::vector<RecordStore> &list,
		const Raw &raw) {
	if (!_written.emplace(raw.first).second) {
		return;
	}
	auto record = RecordStore();
	record.key = raw.first;
	record.setSize(raw.second.size);
	record.checksum = raw.second.checksum;
	record.tag = raw.second.tag;
	record.place = raw.second.place;
	if constexpr (std::is_same_v<RecordStore, StoreWithTime>) {
		record.time.setRelative(raw.second.useTime);
		record.time.system = _info.systemTime;
	}
	list.push_back(record);
}

Compactor::Compactor(
	crl::weak_on_queue<DatabaseObject> database,
	base::binary_guard guard,
	const QString &base,
	const Settings &settings,
	EncryptionKey &&key,
	const Info &info)
: _wrapped(
	std::move(database),
	std::move(guard),
	base,
	settings,
	std::move(key),
	info) {
}

Compactor::~Compactor() = default;

int64 CatchUp(
		const QString &compactPath,
		const QString &binlogPath,
		const EncryptionKey &key,
		int64 from,
		size_type block) {
	File binlog, compact;
	const auto result1 = binlog.open(binlogPath, File::Mode::Read, key);
	if (result1 != File::Result::Success) {
		return 0;
	}
	const auto till = binlog.size();
	if (till == from) {
		return till;
	} else if (till < from || !binlog.seek(from)) {
		return 0;
	}
	const auto result2 = compact.open(
		compactPath,
		File::Mode::ReadAppend,
		key);
	if (result2 != File::Result::Success || !compact.seek(compact.size())) {
		return 0;
	}
	auto buffer = bytes::vector(block);
	auto bytes = bytes::make_span(buffer);
	do {
		const auto left = (till - from);
		const auto limit = std::min(size_type(left), block);
		const auto read = binlog.read(bytes.subspan(0, limit));
		if (!read || read > limit) {
			return 0;
		} else if (!compact.write(bytes.subspan(0, read))) {
			return 0;
		}
		from += read;
	} while (from != till);
	return till;
}

} // namespace details
} // namespace Cache
} // namespace Storage
