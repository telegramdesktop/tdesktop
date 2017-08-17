/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "base/observer.h"
#include "storage/localimageloader.h" // for TaskId

namespace Storage {

constexpr auto kMaxFileInMemory = 10 * 1024 * 1024; // 10 MB max file could be hold in memory
constexpr auto kMaxVoiceInMemory = 2 * 1024 * 1024; // 2 MB audio is hold in memory and auto loaded
constexpr auto kMaxStickerInMemory = 2 * 1024 * 1024; // 2 MB stickers hold in memory, auto loaded and displayed inline
constexpr auto kMaxAnimationInMemory = kMaxFileInMemory; // 10 MB gif and mp4 animations held in memory while playing

class Downloader final {
public:
	Downloader();

	int currentPriority() const {
		return _priority;
	}
	void clearPriorities();

	void delayedDestroyLoader(std::unique_ptr<FileLoader> loader);

	base::Observable<void> &taskFinished() {
		return _taskFinishedObservable;
	}

	void requestedAmountIncrement(MTP::DcId dcId, int index, int amount);
	int chooseDcIndexForRequest(MTP::DcId dcId) const;

	~Downloader();

private:
	base::Observable<void> _taskFinishedObservable;
	int _priority = 1;

	SingleQueuedInvokation _delayedLoadersDestroyer;
	std::vector<std::unique_ptr<FileLoader>> _delayedDestroyedLoaders;

	using RequestedInDc = std::array<int64, MTP::kDownloadSessionsCount>;
	std::map<MTP::DcId, RequestedInDc> _requestedBytesAmount;

};

} // namespace Storage

struct StorageImageSaved {
	StorageImageSaved() = default;
	explicit StorageImageSaved(const QByteArray &data) : data(data) {
	}

	QByteArray data;

};

enum LocalLoadStatus {
	LocalNotTried,
	LocalNotFound,
	LocalLoading,
	LocalLoaded,
	LocalFailed,
};

class mtpFileLoader;
class webFileLoader;

struct FileLoaderQueue;
class FileLoader : public QObject {
	Q_OBJECT

public:
	FileLoader(const QString &toFile, int32 size, LocationType locationType, LoadToCacheSetting, LoadFromCloudSetting fromCloud, bool autoLoading);
	bool finished() const {
		return _finished;
	}
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
	QPixmap imagePixmap(const QSize &shrinkBox = QSize()) const;
	QString fileName() const {
		return _filename;
	}
	float64 currentProgress() const;
	virtual int32 currentOffset(bool includeSkipped = false) const = 0;
	int32 fullSize() const;

	bool setFileName(const QString &filename); // set filename for loaders to cache
	void permitLoadFromCloud();

	void pause();
	void start(bool loadFirst = false, bool prior = true);
	void cancel();

	bool loading() const {
		return _inQueue;
	}
	bool paused() const {
		return _paused;
	}
	bool started() const {
		return _inQueue || _paused;
	}
	bool loadingLocal() const {
		return (_localStatus == LocalLoading);
	}
	bool autoLoading() const {
		return _autoLoading;
	}

	virtual void stop() {
	}
	virtual ~FileLoader();

	void localLoaded(const StorageImageSaved &result, const QByteArray &imageFormat = QByteArray(), const QPixmap &imagePixmap = QPixmap());

signals:
	void progress(FileLoader *loader);
	void failed(FileLoader *loader, bool started);

protected:
	void readImage(const QSize &shrinkBox) const;

	not_null<Storage::Downloader*> _downloader;
	FileLoader *_prev = nullptr;
	FileLoader *_next = nullptr;
	int _priority = 0;
	FileLoaderQueue *_queue = nullptr;

	bool _paused = false;
	bool _autoLoading = false;
	bool _inQueue = false;
	bool _finished = false;
	bool _cancelled = false;
	mutable LocalLoadStatus _localStatus = LocalNotTried;

	virtual bool tryLoadLocal() = 0;
	virtual void cancelRequests() = 0;

	void startLoading(bool loadFirst, bool prior);
	void removeFromQueue();
	void cancel(bool failed);

	void loadNext();
	virtual bool loadPart() = 0;

	QString _filename;
	QFile _file;
	bool _fileIsOpen = false;

	LoadToCacheSetting _toCache;
	LoadFromCloudSetting _fromCloud;

	QByteArray _data;

	int32 _size;
	LocationType _locationType;

	TaskId _localTaskId = 0;
	mutable QByteArray _imageFormat;
	mutable QPixmap _imagePixmap;

};

class StorageImageLocation;
class WebFileImageLocation;
class mtpFileLoader : public FileLoader, public RPCSender {
	Q_OBJECT

public:
	mtpFileLoader(const StorageImageLocation *location, int32 size, LoadFromCloudSetting fromCloud, bool autoLoading);
	mtpFileLoader(int32 dc, uint64 id, uint64 accessHash, int32 version, LocationType type, const QString &toFile, int32 size, LoadToCacheSetting toCache, LoadFromCloudSetting fromCloud, bool autoLoading);
	mtpFileLoader(const WebFileImageLocation *location, int32 size, LoadFromCloudSetting fromCloud, bool autoLoading);

	int32 currentOffset(bool includeSkipped = false) const override;

	uint64 objId() const override {
		return _id;
	}

	void stop() override {
		rpcInvalidate();
	}

	~mtpFileLoader();

private:
	struct RequestData {
		MTP::DcId dcId = 0;
		int dcIndex = 0;
		int offset = 0;
	};
	struct CdnFileHash {
		CdnFileHash(int limit, QByteArray hash) : limit(limit), hash(hash) {
		}
		int limit = 0;
		QByteArray hash;
	};

	bool tryLoadLocal() override;
	void cancelRequests() override;

	int partSize() const;
	RequestData prepareRequest(int offset) const;
	void makeRequest(int offset);

	bool loadPart() override;
	void normalPartLoaded(const MTPupload_File &result, mtpRequestId requestId);
	void webPartLoaded(const MTPupload_WebFile &result, mtpRequestId requestId);
	void cdnPartLoaded(const MTPupload_CdnFile &result, mtpRequestId requestId);
	void reuploadDone(const MTPVector<MTPCdnFileHash> &result, mtpRequestId requestId);
	void requestMoreCdnFileHashes();
	void getCdnFileHashesDone(const MTPVector<MTPCdnFileHash> &result, mtpRequestId requestId);

	void partLoaded(int offset, base::const_byte_span bytes);
	bool partFailed(const RPCError &error);
	bool cdnPartFailed(const RPCError &error, mtpRequestId requestId);

	void placeSentRequest(mtpRequestId requestId, const RequestData &requestData);
	int finishSentRequestGetOffset(mtpRequestId requestId);
	void switchToCDN(int offset, const MTPDupload_fileCdnRedirect &redirect);
	void addCdnHashes(const QVector<MTPCdnFileHash> &hashes);
	void changeCDNParams(int offset, MTP::DcId dcId, const QByteArray &token, const QByteArray &encryptionKey, const QByteArray &encryptionIV, const QVector<MTPCdnFileHash> &hashes);

	enum class CheckCdnHashResult {
		NoHash,
		Invalid,
		Good,
	};
	CheckCdnHashResult checkCdnFileHash(int offset, base::const_byte_span bytes);

	std::map<mtpRequestId, RequestData> _sentRequests;

	bool _lastComplete = false;
	int32 _skippedBytes = 0;
	int32 _nextRequestOffset = 0;

	MTP::DcId _dcId = 0; // for photo locations
	const StorageImageLocation *_location = nullptr;

	uint64 _id = 0; // for document locations
	uint64 _accessHash = 0;
	int32 _version = 0;

	const WebFileImageLocation *_urlLocation = nullptr; // for webdocument locations

	MTP::DcId _cdnDcId = 0;
	QByteArray _cdnToken;
	QByteArray _cdnEncryptionKey;
	QByteArray _cdnEncryptionIV;
	std::map<int, CdnFileHash> _cdnFileHashes;
	std::map<int, QByteArray> _cdnUncheckedParts;
	mtpRequestId _cdnHashesRequestId = 0;

};

class webFileLoaderPrivate;

class webFileLoader : public FileLoader {
	Q_OBJECT

public:

	webFileLoader(const QString &url, const QString &to, LoadFromCloudSetting fromCloud, bool autoLoading);

	virtual int32 currentOffset(bool includeSkipped = false) const;
	virtual webFileLoader *webLoader() {
		return this;
	}
	virtual const webFileLoader *webLoader() const {
		return this;
	}

	void onProgress(qint64 already, qint64 size);
	void onFinished(const QByteArray &data);
	void onError();

	virtual void stop() {
		cancelRequests();
	}

	~webFileLoader();

protected:

	virtual void cancelRequests();
	virtual bool tryLoadLocal();
	virtual bool loadPart();

	QString _url;

	bool _requestSent;
	int32 _already;

	friend class WebLoadManager;
	webFileLoaderPrivate *_private;

};

enum WebReplyProcessResult {
	WebReplyProcessError,
	WebReplyProcessProgress,
	WebReplyProcessFinished,
};

class WebLoadManager : public QObject {
	Q_OBJECT

public:
	WebLoadManager(QThread *thread);

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	void setProxySettings(const QNetworkProxy &proxy);
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

	void append(webFileLoader *loader, const QString &url);
	void stop(webFileLoader *reader);
	bool carries(webFileLoader *reader) const;

	~WebLoadManager();

signals:
	void processDelayed();
	void proxyApplyDelayed();

	void progress(webFileLoader *loader, qint64 already, qint64 size);
	void finished(webFileLoader *loader, QByteArray data);
	void error(webFileLoader *loader);

public slots:
	void onFailed(QNetworkReply *reply);
	void onFailed(QNetworkReply::NetworkError error);
	void onProgress(qint64 already, qint64 size);
	void onMeta();

	void process();
	void proxyApply();
	void finish();

private:
	void clear();
	void sendRequest(webFileLoaderPrivate *loader, const QString &redirect = QString());
	bool handleReplyResult(webFileLoaderPrivate *loader, WebReplyProcessResult result);

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	QNetworkProxy _proxySettings;
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	QNetworkAccessManager _manager;
	typedef QMap<webFileLoader*, webFileLoaderPrivate*> LoaderPointers;
	LoaderPointers _loaderPointers;
	mutable QMutex _loaderPointersMutex;

	typedef OrderedSet<webFileLoaderPrivate*> Loaders;
	Loaders _loaders;

	typedef QMap<QNetworkReply*, webFileLoaderPrivate*> Replies;
	Replies _replies;

};

class WebLoadMainManager : public QObject {
	Q_OBJECT

public:

public slots:

	void progress(webFileLoader *loader, qint64 already, qint64 size);
	void finished(webFileLoader *loader, QByteArray data);
	void error(webFileLoader *loader);

};

static FileLoader * const CancelledFileLoader = SharedMemoryLocation<FileLoader, 0>();
static mtpFileLoader * const CancelledMtpFileLoader = static_cast<mtpFileLoader*>(CancelledFileLoader);
static webFileLoader * const CancelledWebFileLoader = static_cast<webFileLoader*>(CancelledFileLoader);
static WebLoadManager * const FinishedWebLoadManager = SharedMemoryLocation<WebLoadManager, 0>();

void reinitWebLoadManager();
void stopWebLoadManager();
