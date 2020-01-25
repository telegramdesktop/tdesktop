/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/observer.h"
#include "base/timer.h"
#include "base/binary_guard.h"

#include <QtNetwork/QNetworkReply>

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache

// 10 MB max file could be hold in memory
// This value is used in local cache database settings!
constexpr auto kMaxFileInMemory = 10 * 1024 * 1024;

// 2 MB audio is hold in memory and auto loaded
constexpr auto kMaxVoiceInMemory = 2 * 1024 * 1024;

// 2 MB stickers hold in memory, auto loaded and displayed inline
constexpr auto kMaxStickerInMemory = 2 * 1024 * 1024;

// 10 MB GIF and mp4 animations held in memory while playing
constexpr auto kMaxWallPaperInMemory = kMaxFileInMemory;
constexpr auto kMaxAnimationInMemory = kMaxFileInMemory;

// 4096x4096 is max area.
constexpr auto kMaxWallPaperDimension = 4096;

} // namespace Storage

struct StorageImageSaved {
	StorageImageSaved() = default;
	explicit StorageImageSaved(const QByteArray &data) : data(data) {
	}

	QByteArray data;

};

class FileLoader : public QObject {
	Q_OBJECT

public:
	FileLoader(
		const QString &toFile,
		int32 size,
		LocationType locationType,
		LoadToCacheSetting toCache,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	virtual ~FileLoader();

	[[nodiscard]] Main::Session &session() const;

	bool finished() const {
		return _finished;
	}
	void finishWithBytes(const QByteArray &data);
	bool cancelled() const {
		return _cancelled;
	}
	const QByteArray &bytes() const {
		return _data;
	}
	virtual uint64 objId() const {
		return 0;
	}
	QByteArray imageFormat(const QSize &shrinkBox = QSize()) const;
	QImage imageData(const QSize &shrinkBox = QSize()) const;
	QString fileName() const {
		return _filename;
	}
	// Used in MainWidget::documentLoadFailed.
	[[nodiscard]] virtual Data::FileOrigin fileOrigin() const;
	float64 currentProgress() const;
	virtual int currentOffset() const;
	int fullSize() const;

	bool setFileName(const QString &filename); // set filename for loaders to cache
	void permitLoadFromCloud();

	void start();
	void cancel();

	bool loadingLocal() const {
		return (_localStatus == LocalStatus::Loading);
	}
	bool autoLoading() const {
		return _autoLoading;
	}

	void localLoaded(
		const StorageImageSaved &result,
		const QByteArray &imageFormat,
		const QImage &imageData);

signals:
	void progress(FileLoader *loader);
	void failed(FileLoader *loader, bool started);

protected:
	enum class LocalStatus {
		NotTried,
		NotFound,
		Loading,
		Loaded,
	};

	void readImage(const QSize &shrinkBox) const;

	bool tryLoadLocal();
	void loadLocal(const Storage::Cache::Key &key);
	virtual Storage::Cache::Key cacheKey() const = 0;
	virtual std::optional<MediaKey> fileLocationKey() const = 0;
	virtual void cancelHook() = 0;
	virtual void startLoading() = 0;

	void cancel(bool failed);

	void notifyAboutProgress();

	bool writeResultPart(int offset, bytes::const_span buffer);
	bool finalizeResult();
	[[nodiscard]] QByteArray readLoadedPartBack(int offset, int size);

	const not_null<Main::Session*> _session;

	bool _autoLoading = false;
	uint8 _cacheTag = 0;
	bool _finished = false;
	bool _cancelled = false;
	mutable LocalStatus _localStatus = LocalStatus::NotTried;

	QString _filename;
	QFile _file;
	bool _fileIsOpen = false;

	LoadToCacheSetting _toCache;
	LoadFromCloudSetting _fromCloud;

	QByteArray _data;

	int _size = 0;
	int _skippedBytes = 0;
	LocationType _locationType = LocationType();

	base::binary_guard _localLoading;
	mutable QByteArray _imageFormat;
	mutable QImage _imageData;

};
