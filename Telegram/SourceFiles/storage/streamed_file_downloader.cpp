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

using namespace Media::Streaming;

constexpr auto kPartSize = Loader::kPartSize;

} // namespace

StreamedFileDownloader::StreamedFileDownloader(
	uint64 objectId,
	MTP::DcId dcId,
	Data::FileOrigin origin,
	Cache::Key cacheKey,
	MediaKey fileLocationKey,
	std::shared_ptr<Reader> reader,

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
, _fileLocationKey(fileLocationKey)
, _reader(std::move(reader))
, _partsCount((size + kPartSize - 1) / kPartSize) {
	_partIsSaved.resize(_partsCount, false);

	_reader->partsForDownloader(
	) | rpl::start_with_next([=](const LoadedPart &part) {
		if (part.offset == LoadedPart::kFailedOffset) {
			cancel(true);
		} else {
			savePart(std::move(part));
		}
	}, _lifetime);

	_queue = _downloader->queueForDc(dcId);
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

void StreamedFileDownloader::stop() {
	cancelRequests();
}

QByteArray StreamedFileDownloader::readLoadedPart(int offset) {
	Expects(offset >= 0 && offset < _size);
	Expects(!(offset % kPartSize));

	const auto index = (offset / kPartSize);
	return _partIsSaved[index]
		? readLoadedPartBack(offset, kPartSize)
		: QByteArray();
}

Storage::Cache::Key StreamedFileDownloader::cacheKey() const {
	return _cacheKey;
}

std::optional<MediaKey> StreamedFileDownloader::fileLocationKey() const {
	return _fileLocationKey;
}

void StreamedFileDownloader::cancelRequests() {
	//_partsRequested == std::count(
	//	begin(_partIsSaved),
	//	begin(_partIsSaved) + _nextPartIndex,
	//	false);
	_queue->queriesCount -= _partsRequested;
	_partsRequested = 0;
	_nextPartIndex = 0;

	_reader->cancelForDownloader(this);
}

bool StreamedFileDownloader::loadPart() {
	if (_finished || _nextPartIndex >= _partsCount) {
		return false;
	}
	const auto index = std::find(
		begin(_partIsSaved) + _nextPartIndex,
		end(_partIsSaved),
		false
	) - begin(_partIsSaved);
	if (index == _partsCount) {
		_nextPartIndex = _partsCount;
		return false;
	}
	_nextPartIndex = index + 1;
	_reader->loadForDownloader(this, index * kPartSize);

	++_partsRequested;
	++_queue->queriesCount;

	return true;
}

void StreamedFileDownloader::savePart(const LoadedPart &part) {
	Expects(part.offset >= 0 && part.offset < _reader->size());
	Expects(part.offset % kPartSize == 0);

	if (_finished || _cancelled) {
		return;
	}

	const auto offset = part.offset;
	const auto index = offset / kPartSize;
	Assert(index >= 0 && index < _partsCount);
	if (_partIsSaved[index]) {
		return;
	}
	_partIsSaved[index] = true;
	++_partsSaved;

	if (index < _nextPartIndex) {
		--_partsRequested;
		--_queue->queriesCount;
	}
	if (!writeResultPart(offset, bytes::make_span(part.bytes))) {
		return;
	}
	if (_partsSaved == _partsCount) {
		if (!finalizeResult()) {
			return;
		}
	}
	_reader->doneForDownloader(offset);
	notifyAboutProgress();
}

} // namespace Storage
