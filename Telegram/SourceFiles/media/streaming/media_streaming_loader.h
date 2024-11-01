/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_common.h"

namespace Storage {
class StreamedFileDownloader;
} // namespace Storage

namespace Media::Streaming {

struct LoadedPart {
	int64 offset = 0;
	QByteArray bytes;

	static constexpr auto kFailedOffset = int64(-1);

	[[nodiscard]] bool valid(int64 size) const;
};

class Loader {
public:
	static constexpr auto kPartSize = int64(128 * 1024);

	[[nodiscard]] virtual Storage::Cache::Key baseCacheKey() const = 0;
	[[nodiscard]] virtual int64 size() const = 0;

	virtual void load(int64 offset) = 0;
	virtual void cancel(int64 offset) = 0;
	virtual void resetPriorities() = 0;
	virtual void setPriority(int priority) = 0;
	virtual void stop() = 0;

	// Remove from queue if no requests are in progress.
	virtual void tryRemoveFromQueue() = 0;

	// Parts will be sent from the main thread.
	[[nodiscard]] virtual rpl::producer<LoadedPart> parts() const = 0;
	[[nodiscard]] virtual auto speedEstimate() const
		-> rpl::producer<SpeedEstimate> = 0;

	virtual void attachDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader) = 0;
	virtual void clearAttachedDownloader() = 0;

	virtual ~Loader() = default;

};

class PriorityQueue {
public:
	bool add(int64 value);
	bool remove(int64 value);
	void resetPriorities();
	[[nodiscard]] bool empty() const;
	[[nodiscard]] std::optional<int64> front() const;
	[[nodiscard]] std::optional<int64> take();
	[[nodiscard]] base::flat_set<int64> takeInRange(int64 from, int64 till);
	void clear();

private:
	struct Entry {
		int64 value = 0;
		int priority = 0;
	};

	friend bool operator<(const Entry &a, const Entry &b);

	base::flat_set<Entry> _data;
	int _priority = 0;

};

} // namespace Media::Streaming
