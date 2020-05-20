/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Storage {
class StreamedFileDownloader;
} // namespace Storage

namespace Media {
namespace Streaming {

struct LoadedPart {
	int offset = 0;
	QByteArray bytes;

	static constexpr auto kFailedOffset = -1;

	[[nodiscard]] bool valid(int size) const;
};

class Loader {
public:
	static constexpr auto kPartSize = 128 * 1024;

	[[nodiscard]] virtual Storage::Cache::Key baseCacheKey() const = 0;
	[[nodiscard]] virtual int size() const = 0;

	virtual void load(int offset) = 0;
	virtual void cancel(int offset) = 0;
	virtual void resetPriorities() = 0;
	virtual void setPriority(int priority) = 0;
	virtual void stop() = 0;

	// Remove from queue if no requests are in progress.
	virtual void tryRemoveFromQueue() = 0;

	// Parts will be sent from the main thread.
	[[nodiscard]] virtual rpl::producer<LoadedPart> parts() const = 0;

	virtual void attachDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader) = 0;
	virtual void clearAttachedDownloader() = 0;

	virtual ~Loader() = default;

};

class PriorityQueue {
public:
	bool add(int value);
	bool remove(int value);
	void resetPriorities();
	[[nodiscard]] bool empty() const;
	[[nodiscard]] std::optional<int> front() const;
	[[nodiscard]] std::optional<int> take();
	[[nodiscard]] base::flat_set<int> takeInRange(int from, int till);
	void clear();

private:
	struct Entry {
		int value = 0;
		int priority = 0;
	};

	friend bool operator<(const Entry &a, const Entry &b);

	base::flat_set<Entry> _data;
	int _priority = 0;

};

} // namespace Streaming
} // namespace Media
