/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_loader.h"
#include "base/bytes.h"

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Data {
class Session;
} // namespace Data

namespace Media {
namespace Streaming {

class Loader;
struct LoadedPart;
enum class Error;

class Reader final {
public:
	Reader(not_null<Data::Session*> owner, std::unique_ptr<Loader> loader);

	[[nodiscard]] int size() const;
	[[nodiscard]] bool fill(
		int offset,
		bytes::span buffer,
		not_null<crl::semaphore*> notify);
	[[nodiscard]] std::optional<Error> failed() const;

	void headerDone();

	void stop();

	[[nodiscard]] bool isRemoteLoader() const;

	~Reader();

private:
	static constexpr auto kLoadFromRemoteMax = 8;

	struct CacheHelper;

	template <int Size>
	class StackIntVector {
	public:
		bool add(int value);
		auto values() const;

	private:
		std::array<int, Size> _storage = { -1 };

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
		bool filled = false;
	};

	struct Slice {
		enum class Flag : uchar {
			LoadingFromCache = 0x01,
			LoadedFromCache = 0x02,
			ChangedSinceCache = 0x04,
		};
		friend constexpr inline bool is_flag_type(Flag) { return true; }
		using Flags = base::flags<Flag>;

		struct PrepareFillResult {
			StackIntVector<kLoadFromRemoteMax> offsetsFromLoader;
			base::flat_map<int, QByteArray>::const_iterator start;
			base::flat_map<int, QByteArray>::const_iterator finish;
			bool ready = true;
		};

		bytes::const_span processCacheData(
			bytes::const_span data,
			int maxSize);
		bytes::const_span processComplexCacheData(
			bytes::const_span data,
			int maxSize);
		void addPart(int offset, QByteArray bytes);
		PrepareFillResult prepareFill(int from, int till);

		// Get up to kLoadFromRemoteMax not loaded parts in from-till range.
		StackIntVector<kLoadFromRemoteMax> offsetsFromLoader(
			int from,
			int till) const;

		base::flat_map<int, QByteArray> parts;
		Flags flags;

	};

	class Slices {
	public:
		Slices(int size, bool useCache);

		void headerDone(bool fromCache);
		[[nodiscard]] bool headerWontBeFilled() const;
		[[nodiscard]] bool headerModeUnknown() const;
		[[nodiscard]] bool isFullInHeader() const;
		[[nodiscard]] bool isGoodHeader() const;

		void processCacheResult(int sliceNumber, bytes::const_span result);
		void processPart(int offset, QByteArray &&bytes);

		[[nodiscard]] FillResult fill(int offset, bytes::span buffer);
		[[nodiscard]] SerializedSlice unloadToCache();

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
			int offset,
			bytes::span buffer);

		std::vector<Slice> _data;
		Slice _header;
		std::deque<int> _usedSlices;
		int _size = 0;
		HeaderMode _headerMode = HeaderMode::Unknown;

	};

	// 0 is for headerData, slice index = sliceNumber - 1.
	void readFromCache(int sliceNumber);
	bool processCacheResults();
	void putToCache(SerializedSlice &&data);

	void cancelLoadInRange(int from, int till);
	void loadAtOffset(int offset);
	void checkLoadWillBeFirst(int offset);
	bool processLoadedParts();

	bool fillFromSlices(int offset, bytes::span buffer);

	void finalizeCache();

	static std::shared_ptr<CacheHelper> InitCacheHelper(
		std::optional<Storage::Cache::Key> baseKey);

	const not_null<Data::Session*> _owner;
	const std::unique_ptr<Loader> _loader;
	const std::shared_ptr<CacheHelper> _cacheHelper;

	QMutex _loadedPartsMutex;
	std::vector<LoadedPart> _loadedParts;
	std::atomic<crl::semaphore*> _waiting = nullptr;
	PriorityQueue _loadingOffsets;

	Slices _slices;
	std::optional<Error> _failed;
	rpl::lifetime _lifetime;

};

} // namespace Streaming
} // namespace Media
