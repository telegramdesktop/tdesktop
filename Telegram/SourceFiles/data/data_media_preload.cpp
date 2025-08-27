/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_media_preload.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "media/streaming/media_streaming_reader.h"
#include "storage/file_download.h" // kMaxFileInMemory.

namespace Data {
namespace {

constexpr auto kDefaultPreloadPrefix = 4 * 1024 * 1024;

[[nodiscard]] int64 ChoosePreloadPrefix(not_null<DocumentData*> video) {
	const auto result = video->videoPreloadPrefix();
	return result
		? result
		: std::min(int64(kDefaultPreloadPrefix), video->size);
}

} // namespace

MediaPreload::MediaPreload(Fn<void()> done)
: _done(std::move(done)) {
}

void MediaPreload::callDone() {
	if (const auto onstack = _done) {
		onstack();
	}
}

PhotoPreload::PhotoPreload(
	not_null<PhotoData*> photo,
	FileOrigin origin,
	Fn<void()> done)
: MediaPreload(std::move(done))
, _photo(photo->createMediaView()) {
	start(origin);
}

PhotoPreload::~PhotoPreload() {
	if (_photo) {
		base::take(_photo)->owner()->cancel();
	}
}

bool PhotoPreload::Should(
		not_null<PhotoData*> photo,
		not_null<PeerData*> context) {
	return !photo->cancelled()
		&& AutoDownload::Should(
			photo->session().settings().autoDownload(),
			context,
			photo);
}

void PhotoPreload::start(FileOrigin origin) {
	if (_photo->loaded()) {
		callDone();
	} else {
		_photo->owner()->load(origin, LoadFromCloudOrLocal, true);
		_photo->owner()->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return _photo->loaded();
		}) | rpl::start_with_next([=] { callDone(); }, _lifetime);
	}
}

VideoPreload::VideoPreload(
	not_null<DocumentData*> video,
	FileOrigin origin,
	Fn<void()> done)
: MediaPreload(std::move(done))
, DownloadMtprotoTask(
	&video->session().downloader(),
	video->videoPreloadLocation(),
	origin)
, _video(video)
, _full(video->size) {
	if (Can(video)) {
		check();
	} else {
		callDone();
	}
}

void VideoPreload::check() {
	const auto key = _video->bigFileBaseCacheKey();
	const auto weak = base::make_weak(static_cast<has_weak_ptr*>(this));
	_video->owner().cacheBigFile().get(key, [weak](
			const QByteArray &result) {
		if (!result.isEmpty()) {
			crl::on_main([weak] {
				if (const auto strong = weak.get()) {
					static_cast<VideoPreload*>(strong)->callDone();
				}
			});
		} else {
			crl::on_main([weak] {
				if (const auto strong = weak.get()) {
					static_cast<VideoPreload*>(strong)->load();
				}
			});
		}
	});
}

void VideoPreload::load() {
	if (!Can(_video)) {
		callDone();
		return;
	}
	const auto prefix = ChoosePreloadPrefix(_video);
	Assert(prefix > 0 && prefix <= _video->size);
	const auto part = Storage::kDownloadPartSize;
	const auto parts = (prefix + part - 1) / part;
	for (auto i = 0; i != parts; ++i) {
		_parts.emplace(i * part, QByteArray());
	}
	addToQueue();
}

void VideoPreload::done(QByteArray result) {
	const auto key = _video->bigFileBaseCacheKey();
	if (!result.isEmpty() && key) {
		Assert(result.size() < Storage::kMaxFileInMemory);
		_video->owner().cacheBigFile().putIfEmpty(
			key,
			Storage::Cache::Database::TaggedValue(std::move(result), 0));
	}
	callDone();
}

VideoPreload::~VideoPreload() {
	if (!_finished && !_failed) {
		cancelAllRequests();
	}
}

bool VideoPreload::Can(not_null<DocumentData*> video) {
	return video->canBeStreamed(nullptr)
		&& video->videoPreloadLocation().valid()
		&& video->bigFileBaseCacheKey();
}

bool VideoPreload::readyToRequest() const {
	const auto part = Storage::kDownloadPartSize;
	return !_failed && (_nextRequestOffset < _parts.size() * part);
}

int64 VideoPreload::takeNextRequestOffset() {
	Expects(readyToRequest());

	_requestedOffsets.emplace(_nextRequestOffset);
	_nextRequestOffset += Storage::kDownloadPartSize;
	return _requestedOffsets.back();
}

bool VideoPreload::feedPart(
		int64 offset,
		const QByteArray &bytes) {
	Expects(offset < _parts.size() * Storage::kDownloadPartSize);
	Expects(_requestedOffsets.contains(int(offset)));
	Expects(bytes.size() <= Storage::kDownloadPartSize);

	const auto part = Storage::kDownloadPartSize;
	_requestedOffsets.remove(int(offset));
	_parts[offset] = bytes;
	if ((_nextRequestOffset + part >= _parts.size() * part)
		&& _requestedOffsets.empty()) {
		_finished = true;
		removeFromQueue();
		auto result = ::Media::Streaming::SerializeComplexPartsMap(_parts);
		if (result.size() == _full) {
			// Make sure it is parsed as a complex map.
			result.push_back(char(0));
		}
		done(std::move(result));
	}
	return true;
}

void VideoPreload::cancelOnFail() {
	_failed = true;
	cancelAllRequests();
	done({});
}

bool VideoPreload::setWebFileSizeHook(int64 size) {
	_failed = true;
	cancelAllRequests();
	done({});
	return false;
}

} // namespace Data
