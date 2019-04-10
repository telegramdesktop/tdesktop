/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/streamed_file_downloader.h"

#include "media/streaming/media_streaming_loader.h"
#include "media/streaming/media_streaming_reader.h"

namespace Storage {
namespace {

constexpr auto kPartSize = Media::Streaming::Loader::kPartSize;

} // namespace

StreamedFileDownloader::StreamedFileDownloader(
	uint64 objectId,
	Data::FileOrigin origin,
	std::optional<Cache::Key> cacheKey,
	std::shared_ptr<Media::Streaming::Reader> reader,

	// For FileLoader
	const QString &toFile,
	int32 size,
	LocationType locationType,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	toFile,
	size,
	locationType,
	toCache,
	fromCloud,
	autoLoading,
	cacheTag)
, _objectId(objectId)
, _origin(origin)
, _cacheKey(cacheKey)
, _reader(std::move(reader)) {
	_partIsSaved.resize((size + kPartSize - 1) / kPartSize, false);
}

StreamedFileDownloader::~StreamedFileDownloader() {
	stop();
}

uint64 StreamedFileDownloader::objId() const {
	return _objectId;
}

Data::FileOrigin StreamedFileDownloader::fileOrigin() const {
	return _origin;
}

int StreamedFileDownloader::currentOffset() const {
	return 0;
}

void StreamedFileDownloader::stop() {
	cancelRequests();
}

std::optional<Storage::Cache::Key> StreamedFileDownloader::cacheKey() const {
	return _cacheKey;
}

void StreamedFileDownloader::cancelRequests() {
}

bool StreamedFileDownloader::loadPart() {
	return false;
}

} // namespace Storage
