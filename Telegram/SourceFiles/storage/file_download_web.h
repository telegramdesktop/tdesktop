/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/file_download.h"

class WebLoadManager;

enum class WebRequestType {
	FullLoad,
	OnlySize,
};

class webFileLoader final : public FileLoader {
public:
	webFileLoader(
		not_null<Main::Session*> session,
		const QString &url,
		const QString &to,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	webFileLoader(
		not_null<Main::Session*> session,
		const QString &url,
		const QString &path,
		WebRequestType type);
	~webFileLoader();

	[[nodiscard]] QString url() const;
	[[nodiscard]] WebRequestType requestType() const;
	[[nodiscard]] bool streamLoading() const;

	int64 currentOffset() const override;

private:
	void cancelRequest();
	void cancelHook() override;
	void startLoading() override;
	Storage::Cache::Key cacheKey() const override;
	std::optional<MediaKey> fileLocationKey() const override;

	void loadProgress(
		qint64 ready,
		qint64 size,
		const QByteArray &streamed);
	void loadFinished(const QByteArray &data);
	void loadFailed();

	const QString _url;
	int64 _ready = 0;
	int64 _streamedOffset = 0;
	WebRequestType _requestType = {};

	std::shared_ptr<WebLoadManager> _manager;
	rpl::lifetime _managerLifetime;

};
