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

LoaderMtproto::LoaderMtproto(
	not_null<Storage::DownloadManagerMtproto*> owner,
	const StorageFileLocation &location,
	int size,
	Data::FileOrigin origin)
: DownloadMtprotoTask(owner, location, origin)
, _size(size)
, _api(&api().instance()) {
}

Storage::Cache::Key LoaderMtproto::baseCacheKey() const {
	return v::get<StorageFileLocation>(
		location().data
	).bigFileBaseCacheKey();
}

int LoaderMtproto::size() const {
	return _size;
}

void LoaderMtproto::load(int offset) {
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

void LoaderMtproto::cancel(int offset) {
	crl::on_main(this, [=] {
		cancelForOffset(offset);
	});
}

void LoaderMtproto::cancelForOffset(int offset) {
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

int LoaderMtproto::takeNextRequestOffset() {
	const auto offset = _requested.take();

	Ensures(offset.has_value());
	return *offset;
}

bool LoaderMtproto::feedPart(int offset, const QByteArray &bytes) {
	_parts.fire({ offset, bytes });
	return true;
}

void LoaderMtproto::cancelOnFail() {
	_parts.fire({ LoadedPart::kFailedOffset });
}

rpl::producer<LoadedPart> LoaderMtproto::parts() const {
	return _parts.events();
}

} // namespace Streaming
} // namespace Media
