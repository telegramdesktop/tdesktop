/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/file_download_mtproto.h"

#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "storage/cache/storage_cache_types.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_auth_key.h"
#include "base/openssl_help.h"

mtpFileLoader::mtpFileLoader(
	not_null<Main::Session*> session,
	const StorageFileLocation &location,
	Data::FileOrigin origin,
	LocationType type,
	const QString &to,
	int32 size,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	session,
	to,
	size,
	type,
	toCache,
	fromCloud,
	autoLoading,
	cacheTag)
, DownloadMtprotoTask(&session->downloader(), location, origin) {
}

mtpFileLoader::mtpFileLoader(
	not_null<Main::Session*> session,
	const WebFileLocation &location,
	int32 size,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	session,
	QString(),
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, DownloadMtprotoTask(
	&session->downloader(),
	session->serverConfig().webFileDcId,
	{ location }) {
}

mtpFileLoader::mtpFileLoader(
	not_null<Main::Session*> session,
	const GeoPointLocation &location,
	int32 size,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	session,
	QString(),
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, DownloadMtprotoTask(
	&session->downloader(),
	session->serverConfig().webFileDcId,
	{ location }) {
}

mtpFileLoader::~mtpFileLoader() {
	if (!_finished) {
		cancel();
	}
}

Data::FileOrigin mtpFileLoader::fileOrigin() const {
	return DownloadMtprotoTask::fileOrigin();
}

uint64 mtpFileLoader::objId() const {
	return DownloadMtprotoTask::objectId();
}

bool mtpFileLoader::readyToRequest() const {
	return !_finished
		&& !_lastComplete
		&& (_size != 0 || !haveSentRequests())
		&& (!_size || _nextRequestOffset < _size);
}

int mtpFileLoader::takeNextRequestOffset() {
	Expects(readyToRequest());

	const auto result = _nextRequestOffset;
	_nextRequestOffset += Storage::kDownloadPartSize;
	return result;
}

bool mtpFileLoader::feedPart(int offset, const QByteArray &bytes) {
	const auto buffer = bytes::make_span(bytes);
	if (!writeResultPart(offset, buffer)) {
		return false;
	}
	if (buffer.empty() || (buffer.size() % 1024)) { // bad next offset
		_lastComplete = true;
	}
	const auto finished = !haveSentRequests()
		&& (_lastComplete || (_size && _nextRequestOffset >= _size));
	if (finished) {
		removeFromQueue();
		if (!finalizeResult()) {
			return false;
		}
	} else {
		notifyAboutProgress();
	}
	return true;
}

void mtpFileLoader::cancelOnFail() {
	cancel(true);
}

bool mtpFileLoader::setWebFileSizeHook(int size) {
	if (!_size || _size == size) {
		_size = size;
		return true;
	}
	LOG(("MTP Error: "
		"Bad size provided by bot for webDocument: %1, real: %2"
		).arg(_size
		).arg(size));
	cancel(true);
	return false;
}

void mtpFileLoader::startLoading() {
	addToQueue();
}

void mtpFileLoader::cancelHook() {
	cancelAllRequests();
}

Storage::Cache::Key mtpFileLoader::cacheKey() const {
	return location().data.match([&](const WebFileLocation &location) {
		return Data::WebDocumentCacheKey(location);
	}, [&](const GeoPointLocation &location) {
		return Data::GeoPointCacheKey(location);
	}, [&](const StorageFileLocation &location) {
		return location.cacheKey();
	});
}

std::optional<MediaKey> mtpFileLoader::fileLocationKey() const {
	if (_locationType != UnknownFileLocation) {
		return mediaKey(_locationType, dcId(), objId());
	}
	return std::nullopt;
}
