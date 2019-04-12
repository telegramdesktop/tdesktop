/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_reader.h"

#include "media/streaming/media_streaming_common.h"
#include "media/streaming/media_streaming_loader.h"
#include "storage/cache/storage_cache_database.h"
#include "data/data_session.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kPartSize = Loader::kPartSize;
constexpr auto kPartsInSlice = 64;
constexpr auto kInSlice = kPartsInSlice * kPartSize;
constexpr auto kMaxPartsInHeader = 64;
constexpr auto kMaxOnlyInHeader = 80 * kPartSize;
constexpr auto kPartsOutsideFirstSliceGood = 8;
constexpr auto kSlicesInMemory = 2;

// 1 MB of header parts can be outside the first slice for us to still
// put the whole first slice of the file in the header cache entry.
//constexpr auto kMaxOutsideHeaderPartsForOptimizedMode = 8;

// 1 MB of parts are requested from cloud ahead of reading demand.
constexpr auto kPreloadPartsAhead = 8;

bool IsContiguousSerialization(int serializedSize, int maxSliceSize) {
	return !(serializedSize % kPartSize) || (serializedSize == maxSliceSize);
}

template <typename Range> // Range::value_type is Pair<int, QByteArray>
int FindNotLoadedStart(Range &&parts, int offset) {
	auto result = offset;
	for (const auto &part : parts) {
		const auto partStart = part.first;
		const auto partEnd = partStart + part.second.size();
		if (partStart <= result && partEnd >= result) {
			result = partEnd;
		} else {
			break;
		}
	}
	return result;
}

template <typename Range> // Range::value_type is Pair<int, QByteArray>
void CopyLoaded(bytes::span buffer, Range &&parts, int offset, int till) {
	auto filled = offset;
	for (const auto &part : parts) {
		const auto bytes = bytes::make_span(part.second);
		const auto partStart = part.first;
		const auto partEnd = int(partStart + bytes.size());
		const auto copyTill = std::min(partEnd, till);
		Assert(partStart <= filled && filled < copyTill);

		const auto from = filled - partStart;
		const auto copy = copyTill - filled;
		bytes::copy(buffer, bytes.subspan(from, copy));
		buffer = buffer.subspan(copy);
		filled += copy;
	}
}

} // namespace

template <int Size>
bool Reader::StackIntVector<Size>::add(int value) {
	using namespace rpl::mappers;

	const auto i = ranges::find_if(_storage, _1 < 0);
	if (i == end(_storage)) {
		return false;
	}
	*i = value;
	const auto next = i + 1;
	if (next != end(_storage)) {
		*next = -1;
	}
	return true;
}

template <int Size>
auto Reader::StackIntVector<Size>::values() const {
	using namespace rpl::mappers;

	return ranges::view::all(_storage) | ranges::view::take_while(_1 >= 0);
}

struct Reader::CacheHelper {
	explicit CacheHelper(Storage::Cache::Key baseKey);

	Storage::Cache::Key key(int sliceNumber) const;

	const Storage::Cache::Key baseKey;

	QMutex mutex;
	base::flat_map<int, QByteArray> results;
	std::atomic<crl::semaphore*> waiting = nullptr;
};

Reader::CacheHelper::CacheHelper(Storage::Cache::Key baseKey)
: baseKey(baseKey) {
}

Storage::Cache::Key Reader::CacheHelper::key(int sliceNumber) const {
	return Storage::Cache::Key{ baseKey.high, baseKey.low + sliceNumber };
}

bytes::const_span Reader::Slice::processCacheData(
		bytes::const_span data,
		int maxSize) {
	Expects((flags & Flag::LoadingFromCache) != 0);
	Expects(!(flags & Flag::LoadedFromCache));

	const auto guard = gsl::finally([&] {
		flags |= Flag::LoadedFromCache;
		flags &= ~Flag::LoadingFromCache;
	});

	const auto size = int(data.size());
	if (IsContiguousSerialization(size, maxSize)) {
		if (size > maxSize) {
			return {};
		}
		for (auto offset = 0; offset < size; offset += kPartSize) {
			const auto part = data.subspan(
				offset,
				std::min(kPartSize, size - offset));
			parts.try_emplace(
				offset,
				reinterpret_cast<const char*>(part.data()),
				part.size());
		}
		return {};
	}
	return processComplexCacheData(bytes::make_span(data), maxSize);
}

bytes::const_span Reader::Slice::processComplexCacheData(
		bytes::const_span data,
		int maxSize) {
	const auto takeInt = [&]() -> std::optional<int> {
		if (data.size() < sizeof(int32)) {
			return std::nullopt;
		}
		const auto bytes = data.data();
		const auto result = *reinterpret_cast<const int32*>(bytes);
		data = data.subspan(sizeof(int32));
		return result;
	};
	const auto takeBytes = [&](int count) {
		if (count <= 0 || data.size() < count) {
			return bytes::const_span();
		}
		const auto result = data.subspan(0, count);
		data = data.subspan(count);
		return result;
	};
	const auto maybeCount = takeInt();
	if (!maybeCount) {
		return {};
	}
	const auto count = *maybeCount;
	if (count < 0) {
		return {};
	} else if (!count) {
		return data;
	}
	for (auto i = 0; i != count; ++i) {
		const auto offset = takeInt().value_or(0);
		const auto size = takeInt().value_or(0);
		const auto bytes = takeBytes(size);
		if (offset < 0
			|| offset >= maxSize
			|| size <= 0
			|| size > maxSize
			|| offset + size > maxSize
			|| bytes.size() != size) {
			return {};
		}
		parts.try_emplace(
			offset,
			reinterpret_cast<const char*>(bytes.data()),
			bytes.size());
	}
	return data;
}

void Reader::Slice::addPart(int offset, QByteArray bytes) {
	Expects(!parts.contains(offset));

	parts.emplace(offset, std::move(bytes));
	if (flags & Flag::LoadedFromCache) {
		flags |= Flag::ChangedSinceCache;
	}
}

auto Reader::Slice::prepareFill(int from, int till) -> PrepareFillResult {
	auto result = PrepareFillResult();

	result.ready = false;
	const auto fromOffset = (from / kPartSize) * kPartSize;
	const auto tillPart = (till + kPartSize - 1) / kPartSize;
	const auto preloadTillOffset = (tillPart + kPreloadPartsAhead)
		* kPartSize;

	const auto after = ranges::upper_bound(
		parts,
		from,
		ranges::less(),
		&base::flat_map<int, QByteArray>::value_type::first);
	if (after == begin(parts)) {
		result.offsetsFromLoader = offsetsFromLoader(
			fromOffset,
			preloadTillOffset);
		return result;
	}

	const auto start = after - 1;
	const auto finish = ranges::lower_bound(
		start,
		end(parts),
		till,
		ranges::less(),
		&base::flat_map<int, QByteArray>::value_type::first);
	const auto haveTill = FindNotLoadedStart(
		ranges::make_iterator_range(start, finish),
		fromOffset);
	if (haveTill < till) {
		result.offsetsFromLoader = offsetsFromLoader(
			haveTill,
			preloadTillOffset);
		return result;
	}
	result.ready = true;
	result.start = start;
	result.finish = finish;
	result.offsetsFromLoader = offsetsFromLoader(
		tillPart * kPartSize,
		preloadTillOffset);
	return result;
}

auto Reader::Slice::offsetsFromLoader(int from, int till) const
-> StackIntVector<Reader::kLoadFromRemoteMax> {
	auto result = StackIntVector<kLoadFromRemoteMax>();

	const auto after = ranges::upper_bound(
		parts,
		from,
		ranges::less(),
		&base::flat_map<int, QByteArray>::value_type::first);
	auto check = (after == begin(parts)) ? after : (after - 1);
	const auto end = parts.end();
	for (auto offset = from; offset != till; offset += kPartSize) {
		while (check != end && check->first < offset) {
			++check;
		}
		if (check != end && check->first == offset) {
			continue;
		} else if (!result.add(offset)) {
			break;
		}
	}
	return result;
}

Reader::Slices::Slices(int size, bool useCache)
: _size(size) {
	Expects(size > 0);

	if (useCache) {
		_header.flags |= Slice::Flag::LoadingFromCache;
	} else {
		_headerMode = HeaderMode::NoCache;
	}
	if (!isFullInHeader()) {
		_data.resize((_size + kInSlice - 1) / kInSlice);
	}
}

bool Reader::Slices::headerModeUnknown() const {
	return (_headerMode == HeaderMode::Unknown);
}

bool Reader::Slices::isFullInHeader() const {
	return (_size <= kMaxOnlyInHeader);
}

bool Reader::Slices::isGoodHeader() const {
	return (_headerMode == HeaderMode::Good);
}

bool Reader::Slices::computeIsGoodHeader() const {
	if (isFullInHeader()) {
		return false;
	}
	const auto outsideFirstSliceIt = ranges::lower_bound(
		_header.parts,
		kInSlice,
		ranges::less(),
		&base::flat_map<int, QByteArray>::value_type::first);
	const auto outsideFirstSlice = end(_header.parts) - outsideFirstSliceIt;
	return (outsideFirstSlice <= kPartsOutsideFirstSliceGood);
}

void Reader::Slices::headerDone(bool fromCache) {
	if (_headerMode != HeaderMode::Unknown) {
		return;
	}
	_headerMode = isFullInHeader()
		? HeaderMode::Full
		: computeIsGoodHeader()
		? HeaderMode::Good
		: HeaderMode::Small;
	if (!fromCache) {
		for (auto &slice : _data) {
			using Flag = Slice::Flag;
			Assert(!(slice.flags
				& (Flag::LoadingFromCache | Flag::LoadedFromCache)));
			slice.flags |= Slice::Flag::LoadedFromCache;
		}
	}
}

bool Reader::Slices::headerWontBeFilled() const {
	return headerModeUnknown()
		&& (_header.parts.size() >= kMaxPartsInHeader);
}

void Reader::Slices::applyHeaderCacheData() {
	using namespace rpl::mappers;

	const auto applyWhile = [&](auto &&predicate) {
		for (const auto &[offset, part] : _header.parts) {
			const auto index = offset / kInSlice;
			if (!predicate(index)) {
				break;
			}
			_data[index].addPart(
				offset - index * kInSlice,
				base::duplicate(part));
		}
	};
	if (_header.parts.empty()) {
		return;
	} else if (_headerMode == HeaderMode::Good) {
		// Always apply data to first block if it is cached in the header.
		applyWhile(_1 == 0);
	} else if (_headerMode != HeaderMode::Unknown) {
		return;
	} else if (isFullInHeader()) {
		headerDone(true);
	} else {
		applyWhile(_1 < int(_data.size()));
		headerDone(true);
	}
}

void Reader::Slices::processCacheResult(
		int sliceNumber,
		bytes::const_span result) {
	Expects(sliceNumber >= 0 && sliceNumber <= _data.size());

	auto &slice = (sliceNumber ? _data[sliceNumber - 1] : _header);
	if (!sliceNumber && isGoodHeader()) {
		// We've loaded header slice because really we wanted first slice.
		if (!(_data[0].flags & Slice::Flag::LoadingFromCache)) {
			// We could've already unloaded this slice using LRU _usedSlices.
			return;
		}
		// So just process whole result even if we didn't want header really.
		slice.flags |= Slice::Flag::LoadingFromCache;
		slice.flags &= ~Slice::Flag::LoadedFromCache;
	}
	if (!(slice.flags & Slice::Flag::LoadingFromCache)) {
		// We could've already unloaded this slice using LRU _usedSlices.
		return;
	}
	const auto remaining = slice.processCacheData(
		result,
		maxSliceSize(sliceNumber));
	if (!sliceNumber) {
		applyHeaderCacheData();
		if (isGoodHeader()) {
			// When we first read header we don't request the first slice.
			// But we get it, so let's apply it anyway.
			_data[0].flags |= Slice::Flag::LoadingFromCache;
			processCacheResult(1, remaining);
		}
	}
}

void Reader::Slices::processPart(
		int offset,
		QByteArray &&bytes) {
	Expects(isFullInHeader() || (offset / kInSlice < _data.size()));

	if (isFullInHeader()) {
		_header.addPart(offset, bytes);
		return;
	} else if (_headerMode == HeaderMode::Unknown) {
		if (_header.parts.contains(offset)) {
			return;
		} else if (_header.parts.size() < kMaxPartsInHeader) {
			_header.addPart(offset, bytes);
		}
	}
	const auto index = offset / kInSlice;
	_data[index].addPart(offset - index * kInSlice, std::move(bytes));
}

auto Reader::Slices::fill(int offset, bytes::span buffer) -> FillResult {
	Expects(!buffer.empty());
	Expects(offset >= 0 && offset < _size);
	Expects(offset + buffer.size() <= _size);
	Expects(buffer.size() <= kInSlice);

	using Flag = Slice::Flag;

	if (_headerMode != HeaderMode::NoCache
		&& !(_header.flags & Flag::LoadedFromCache)) {
		// Waiting for initial cache query.
		Assert(_header.flags & Flag::LoadingFromCache);
		return {};
	} else if (isFullInHeader()) {
		return fillFromHeader(offset, buffer);
	}

	auto result = FillResult();
	const auto till = int(offset + buffer.size());
	const auto fromSlice = offset / kInSlice;
	const auto tillSlice = (till + kInSlice - 1) / kInSlice;
	Assert(fromSlice >= 0
		&& (fromSlice + 1 == tillSlice || fromSlice + 2 == tillSlice)
		&& tillSlice <= _data.size());

	const auto cacheNotLoaded = [&](int sliceIndex) {
		return (_headerMode != HeaderMode::NoCache)
			&& (_headerMode != HeaderMode::Unknown)
			&& !(_data[sliceIndex].flags & Flag::LoadedFromCache);
	};
	const auto handlePrepareResult = [&](
			int sliceIndex,
			const Slice::PrepareFillResult &prepared) {
		if (cacheNotLoaded(sliceIndex)) {
			return;
		}
		for (const auto offset : prepared.offsetsFromLoader.values()) {
			const auto full = offset + sliceIndex * kInSlice;
			if (offset < kInSlice && full < _size) {
				result.offsetsFromLoader.add(full);
			}
		}
	};
	const auto handleReadFromCache = [&](int sliceIndex) {
		if (cacheNotLoaded(sliceIndex)
			&& !(_data[sliceIndex].flags & Flag::LoadingFromCache)) {
			_data[sliceIndex].flags |= Flag::LoadingFromCache;
			result.sliceNumbersFromCache.add(sliceIndex + 1);
		}
	};
	const auto firstFrom = offset - fromSlice * kInSlice;
	const auto firstTill = std::min(kInSlice, till - fromSlice * kInSlice);
	const auto secondFrom = 0;
	const auto secondTill = till - (fromSlice + 1) * kInSlice;
	const auto first = _data[fromSlice].prepareFill(firstFrom, firstTill);
	const auto second = (fromSlice + 1 < tillSlice)
		? _data[fromSlice + 1].prepareFill(secondFrom, secondTill)
		: Slice::PrepareFillResult();
	handlePrepareResult(fromSlice, first);
	if (fromSlice + 1 < tillSlice) {
		handlePrepareResult(fromSlice + 1, second);
	}
	if (first.ready && second.ready) {
		markSliceUsed(fromSlice);
		CopyLoaded(
			buffer,
			ranges::make_iterator_range(first.start, first.finish),
			firstFrom,
			firstTill);
		if (fromSlice + 1 < tillSlice) {
			markSliceUsed(fromSlice + 1);
			CopyLoaded(
				buffer.subspan(firstTill - firstFrom),
				ranges::make_iterator_range(second.start, second.finish),
				secondFrom,
				secondTill);
		}
		result.toCache = serializeAndUnloadUnused();
		result.filled = true;
	} else {
		handleReadFromCache(fromSlice);
		if (fromSlice + 1 < tillSlice) {
			handleReadFromCache(fromSlice + 1);
		}
	}
	return result;
}

auto Reader::Slices::fillFromHeader(int offset, bytes::span buffer)
-> FillResult {
	auto result = FillResult();
	const auto from = offset;
	const auto till = int(offset + buffer.size());

	const auto prepared = _header.prepareFill(from, till);
	for (const auto full : prepared.offsetsFromLoader.values()) {
		if (full < _size) {
			result.offsetsFromLoader.add(full);
		}
	}
	if (prepared.ready) {
		CopyLoaded(
			buffer,
			ranges::make_iterator_range(prepared.start, prepared.finish),
			from,
			till);
		result.filled = true;
	}
	return result;
}

void Reader::Slices::markSliceUsed(int sliceIndex) {
	const auto i = ranges::find(_usedSlices, sliceIndex);
	const auto end = _usedSlices.end();
	if (i == end) {
		_usedSlices.push_back(sliceIndex);
	} else {
		const auto next = i + 1;
		if (next != end) {
			std::rotate(i, next, end);
		}
	}
}

int Reader::Slices::maxSliceSize(int sliceNumber) const {
	return !sliceNumber
		? _size
		: (sliceNumber == _data.size())
		? (_size - (sliceNumber - 1) * kInSlice)
		: kInSlice;
}

Reader::SerializedSlice Reader::Slices::serializeAndUnloadUnused() {
	using Flag = Slice::Flag;

	if (_headerMode == HeaderMode::Unknown
		|| _usedSlices.size() <= kSlicesInMemory) {
		return {};
	}
	const auto purgeSlice = _usedSlices.front();
	_usedSlices.pop_front();
	if (!(_data[purgeSlice].flags & Flag::LoadedFromCache)) {
		// If the only data in this slice was from _header, just leave it.
		return {};
	}
	const auto noNeedToSaveToCache = [&] {
		if (_headerMode == HeaderMode::NoCache) {
			// Cache is not used.
			return true;
		} else if (!(_data[purgeSlice].flags & Flag::ChangedSinceCache)) {
			// If no data was changed we should still save first slice,
			// if header data was changed since loading from cache.
			// Otherwise in destructor we won't be able to unload header.
			if (!isGoodHeader()
				|| (purgeSlice > 0)
				|| (!(_header.flags & Flag::ChangedSinceCache))) {
				return true;
			}
		}
		return false;
	}();
	if (noNeedToSaveToCache) {
		_data[purgeSlice] = Slice();
		return {};
	}
	return serializeAndUnloadSlice(purgeSlice + 1);
}

Reader::SerializedSlice Reader::Slices::serializeAndUnloadSlice(
		int sliceNumber) {
	Expects(_headerMode != HeaderMode::Unknown);
	Expects(_headerMode != HeaderMode::NoCache);
	Expects(sliceNumber >= 0 && sliceNumber <= _data.size());

	if (isGoodHeader() && (sliceNumber == 1)) {
		return serializeAndUnloadSlice(0);
	}
	const auto writeHeaderAndSlice = isGoodHeader() && !sliceNumber;

	auto &slice = sliceNumber ? _data[sliceNumber - 1] : _header;
	const auto count = slice.parts.size();
	Assert(count > 0);

	auto result = SerializedSlice();
	result.number = sliceNumber;

	// We always use complex serialization for header + first slice.
	const auto continuousTill = writeHeaderAndSlice
		? 0
		: FindNotLoadedStart(slice.parts, 0);
	const auto continuous = (continuousTill > slice.parts.back().first);
	if (continuous) {
		// All data is continuous.
		result.data.reserve(count * kPartSize);
		for (const auto &[offset, part] : slice.parts) {
			result.data.append(part);
		}
	} else {
		result.data = serializeComplexSlice(slice);
		if (writeHeaderAndSlice) {
			result.data.append(serializeAndUnloadFirstSliceNoHeader());
		}

		// Make sure this data won't be taken for full continuous data.
		const auto maxSize = maxSliceSize(sliceNumber);
		while (IsContiguousSerialization(result.data.size(), maxSize)) {
			result.data.push_back(char(0));
		}
	}

	// We may serialize header in the middle of streaming, if we use
	// HeaderMode::Good and we unload first slice. We still require
	// header data to continue working, so don't really unload the header.
	if (sliceNumber) {
		slice = Slice();
	} else {
		slice.flags &= ~Slice::Flag::ChangedSinceCache;
	}
	return result;
}

QByteArray Reader::Slices::serializeComplexSlice(const Slice &slice) const {
	auto result = QByteArray();
	const auto count = slice.parts.size();
	const auto intSize = sizeof(int32);
	result.reserve(count * kPartSize + 2 * intSize * (count + 1));
	const auto appendInt = [&](int value) {
		auto serialized = int32(value);
		result.append(
			reinterpret_cast<const char*>(&serialized),
			intSize);
	};
	appendInt(count);
	for (const auto &[offset, part] : slice.parts) {
		appendInt(offset);
		appendInt(part.size());
		result.append(part);
	}
	return result;
}

QByteArray Reader::Slices::serializeAndUnloadFirstSliceNoHeader() {
	Expects(_data[0].flags & Slice::Flag::LoadedFromCache);

	auto &slice = _data[0];
	for (const auto &[offset, part] : _header.parts) {
		slice.parts.erase(offset);
	}
	auto result = serializeComplexSlice(slice);
	slice = Slice();
	return result;
}

Reader::SerializedSlice Reader::Slices::unloadToCache() {
	if (_headerMode == HeaderMode::Unknown
		|| _headerMode == HeaderMode::NoCache) {
		return {};
	}
	if (_header.flags & Slice::Flag::ChangedSinceCache) {
		return serializeAndUnloadSlice(0);
	}
	for (auto i = 0, count = int(_data.size()); i != count; ++i) {
		if (_data[i].flags & Slice::Flag::ChangedSinceCache) {
			return serializeAndUnloadSlice(i + 1);
		}
	}
	return {};
}

Reader::Reader(
	not_null<Data::Session*> owner,
	std::unique_ptr<Loader> loader)
: _owner(owner)
, _loader(std::move(loader))
, _cacheHelper(InitCacheHelper(_loader->baseCacheKey()))
, _slices(_loader->size(), _cacheHelper != nullptr) {
	_loader->parts(
	) | rpl::start_with_next([=](LoadedPart &&part) {
		QMutexLocker lock(&_loadedPartsMutex);
		_loadedParts.push_back(std::move(part));
		lock.unlock();

		if (const auto waiting = _waiting.load()) {
			_waiting = nullptr;
			waiting->release();
		}
	}, _lifetime);

	if (_cacheHelper) {
		readFromCache(0);
	}
}

void Reader::stop() {
	_waiting = nullptr;
}

bool Reader::isRemoteLoader() const {
	return _loader->baseCacheKey().has_value();
}

std::shared_ptr<Reader::CacheHelper> Reader::InitCacheHelper(
		std::optional<Storage::Cache::Key> baseKey) {
	if (!baseKey) {
		return nullptr;
	}
	return std::make_shared<Reader::CacheHelper>(*baseKey);
}

// 0 is for headerData, slice index = sliceNumber - 1.
void Reader::readFromCache(int sliceNumber) {
	Expects(_cacheHelper != nullptr);
	Expects(!sliceNumber || !_slices.headerModeUnknown());

	if (sliceNumber == 1 && _slices.isGoodHeader()) {
		return readFromCache(0);
	}
	const auto key = _cacheHelper->key(sliceNumber);
	const auto weak = std::weak_ptr<CacheHelper>(_cacheHelper);
	_owner->cacheBigFile().get(key, [=](QByteArray &&result) {
		if (const auto strong = weak.lock()) {
			QMutexLocker lock(&strong->mutex);
			strong->results.emplace(sliceNumber, std::move(result));
			if (const auto waiting = strong->waiting.load()) {
				strong->waiting = nullptr;
				waiting->release();
			}
		}
	});
}

void Reader::putToCache(SerializedSlice &&slice) {
	Expects(_cacheHelper != nullptr);
	Expects(slice.number >= 0);

	_owner->cacheBigFile().put(
		_cacheHelper->key(slice.number),
		std::move(slice.data));
}

int Reader::size() const {
	return _loader->size();
}

std::optional<Error> Reader::failed() const {
	return _failed;
}

void Reader::headerDone() {
	_slices.headerDone(false);
}

bool Reader::fill(
		int offset,
		bytes::span buffer,
		not_null<crl::semaphore*> notify) {
	Expects(offset + buffer.size() <= size());

	const auto startWaiting = [&] {
		if (_cacheHelper) {
			_cacheHelper->waiting = notify.get();
		}
		_waiting = notify.get();
	};
	const auto clearWaiting = [&] {
		_waiting = nullptr;
		if (_cacheHelper) {
			_cacheHelper->waiting = nullptr;
		}
	};
	const auto done = [&] {
		clearWaiting();
		return true;
	};
	const auto failed = [&] {
		clearWaiting();
		notify->release();
		return false;
	};

	processCacheResults();
	processLoadedParts();
	if (_failed) {
		return failed();
	}

	do {
		if (fillFromSlices(offset, buffer)) {
			clearWaiting();
			return true;
		}
		startWaiting();
	} while (processCacheResults() || processLoadedParts());

	return _failed ? failed() : false;
}

bool Reader::fillFromSlices(int offset, bytes::span buffer) {
	using namespace rpl::mappers;

	auto result = _slices.fill(offset, buffer);
	if (!result.filled && _slices.headerWontBeFilled()) {
		_failed = Error::NotStreamable;
		return false;
	}

	for (const auto sliceNumber : result.sliceNumbersFromCache.values()) {
		readFromCache(sliceNumber);
	}

	if (_cacheHelper && result.toCache.number >= 0) {
		// If we put to cache the header (number == 0) that means we're in
		// HeaderMode::Good and really are putting the first slice to cache.
		Assert(result.toCache.number > 0 || _slices.isGoodHeader());

		const auto index = std::max(result.toCache.number, 1) - 1;
		cancelLoadInRange(index * kInSlice, (index + 1) * kInSlice);
		putToCache(std::move(result.toCache));
	}
	auto checkPriority = true;
	for (const auto offset : result.offsetsFromLoader.values()) {
		if (checkPriority) {
			checkLoadWillBeFirst(offset);
			checkPriority = false;
		}
		loadAtOffset(offset);
	}
	return result.filled;
}

void Reader::cancelLoadInRange(int from, int till) {
	Expects(from < till);

	for (const auto offset : _loadingOffsets.takeInRange(from, till)) {
		_loader->cancel(offset);
	}
}

void Reader::checkLoadWillBeFirst(int offset) {
	if (_loadingOffsets.front().value_or(offset) != offset) {
		_loadingOffsets.increasePriority();
		_loader->increasePriority();
	}
}

bool Reader::processCacheResults() {
	if (!_cacheHelper) {
		return false;
	} else if (_failed) {
		return false;
	}

	QMutexLocker lock(&_cacheHelper->mutex);
	auto loaded = base::take(_cacheHelper->results);
	lock.unlock();

	for (const auto &[sliceNumber, result] : loaded) {
		_slices.processCacheResult(sliceNumber, bytes::make_span(result));
	}
	return !loaded.empty();
}

bool Reader::processLoadedParts() {
	if (_failed) {
		return false;
	}

	QMutexLocker lock(&_loadedPartsMutex);
	auto loaded = base::take(_loadedParts);
	lock.unlock();

	for (auto &part : loaded) {
		if (part.offset == LoadedPart::kFailedOffset
			|| (part.bytes.size() != Loader::kPartSize
				&& part.offset + part.bytes.size() != size())) {
			_failed = Error::LoadFailed;
			return false;
		} else if (!_loadingOffsets.remove(part.offset)) {
			continue;
		}
		_slices.processPart(
			part.offset,
			std::move(part.bytes));
	}
	return !loaded.empty();
}

void Reader::loadAtOffset(int offset) {
	if (_loadingOffsets.add(offset)) {
		_loader->load(offset);
	}
}

void Reader::finalizeCache() {
	if (!_cacheHelper) {
		return;
	}
	if (_cacheHelper->waiting != nullptr) {
		QMutexLocker lock(&_cacheHelper->mutex);
		_cacheHelper->waiting = nullptr;
	}
	auto toCache = _slices.unloadToCache();
	while (toCache.number >= 0) {
		putToCache(std::move(toCache));
		toCache = _slices.unloadToCache();
	}
	_owner->cacheBigFile().sync();
}

Reader::~Reader() {
	finalizeCache();
}

} // namespace Streaming
} // namespace Media
