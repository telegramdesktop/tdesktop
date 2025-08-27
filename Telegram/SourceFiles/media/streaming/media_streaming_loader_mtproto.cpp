/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_loader_mtproto.h"

#include "apiwrap.h"
#include "main/main_session.h"
#include "storage/streamed_file_downloader.h"
#include "storage/cache/storage_cache_types.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kCheckStatsInterval = crl::time(1000);
constexpr auto kInitialStatsWait = 5 * crl::time(1000);

} // namespace

LoaderMtproto::LoaderMtproto(
	not_null<Storage::DownloadManagerMtproto*> owner,
	const StorageFileLocation &location,
	int64 size,
	Data::FileOrigin origin)
: DownloadMtprotoTask(owner, location, origin)
, _size(size)
, _api(&api().instance())
, _statsTimer([=] { checkStats(); }) {
}

Storage::Cache::Key LoaderMtproto::baseCacheKey() const {
	return v::get<StorageFileLocation>(
		location().data
	).bigFileBaseCacheKey();
}

int64 LoaderMtproto::size() const {
	return _size;
}

void LoaderMtproto::load(int64 offset) {
	crl::on_main(this, [=] {
		if (_downloader) {
			auto bytes = _downloader->readLoadedPart(offset);
			if (!bytes.isEmpty()) {
				cancelForOffset(offset);
				_parts.fire({ offset, std::move(bytes) });
				return;
			}
		}
		if (haveSentRequestForOffset(offset)) {
			return;
		} else if (_requested.add(offset)) {
			addToQueueWithPriority();
		}
	});
}

void LoaderMtproto::addToQueueWithPriority() {
	addToQueue(_priority);
}

void LoaderMtproto::stop() {
	crl::on_main(this, [=] {
		cancelAllRequests();
		_requested.clear();
		removeFromQueue();
	});
}

void LoaderMtproto::tryRemoveFromQueue() {
	crl::on_main(this, [=] {
		if (_requested.empty() && !haveSentRequests()) {
			removeFromQueue();
		}
	});
}

void LoaderMtproto::cancel(int64 offset) {
	crl::on_main(this, [=] {
		cancelForOffset(offset);
	});
}

void LoaderMtproto::cancelForOffset(int64 offset) {
	if (haveSentRequestForOffset(offset)) {
		cancelRequestForOffset(offset);
		if (!_requested.empty()) {
			addToQueueWithPriority();
		}
	} else {
		_requested.remove(offset);
	}
}

void LoaderMtproto::attachDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader) {
	_downloader = downloader;
}

void LoaderMtproto::clearAttachedDownloader() {
	_downloader = nullptr;
}

void LoaderMtproto::resetPriorities() {
	crl::on_main(this, [=] {
		_requested.resetPriorities();
	});
}

void LoaderMtproto::setPriority(int priority) {
	if (_priority == priority) {
		return;
	}
	_priority = priority;
	if (haveSentRequests()) {
		addToQueueWithPriority();
	}
}

bool LoaderMtproto::readyToRequest() const {
	return !_requested.empty();
}

int64 LoaderMtproto::takeNextRequestOffset() {
	const auto offset = _requested.take();
	Assert(offset.has_value());

	const auto time = crl::now();
	if (!_firstRequestStart) {
		_firstRequestStart = time;
	}
	_stats.push_back({ .start = crl::now(), .offset = *offset });

	Ensures(offset.has_value());
	return *offset;
}

bool LoaderMtproto::feedPart(int64 offset, const QByteArray &bytes) {
	const auto time = crl::now();
	for (auto &entry : _stats) {
		if (entry.offset == offset && entry.start < time) {
			entry.end = time;
			if (!_statsTimer.isActive()) {
				const auto checkAt = std::max(
					time + kCheckStatsInterval,
					_firstRequestStart + kInitialStatsWait);
				_statsTimer.callOnce(checkAt - time);
			}
			break;
		}
	}
	_parts.fire({ offset, bytes });
	return true;
}

void LoaderMtproto::cancelOnFail() {
	_parts.fire({ LoadedPart::kFailedOffset });
}

rpl::producer<LoadedPart> LoaderMtproto::parts() const {
	return _parts.events();
}

rpl::producer<SpeedEstimate> LoaderMtproto::speedEstimate() const {
	return _speedEstimate.events();
}

void LoaderMtproto::checkStats() {
	const auto time = crl::now();
	const auto from = time - kInitialStatsWait;
	{ // Erase all stats entries that are too old.
		for (auto i = begin(_stats); i != end(_stats);) {
			if (i->start >= from) {
				break;
			} else if (i->end && i->end < from) {
				i = _stats.erase(i);
			} else {
				++i;
			}
		}
	}
	if (_stats.empty()) {
		return;
	}
	// Count duration for which at least one request was in progress.
	// This is the time we should consider for download speed.
	// We don't count time when no requests were in progress.
	auto durationCountedTill = _stats.front().start;
	auto duration = crl::time(0);
	auto received = int64(0);
	for (const auto &entry : _stats) {
		if (entry.start > durationCountedTill) {
			durationCountedTill = entry.start;
		}
		const auto till = entry.end ? entry.end : time;
		if (till > durationCountedTill) {
			duration += (till - durationCountedTill);
			durationCountedTill = till;
		}
		if (entry.end) {
			received += Storage::kDownloadPartSize;
		}
	}
	if (duration) {
		_speedEstimate.fire({
			.bytesPerSecond = int(std::clamp(
				int64(received * 1000 / duration),
				int64(0),
				int64(64 * 1024 * 1024))),
			.unreliable = (received < 3 * Storage::kDownloadPartSize),
		});
	}
}

} // namespace Streaming
} // namespace Media
