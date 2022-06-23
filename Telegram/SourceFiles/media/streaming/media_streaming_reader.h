/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_loader.h"
#include "base/bytes.h"
#include "base/weak_ptr.h"
#include "base/thread_safe_wrap.h"

namespace Storage {
class StreamedFileDownloader;
} // namespace Storage

namespace Storage {
namespace Cache {
struct Key;
class Database;
} // namespace Cache
} // namespace Storage

namespace Media {
namespace Streaming {

class Loader;
struct LoadedPart;
enum class Error;

class Reader final : public base::has_weak_ptr {
public:
	enum class FillState : uchar {
		Success,
		WaitingCache,
		WaitingRemote,
		Failed,
	};

	// Main thread.
	explicit Reader(
		std::unique_ptr<Loader> loader,
		Storage::Cache::Database *cache = nullptr);

	void setLoaderPriority(int priority);

	// Any thread.
	[[nodiscard]] int64 size() const;
	[[nodiscard]] bool isRemoteLoader() const;

	// Single thread.
	[[nodiscard]] FillState fill(
		int64 offset,
		bytes::span buffer,
		not_null<crl::semaphore*> notify);
	[[nodiscard]] std::optional<Error> streamingError() const;
	void headerDone();
	[[nodiscard]] int headerSize() const;
	[[nodiscard]] bool fullInCache() const;

	// Thread safe.
	void startSleep(not_null<crl::semaphore*> wake);
	void wakeFromSleep();
	void stopSleep();
	void stopStreamingAsync();
	void tryRemoveLoaderAsync();

	// Main thread.
	void startStreaming();
	void stopStreaming(bool stillActive = false);
	[[nodiscard]] rpl::producer<LoadedPart> partsForDownloader() const;
	void loadForDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader,
		int64 offset);
	void doneForDownloader(int64 offset);
	void cancelForDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader);

	~Reader();

private:
	static constexpr auto kLoadFromRemoteMax = 8;

	struct CacheHelper;

	// FileSize: Right now any file size fits 32 bit.

	using PartsMap = base::flat_map<uint32, QByteArray>;

	template <int Size>
	class StackIntVector {
	public:
		bool add(uint32 value);
		auto values() const;

	private:
		std::array<uint32, Size> _storage = { uint32(-1) };

	};

	struct SerializedSlice {
		int number = -1;
		QByteArray data;
	};
	struct FillResult {
		static constexpr auto kReadFromCacheMax = 2;

		StackIntVector<kReadFromCacheMax> sliceNumbersFromCache;
		StackIntVector<kLoadFromRemoteMax> offsetsFromLoader;
		SerializedSlice toCache;
		FillState state = FillState::WaitingRemote;
	};
	struct Slice {
		enum class Flag : uchar {
			LoadingFromCache = 0x01,
			LoadedFromCache = 0x02,
			ChangedSinceCache = 0x04,
			FullInCache = 0x08,
		};
		friend constexpr inline bool is_flag_type(Flag) { return true; }
		using Flags = base::flags<Flag>;

		struct PrepareFillResult {
			StackIntVector<kLoadFromRemoteMax> offsetsFromLoader;
			PartsMap::const_iterator start;
			PartsMap::const_iterator finish;
			bool ready = true;
		};

		void processCacheData(PartsMap &&data);
		void addPart(uint32 offset, QByteArray bytes);
		PrepareFillResult prepareFill(uint32 from, uint32 till);

		// Get up to kLoadFromRemoteMax not loaded parts in from-till range.
		StackIntVector<kLoadFromRemoteMax> offsetsFromLoader(
			uint32 from,
			uint32 till) const;

		PartsMap parts;
		Flags flags;

	};

	class Slices {
	public:
		Slices(uint32 size, bool useCache);

		void headerDone(bool fromCache);
		[[nodiscard]] int headerSize() const;
		[[nodiscard]] bool fullInCache() const;
		[[nodiscard]] bool headerWontBeFilled() const;
		[[nodiscard]] bool headerModeUnknown() const;
		[[nodiscard]] bool isFullInHeader() const;
		[[nodiscard]] bool isGoodHeader() const;
		[[nodiscard]] bool waitingForHeaderCache() const;

		[[nodiscard]] int requestSliceSizesCount() const;

		void processCacheResult(int sliceNumber, PartsMap &&result);
		void processCachedSizes(const std::vector<int> &sizes);
		void processPart(uint32 offset, QByteArray &&bytes);

		[[nodiscard]] FillResult fill(uint32 offset, bytes::span buffer);
		[[nodiscard]] SerializedSlice unloadToCache();

		[[nodiscard]] QByteArray partForDownloader(uint32 offset) const;
		[[nodiscard]] bool readCacheForDownloaderRequired(uint32 offset);

	private:
		enum class HeaderMode {
			Unknown,
			Small,
			Good,
			Full,
			NoCache,
		};

		void applyHeaderCacheData();
		[[nodiscard]] int maxSliceSize(int sliceNumber) const;
		[[nodiscard]] SerializedSlice serializeAndUnloadSlice(
			int sliceNumber);
		[[nodiscard]] SerializedSlice serializeAndUnloadUnused();
		[[nodiscard]] QByteArray serializeComplexSlice(
			const Slice &slice) const;
		[[nodiscard]] QByteArray serializeAndUnloadFirstSliceNoHeader();
		void markSliceUsed(int sliceIndex);
		[[nodiscard]] bool computeIsGoodHeader() const;
		[[nodiscard]] FillResult fillFromHeader(
			uint32 offset,
			bytes::span buffer);
		void unloadSlice(Slice &slice) const;
		void checkSliceFullLoaded(int sliceNumber);
		[[nodiscard]] bool checkFullInCache() const;

		std::vector<Slice> _data;
		Slice _header;
		std::deque<int> _usedSlices;
		uint32 _size = 0;
		HeaderMode _headerMode = HeaderMode::Unknown;
		bool _fullInCache = false;

	};

	// 0 is for headerData, slice index = sliceNumber - 1.
	// returns false if asked for a known-empty downloader slice cache.
	void readFromCache(int sliceNumber);
	[[nodiscard]] bool readFromCacheForDownloader(int sliceNumber);
	bool processCacheResults();
	void putToCache(SerializedSlice &&data);

	void cancelLoadInRange(uint32 from, uint32 till);
	void loadAtOffset(uint32 offset);
	void checkLoadWillBeFirst(uint32 offset);
	bool processLoadedParts();

	bool checkForSomethingMoreReceived();

	FillState fillFromSlices(uint32 offset, bytes::span buffer);

	void finalizeCache();

	void processDownloaderRequests();
	void checkCacheResultsForDownloader();
	void pruneDownloaderCache(uint32 minimalOffset);
	void pruneDoneDownloaderRequests();
	void sendDownloaderRequests();
	[[nodiscard]] bool downloaderWaitForCachedSlice(uint32 offset);
	void enqueueDownloaderOffsets();
	void checkForDownloaderChange(int checkItemsCount);
	void checkForDownloaderReadyOffsets();

	void refreshLoaderPriority();

	static std::shared_ptr<CacheHelper> InitCacheHelper(
		Storage::Cache::Key baseKey);

	const std::unique_ptr<Loader> _loader;
	Storage::Cache::Database * const _cache = nullptr;

	// shared_ptr is used to be able to have weak_ptr.
	const std::shared_ptr<CacheHelper> _cacheHelper;

	base::thread_safe_queue<LoadedPart, std::vector> _loadedParts;
	std::atomic<crl::semaphore*> _waiting = nullptr;
	std::atomic<crl::semaphore*> _sleeping = nullptr;
	std::atomic<bool> _stopStreamingAsync = false;
	PriorityQueue _loadingOffsets;

	Slices _slices;

	// Even if streaming had failed, the Reader can work for the downloader.
	std::optional<Error> _streamingError;

	// In case streaming is active both main and streaming threads have work.
	// In case only downloader is active, all work is done on main thread.

	// Main thread.
	Storage::StreamedFileDownloader *_attachedDownloader = nullptr;
	rpl::event_stream<LoadedPart> _partsForDownloader;
	int _realPriority = 1;
	bool _streamingActive = false;

	// Streaming thread.
	std::deque<uint32> _offsetsForDownloader;
	base::flat_set<uint32> _downloaderOffsetsRequested;
	base::flat_map<uint32, std::optional<PartsMap>> _downloaderReadCache;

	// Communication from main thread to streaming thread.
	// Streaming thread to main thread communicates using crl::on_main.
	base::thread_safe_queue<uint32> _downloaderOffsetRequests;
	base::thread_safe_queue<uint32> _downloaderOffsetAcks;

	rpl::lifetime _lifetime;

};

} // namespace Streaming
} // namespace Media
