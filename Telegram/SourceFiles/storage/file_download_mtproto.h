/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/file_download.h"
#include "storage/download_manager_mtproto.h"

class mtpFileLoader final
	: public FileLoader
	, private Storage::DownloadMtprotoTask {
public:
	mtpFileLoader(
		not_null<Main::Session*> session,
		const StorageFileLocation &location,
		Data::FileOrigin origin,
		LocationType type,
		const QString &toFile,
		int32 size,
		LoadToCacheSetting toCache,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	mtpFileLoader(
		not_null<Main::Session*> session,
		const WebFileLocation &location,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	mtpFileLoader(
		not_null<Main::Session*> session,
		const GeoPointLocation &location,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	~mtpFileLoader();

	Data::FileOrigin fileOrigin() const override;
	uint64 objId() const override;

private:
	Storage::Cache::Key cacheKey() const override;
	std::optional<MediaKey> fileLocationKey() const override;
	void startLoading() override;
	void startLoadingWithData(const QByteArray &data) override;
	void cancelHook() override;

	bool readyToRequest() const override;
	int takeNextRequestOffset() override;
	bool feedPart(int offset, const QByteArray &bytes) override;
	void cancelOnFail() override;
	bool setWebFileSizeHook(int size) override;

	bool _lastComplete = false;
	int32 _nextRequestOffset = 0;

};
