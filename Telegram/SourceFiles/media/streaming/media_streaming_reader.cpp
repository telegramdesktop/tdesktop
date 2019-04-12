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

// 1 MB of parts are requested from cloud ahead of reading demand.
constexpr auto kPreloadPartsAhead = 8;

using PartsMap = base::flat_map<int, QByteArray>;

struct ParsedCacheEntry {
	PartsMap parts;
	std::optional<PartsMap> included;
};

bool IsContiguousSerialization(int serializedSize, int maxSliceSize) {
	return !(serializedSize % kPartSize) || (serializedSize == maxSliceSize);
}

bool IsFullInHeader(int size) {
	return (size <= kMaxOnlyInHeader);
}

bool ComputeIsGoodHeader(int size, const PartsMap &header) {
	if (IsFullInHeader(size)) {
		return false;
	}
	const auto outsideFirstSliceIt = ranges::lower_bound(
		header,
		kInSlice,
		ranges::less(),
		&PartsMap::value_type::first);
	const auto outsideFirstSlice = end(header) - outsideFirstSliceIt;
	return (outsideFirstSlice <= kPartsOutsideFirstSliceGood);
}

int SlicesCount(int size) {
	return (size + kInSlice - 1) / kInSlice;
}

int MaxSliceSize(int sliceNumber, int size) {
	return !sliceNumber
		? size
		: (sliceNumber == SlicesCount(size))
		? (size - (sliceNumber - 1) * kInSlice)
		: kInSlice;
}

bytes::const_span ParseComplexCachedMap(
		PartsMap &result,
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
		result.try_emplace(
			offset,
			reinterpret_cast<const char*>(bytes.data()),
			bytes.size());
	}
	return data;
}

bytes::const_span ParseCachedMap(
		PartsMap &result,
		bytes::const_span data,
		int maxSize) {
	const auto size = int(data.size());
	if (IsContiguousSerialization(size, maxSize)) {
		if (size > maxSize) {
			return {};
		}
		for (auto offset = 0; offset < size; offset += kPartSize) {
			const auto part = data.subspan(
				offset,
				std::min(kPartSize, size - offset));
			result.try_emplace(
				offset,
				reinterpret_cast<const char*>(part.data()),
				part.size());
		}
		return {};
	}
	return ParseComplexCachedMap(result, data, maxSize);
}

ParsedCacheEntry ParseCacheEntry(
		bytes::const_span data,
		int sliceNumber,
		int size) {
	auto result = ParsedCacheEntry();
	const auto remaining = ParseCachedMap(
		result.parts,
		data,
		MaxSliceSize(sliceNumber, size));
	if (!sliceNumber && ComputeIsGoodHeader(size, result.parts)) {
		result.included = PartsMap();
		ParseCachedMap(*result.included, remaining, MaxSliceSize(1, size));
	}
	return result;
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
	base::flat_map<int, PartsMap> results;
	std::atomic<crl::semaphore*> waiting = nullptr;
};

Reader::CacheHelper::CacheHelper(Storage::Cache::Key baseKey)
: baseKey(baseKey) {
}

Storage::Cache::Key Reader::CacheHelper::key(int sliceNumber) const {
	return Storage::Cache::Key{ baseKey.high, baseKey.low + sliceNumber };
}

void Reader::Slice::processCacheData(PartsMap &&data) {
	Expects((flags & Flag::LoadingFromCache) != 0);
	Expects(!(flags & Flag::LoadedFromCache));

	const auto guard = gsl::finally([&] {
		flags |= Flag::LoadedFromCache;
		flags &= ~Flag::LoadingFromCache;
	});
	if (parts.empty()) {
		parts = std::move(data);
	} else {
		for (auto &[offset, bytes] : data) {
			parts.emplace(offset, std::move(bytes));
		}
	}
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
		&PartsMap::value_type::first);
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
		&PartsMap::value_type::first);
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
		&PartsMap::value_type::first);
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
		_data.resize(SlicesCount(_size));
	}
}

bool Reader::Slices::headerModeUnknown() const {
	return (_headerMode == HeaderMode::Unknown);
}

bool Reader::Slices::isFullInHeader() const {
	return IsFullInHeader(_size);
}

bool Reader::Slices::isGoodHeader() const {
	return (_headerMode == HeaderMode::Good);
}

bool Reader::Slices::computeIsGoodHeader() const {
	return ComputeIsGoodHeader(_size, _header.parts);
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

void Reader::Slices::processCacheResult(int sliceNumber, PartsMap &&result) {
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
	slice.processCacheData(std::move(result));
	if (!sliceNumber) {
		applyHeaderCacheData();
		if (isGoodHeader()) {
			// When we first read header we don't request the first slice.
			// But we get it, so let's apply it anyway.
			_data[0].flags |= Slice::Flag::LoadingFromCache;
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

QByteArray Reader::Slices::partForDownloader(int offset) const {
	Expects(offset < _size);

	if (const auto i = _header.parts.find(offset); i != end(_header.parts)) {
		return i->second;
	} else if (isFullInHeader()) {
		return QByteArray();
	}
	const auto index = offset / kInSlice;
	const auto &slice = _data[index];
	const auto i = slice.parts.find(offset - index * kInSlice);
	return (i != end(slice.parts)) ? i->second : QByteArray();
}

std::optional<int> Reader::Slices::readCacheRequiredFor(int offset) {
	Expects(offset < _size);

	using Flag = Slice::Flag;
	if ((_header.flags & Flag::LoadingFromCache) || isFullInHeader()) {
		return std::nullopt;
	}
	const auto index = offset / kInSlice;
	auto &slice = _data[index];
	return (slice.flags & Flag::LoadedFromCache)
		? std::nullopt
		: std::make_optional(index + 1);
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
	return MaxSliceSize(sliceNumber, _size);
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
		if (_downloaderAttached.load(std::memory_order_acquire)) {
			_partsForDownloader.fire_copy(part);
		}

		_loadedParts.emplace(std::move(part));

		if (const auto waiting = _waiting.load(std::memory_order_acquire)) {
			_waiting.store(nullptr, std::memory_order_release);
			waiting->release();
		}
	}, _lifetime);

	if (_cacheHelper) {
		readFromCache(0);
	}
}

void Reader::startSleep(not_null<crl::semaphore*> wake) {
	_sleeping.store(wake, std::memory_order_release);
	processDownloaderRequests();
}

void Reader::wakeFromSleep() {
	if (const auto sleeping = _sleeping.load(std::memory_order_acquire)) {
		_sleeping.store(nullptr, std::memory_order_release);
		sleeping->release();
	}
}

void Reader::stopSleep() {
	_sleeping.store(nullptr, std::memory_order_release);
}

void Reader::startStreaming() {
	_streamingActive = true;
}

void Reader::stopStreaming(bool stillActive) {
	Expects(_sleeping == nullptr);

	_waiting.store(nullptr, std::memory_order_release);
	if (!stillActive) {
		_streamingActive = false;
		processDownloaderRequests();
	}
}

rpl::producer<LoadedPart> Reader::partsForDownloader() const {
	return _partsForDownloader.events();
}

void Reader::loadForDownloader(int offset) {
	_downloaderAttached.store(true, std::memory_order_release);
	_downloaderOffsetRequests.emplace(offset);
	if (_streamingActive) {
		wakeFromSleep();
	} else {
		processDownloaderRequests();
	}
}

void Reader::doneForDownloader(int offset) {
	if (_downloaderOffsetsRequested.remove(offset) && !_streamingActive) {
		processDownloaderRequests();
	}
}

void Reader::cancelForDownloader() {
	if (_downloaderAttached.load(std::memory_order_acquire)) {
		_downloaderOffsetRequests.take();
		_downloaderAttached.store(false, std::memory_order_release);
	}
}

void Reader::enqueueDownloaderOffsets() {
	auto offsets = _downloaderOffsetRequests.take();
	if (!empty(offsets)) {
		if (!empty(_offsetsForDownloader)) {
			_offsetsForDownloader.insert(
				end(_offsetsForDownloader),
				std::make_move_iterator(begin(offsets)),
				std::make_move_iterator(end(offsets)));
			checkForDownloaderChange(offsets.size() + 1);
		} else {
			_offsetsForDownloader = std::move(offsets);
			checkForDownloaderChange(offsets.size());
		}
	}
}

void Reader::checkForDownloaderChange(int checkItemsCount) {
	Expects(checkItemsCount <= _offsetsForDownloader.size());

	// If a requested offset is less-or-equal of some previously requested
	// offset, it means that the downloader was changed, ignore old offsets.
	const auto end = _offsetsForDownloader.end();
	const auto changed = std::adjacent_find(
		end - checkItemsCount,
		end,
		[](int first, int second) { return (second <= first); });
	if (changed != end) {
		_offsetsForDownloader.erase(
			begin(_offsetsForDownloader),
			changed + 1);
		_downloaderSliceNumber = 0;
		_downloaderSliceCache = std::nullopt;
	}
}

void Reader::checkForDownloaderReadyOffsets() {
	// If a requested part is available right now we simply fire it on the
	// main thread, until the first not-available-right-now offset is found.
	const auto ready = [&](int offset, QByteArray &&bytes) {
		crl::on_main(this, [=, bytes = std::move(bytes)]() mutable {
			_partsForDownloader.fire({ offset, std::move(bytes) });
		});
		return true;
	};
	const auto unavailable = [&](int offset) {
		auto bytes = _slices.partForDownloader(offset);
		if (!bytes.isEmpty()) {
			return !ready(offset, std::move(bytes));
		}
		const auto sliceIndex = (offset / kInSlice);
		if ((sliceIndex + 1 == _downloaderSliceNumber)
			&& _downloaderSliceCache) {
			const auto i = _downloaderSliceCache->find(
				offset - sliceIndex * kInSlice);
			if (i != _downloaderSliceCache->end()) {
				return !ready(offset, std::move(i->second));
			}
		}
		return true;
	};
	_offsetsForDownloader.erase(
		begin(_offsetsForDownloader),
		ranges::find_if(_offsetsForDownloader, unavailable));
}

void Reader::processDownloaderRequests() {
	processCacheResults();
	enqueueDownloaderOffsets();
	checkForDownloaderReadyOffsets();
	if (empty(_offsetsForDownloader)) {
		return;
	}

	const auto offset = _offsetsForDownloader.front();
	if (_cacheHelper && downloaderWaitForCachedSlice(offset)) {
		return;
	}

	_offsetsForDownloader.pop_front();
	if (_downloaderOffsetsRequested.emplace(offset).second) {
		_loader->load(offset);
	}
}

bool Reader::downloaderWaitForCachedSlice(int offset) {
	const auto sliceNumber = _slices.readCacheRequiredFor(offset);
	if (sliceNumber.value_or(0) != _downloaderSliceNumber) {
		_downloaderSliceNumber = sliceNumber.value_or(0);
		_downloaderSliceCache = std::nullopt;
		if (_downloaderSliceNumber) {
			if (readFromCacheForDownloader()) {
				return true;
			}
			_downloaderSliceCache = PartsMap();
		}
	} else if (_downloaderSliceNumber && !_downloaderSliceCache) {
		return true;
	}
	return false;
}

void Reader::checkCacheResultsForDownloader() {
	if (_streamingActive) {
		return;
	}
	processDownloaderRequests();
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
	const auto size = _loader->size();
	const auto key = _cacheHelper->key(sliceNumber);
	const auto cache = std::weak_ptr<CacheHelper>(_cacheHelper);
	const auto weak = base::make_weak(this);
	_owner->cacheBigFile().get(key, [=](QByteArray &&result) {
		crl::async([=, result = std::move(result)]{
			auto entry = ParseCacheEntry(
				bytes::make_span(result),
				sliceNumber,
				size);
			if (const auto strong = cache.lock()) {
				QMutexLocker lock(&strong->mutex);
				strong->results.emplace(sliceNumber, std::move(entry.parts));
				if (!sliceNumber && entry.included) {
					strong->results.emplace(1, std::move(*entry.included));
				}
				if (const auto waiting = strong->waiting.load()) {
					strong->waiting.store(nullptr, std::memory_order_release);
					waiting->release();
				} else {
					crl::on_main(weak, [=] {
						checkCacheResultsForDownloader();
					});
				}
			}
		});
	});
}

bool Reader::readFromCacheForDownloader() {
	Expects(_cacheHelper != nullptr);
	Expects(_downloaderSliceNumber > 0);

	if (_slices.headerModeUnknown()) {
		return false;
	}
	readFromCache(_downloaderSliceNumber);
	return true;
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

std::optional<Error> Reader::streamingError() const {
	return _streamingError;
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
		_waiting.store(notify.get(), std::memory_order_release);
	};
	const auto clearWaiting = [&] {
		_waiting.store(nullptr, std::memory_order_release);
		if (_cacheHelper) {
			_cacheHelper->waiting.store(nullptr, std::memory_order_release);
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

	checkForSomethingMoreReceived();
	if (_streamingError) {
		return failed();
	}

	do {
		if (fillFromSlices(offset, buffer)) {
			clearWaiting();
			return true;
		}
		startWaiting();
	} while (checkForSomethingMoreReceived());

	return _streamingError ? failed() : false;
}

bool Reader::fillFromSlices(int offset, bytes::span buffer) {
	using namespace rpl::mappers;

	auto result = _slices.fill(offset, buffer);
	if (!result.filled && _slices.headerWontBeFilled()) {
		_streamingError = Error::NotStreamable;
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
		if (!_downloaderOffsetsRequested.contains(offset)) {
			_loader->cancel(offset);
		}
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
	}

	QMutexLocker lock(&_cacheHelper->mutex);
	auto loaded = base::take(_cacheHelper->results);
	lock.unlock();

	if (_downloaderSliceNumber) {
		const auto i = loaded.find(_downloaderSliceNumber);
		if (i != end(loaded)) {
			_downloaderSliceCache = i->second;
		}
	}

	if (_streamingError) {
		return false;
	}
	for (auto &[sliceNumber, result] : loaded) {
		_slices.processCacheResult(sliceNumber, std::move(result));
	}
	if (!loaded.empty()
		&& (loaded.front().first == 0)
		&& _slices.isGoodHeader()) {
		Assert(loaded.size() > 1);
		Assert((loaded.begin() + 1)->first == 1);
	}
	return !loaded.empty();
}

bool Reader::processLoadedParts() {
	if (_streamingError) {
		return false;
	}

	auto loaded = _loadedParts.take();
	for (auto &part : loaded) {
		if (!part.valid(size())) {
			_streamingError = Error::LoadFailed;
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

bool Reader::checkForSomethingMoreReceived() {
	const auto result1 = processCacheResults();
	const auto result2 = processLoadedParts();
	return result1 || result2;
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
		_cacheHelper->waiting.store(nullptr, std::memory_order_release);
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
