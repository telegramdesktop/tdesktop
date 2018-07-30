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
#include "base/algorithm.h"
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

constexpr auto kMaxBundledRecords = 256 * 1024;
constexpr auto kReadBlockSize = 8 * 1024 * 1024;
constexpr auto kRecordSizeUnknown = size_type(-1);
constexpr auto kRecordSizeInvalid = size_type(-2);
constexpr auto kMaxDataSize = 10 * 1024 * 1024;

using RecordType = uint8;
using PlaceId = std::array<uint8, 7>;
using EntrySize = std::array<uint8, 3>;
using RecordsCount = std::array<uint8, 3>;

static_assert(kMaxBundledRecords < (1 << (RecordsCount().size() * 8)));
static_assert(kMaxDataSize < (1 << (EntrySize().size() * 8)));

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
		if (i == 1) {
			result.push_back('/');
		}
	}
	return result;
}

struct Store {
	static constexpr auto kType = RecordType(0x01);

	RecordType type = kType;
	uint8 tag = 0;
	PlaceId place = { { 0 } };
	EntrySize size = { { 0 } };
	uint32 checksum = 0;
	Key key;
};
static_assert(sizeof(Store) == 1 + 7 + 1 + 3 + 4 + 16);

struct MultiStoreHeader {
	static constexpr auto kType = RecordType(0x02);

	explicit MultiStoreHeader(size_type count = 0);

	RecordType type = kType;
	RecordsCount count = { { 0 } };
};
struct MultiStorePart {
	uint8 reserved = 0;
	PlaceId place = { { 0 } };
	uint8 tag = 0;
	EntrySize size = { { 0 } };
	uint32 checksum = 0;
	Key key;
};
static_assert(sizeof(MultiStoreHeader) == 4);
static_assert(sizeof(MultiStorePart) == sizeof(Store));

MultiStoreHeader::MultiStoreHeader(size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count)) {
	Expects(count >= 0 && count < kMaxBundledRecords);
}

struct MultiRemoveHeader {
	static constexpr auto kType = RecordType(0x03);

	explicit MultiRemoveHeader(size_type count = 0);

	RecordType type = kType;
	RecordsCount count = { { 0 } };
};
struct MultiRemovePart {
	Key key;
};
static_assert(sizeof(MultiRemoveHeader) == 4);
static_assert(sizeof(MultiRemovePart) == 16);

MultiRemoveHeader::MultiRemoveHeader(size_type count)
: type(kType)
, count(ReadTo<RecordsCount>(count)) {
	Expects(count >= 0 && count < kMaxBundledRecords);
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
	void remove(const Key &key, FnMut<void()> done);

	void clear(FnMut<void(Error)> done);

private:
	struct Entry {
		Entry() = default;
		Entry(PlaceId place, uint8 tag, uint32 checksum, size_type size);

		uint64 tag = 0;
		uint32 checksum = 0;
		size_type size = 0;
		PlaceId place = { { 0 } };
	};
	struct CleanerWrap {
		std::unique_ptr<Cleaner> object;
		base::binary_guard guard;
	};

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
	void readBinlog();
	size_type readBinlogRecords(bytes::const_span data);
	size_type readBinlogRecordSize(bytes::const_span data) const;
	bool readBinlogRecord(bytes::const_span data);
	bool readRecordStore(bytes::const_span data);
	bool readRecordMultiStore(bytes::const_span data);
	bool readRecordMultiRemove(bytes::const_span data);

	Version findAvailableVersion() const;
	QString versionPath() const;
	bool writeVersion(Version version);
	Version readVersion() const;

	QString placePath(PlaceId place) const;
	bool isFreePlace(PlaceId place) const;
	QString writeKeyPlace(const Key &key, size_type size, uint32 checksum);
	void writeMultiRemove();

	void createCleaner();
	void cleanerDone(Error error);

	crl::weak_on_queue<Database> _weak;
	QString _base, _path;
	Settings _settings;
	EncryptionKey _key;
	File _binlog;
	std::unordered_map<Key, Entry> _map;
	std::set<Key> _removing;

	CleanerWrap _cleaner;

};

Database::Entry::Entry(
	PlaceId place,
	uint8 tag,
	uint32 checksum,
	size_type size)
: tag(tag)
, checksum(checksum)
, place(place)
, size(size) {
}

Database::Database(
	crl::weak_on_queue<Database> weak,
	const QString &path,
	const Settings &settings)
: _weak(std::move(weak))
, _base(ComputeBasePath(path))
, _settings(settings) {
}

template <typename Callback, typename ...Args>
void Database::invokeCallback(Callback &&callback, Args &&...args) {
	if (callback) {
		callback(std::move(args)...);
		//crl::on_main([
		//	callback = std::move(callback),
		//	args = std::forward<Args>(args)...
		//]() mutable {
		//	callback(std::move(args)...);
		//});
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
		_path = computePath(version);
		_key = std::move(key);
		createCleaner();
		readBinlog();
	}
	return result;
}

void Database::readBinlog() {
	auto data = bytes::vector(kReadBlockSize);
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
		return sizeof(Store);

	case MultiStoreHeader::kType:
		if (data.size() >= sizeof(MultiStoreHeader)) {
			const auto header = reinterpret_cast<const MultiStoreHeader*>(
				data.data());
			const auto count = ReadFrom(header->count);
			return (count > 0 && count < kMaxBundledRecords)
				? (sizeof(MultiStoreHeader)
					+ count * sizeof(MultiStorePart))
				: kRecordSizeInvalid;
		}
		return kRecordSizeUnknown;

	case MultiRemoveHeader::kType:
		if (data.size() >= sizeof(MultiRemoveHeader)) {
			const auto header = reinterpret_cast<const MultiRemoveHeader*>(
				data.data());
			const auto count = ReadFrom(header->count);
			return (count > 0 && count < kMaxBundledRecords)
				? (sizeof(MultiRemoveHeader)
					+ count * sizeof(MultiRemovePart))
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

	}
	Unexpected("Bad type in Database::readBinlogRecord.");
}

bool Database::readRecordStore(bytes::const_span data) {
	const auto record = reinterpret_cast<const Store*>(data.data());
	const auto size = ReadFrom(record->size);
	if (size > kMaxDataSize) {
		return false;
	}
	_map[record->key] = Entry(
		record->place,
		record->tag,
		record->checksum,
		size);
	return true;
}

bool Database::readRecordMultiStore(bytes::const_span data) {
	const auto bytes = data.data();
	const auto record = reinterpret_cast<const MultiStoreHeader*>(bytes);
	const auto count = ReadFrom(record->count);
	Assert(data.size() >= sizeof(MultiStoreHeader)
		+ count * sizeof(MultiStorePart));
	const auto parts = gsl::make_span(
		reinterpret_cast<const MultiStorePart*>(
			bytes + sizeof(MultiStoreHeader)),
		count);
	for (const auto &part : parts) {
		const auto size = ReadFrom(part.size);
		if (part.reserved != 0 || size > kMaxDataSize) {
			return false;
		}
		_map[part.key] = Entry(part.place, part.tag, part.checksum, size);
	}
	return true;
}

bool Database::readRecordMultiRemove(bytes::const_span data) {
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
		_map.erase(part.key);
	}
	return true;
}

void Database::close(FnMut<void()> done) {
	_cleaner = CleanerWrap();
	_binlog.close();
	invokeCallback(done);
}

void Database::put(
		const Key &key,
		QByteArray value,
		FnMut<void(Error)> done) {
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
		}
	} break;

	default: Unexpected("Result in Database::put.");
	}
}

QString Database::writeKeyPlace(
		const Key &key,
		size_type size,
		uint32 checksum) {
	Expects(size <= kMaxDataSize);

	auto record = Store();
	record.key = key;
	record.size = ReadTo<EntrySize>(size);
	record.checksum = checksum;
	do {
		bytes::set_random(bytes::object_as_span(&record.place));
	} while (!isFreePlace(record.place));

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

void Database::get(const Key &key, FnMut<void(QByteArray)> done) {
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
		}
	} break;

	default: Unexpected("Result in Database::get.");
	}
}

void Database::remove(const Key &key, FnMut<void()> done) {
	const auto i = _map.find(key);
	if (i != _map.end()) {
		_removing.emplace(key);
		if (true || _removing.size() == kMaxBundledRecords) {
			writeMultiRemove();
			// cancel timeout?..
		} else {
			// timeout?..
		}

		const auto &entry = i->second;
		const auto path = placePath(entry.place);
		_map.erase(i);
		QFile(path).remove();
	}
	invokeCallback(done);
}

void Database::writeMultiRemove() {
	Expects(_removing.size() <= kMaxBundledRecords);

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
