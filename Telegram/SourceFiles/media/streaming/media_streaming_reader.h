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

class Reader final {
public:
	Reader(not_null<Data::Session*> owner, std::unique_ptr<Loader> loader);

	int size() const;
	bool fill(
		int offset,
		bytes::span buffer,
		not_null<crl::semaphore*> notify);
	bool failed() const;

	void headerDone();

	void stop();

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
			Header = 0x01,
			LoadingFromCache = 0x02,
			LoadedFromCache = 0x04,
			ChangedSinceCache = 0x08,
		};
		friend constexpr inline bool is_flag_type(Flag) { return true; }
		using Flags = base::flags<Flag>;

		struct PrepareFillResult {
			StackIntVector<kLoadFromRemoteMax> offsetsFromLoader;
			base::flat_map<int, QByteArray>::const_iterator start;
			base::flat_map<int, QByteArray>::const_iterator finish;
			bool ready = true;
		};

		bool processCacheData(QByteArray &&data, int maxSliceSize);
		bool processComplexCacheData(
			bytes::const_span data,
			int maxSliceSize);
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

		bool processCacheResult(int sliceNumber, QByteArray &&result);
		bool processPart(int offset, QByteArray &&bytes);

		FillResult fill(int offset, bytes::span buffer);
		SerializedSlice unloadToCache();

	private:
		enum class HeaderMode {
			Unknown,
			Small,
//			Full,
			NoCache,
		};

		void applyHeaderCacheData();
		int maxSliceSize(int sliceNumber) const;
		SerializedSlice serializeAndUnloadSlice(int sliceNumber);
		SerializedSlice serializeAndUnloadUnused();
		void markSliceUsed(int sliceIndex);

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
	bool _failed = false;
	rpl::lifetime _lifetime;

};

} // namespace Streaming
} // namespace Media
