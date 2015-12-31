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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace MTP {
	void clearLoaderPriorities();
}

enum LocationType {
	UnknownFileLocation  = 0,
	DocumentFileLocation = 0x4e45abe9, // mtpc_inputDocumentFileLocation
	AudioFileLocation    = 0x74dc404d, // mtpc_inputAudioFileLocation
	VideoFileLocation    = 0x3d0364ec, // mtpc_inputVideoFileLocation
};
inline LocationType mtpToLocationType(mtpTypeId type) {
	switch (type) {
		case mtpc_inputDocumentFileLocation: return DocumentFileLocation;
		case mtpc_inputAudioFileLocation: return AudioFileLocation;
		case mtpc_inputVideoFileLocation: return VideoFileLocation;
		default: return UnknownFileLocation;
	}
}
inline mtpTypeId mtpFromLocationType(LocationType type) {
	switch (type) {
		case DocumentFileLocation: return mtpc_inputDocumentFileLocation;
		case AudioFileLocation: return mtpc_inputAudioFileLocation;
		case VideoFileLocation: return mtpc_inputVideoFileLocation;
		case UnknownFileLocation:
		default: return 0;
	}
}

enum StorageFileType {
	StorageFileUnknown = 0xaa963b05, // mtpc_storage_fileUnknown
	StorageFileJpeg = 0x7efe0e,   // mtpc_storage_fileJpeg
	StorageFileGif = 0xcae1aadf, // mtpc_storage_fileGif
	StorageFilePng = 0xa4f63c0,  // mtpc_storage_filePng
	StorageFilePdf = 0xae1e508d, // mtpc_storage_filePdf
	StorageFileMp3 = 0x528a0677, // mtpc_storage_fileMp3
	StorageFileMov = 0x4b09ebbc, // mtpc_storage_fileMov
	StorageFilePartial = 0x40bc6f52, // mtpc_storage_filePartial
	StorageFileMp4 = 0xb3cea0e4, // mtpc_storage_fileMp4
	StorageFileWebp = 0x1081464c, // mtpc_storage_fileWebp
};
inline StorageFileType mtpToStorageType(mtpTypeId type) {
	switch (type) {
	case mtpc_storage_fileJpeg: return StorageFileJpeg;
	case mtpc_storage_fileGif: return StorageFileGif;
	case mtpc_storage_filePng: return StorageFilePng;
	case mtpc_storage_filePdf: return StorageFilePdf;
	case mtpc_storage_fileMp3: return StorageFileMp3;
	case mtpc_storage_fileMov: return StorageFileMov;
	case mtpc_storage_filePartial: return StorageFilePartial;
	case mtpc_storage_fileMp4: return StorageFileMp4;
	case mtpc_storage_fileWebp: return StorageFileWebp;
	case mtpc_storage_fileUnknown:
	default: return StorageFileUnknown;
	}
}
inline mtpTypeId mtpFromStorageType(StorageFileType type) {
	switch (type) {
	case StorageFileGif: return mtpc_storage_fileGif;
	case StorageFilePng: return mtpc_storage_filePng;
	case StorageFilePdf: return mtpc_storage_filePdf;
	case StorageFileMp3: return mtpc_storage_fileMp3;
	case StorageFileMov: return mtpc_storage_fileMov;
	case StorageFilePartial: return mtpc_storage_filePartial;
	case StorageFileMp4: return mtpc_storage_fileMp4;
	case StorageFileWebp: return mtpc_storage_fileWebp;
	case StorageFileUnknown:
	default: return mtpc_storage_fileUnknown;
	}
}
struct StorageImageSaved {
	StorageImageSaved() : type(StorageFileUnknown) {
	}
	StorageImageSaved(StorageFileType type, const QByteArray &data) : type(type), data(data) {
	}
	StorageFileType type;
	QByteArray data;
};

enum LocalLoadStatus {
	LocalNotTried,
	LocalNotFound,
	LocalLoading,
	LocalLoaded,
	LocalFailed,
};

typedef void *TaskId; // no interface, just id

enum LoadFromCloudSetting {
	LoadFromCloudOrLocal,
	LoadFromLocalOnly,
};
enum LoadToCacheSetting {
	LoadToFileOnly,
	LoadToCacheAsWell,
};

class mtpFileLoader;
class webFileLoader;

struct FileLoaderQueue;
class FileLoader : public QObject {
	Q_OBJECT

public:

	FileLoader(const QString &toFile, int32 size, LocationType locationType, LoadToCacheSetting, LoadFromCloudSetting fromCloud, bool autoLoading);
	bool done() const {
		return _complete;
	}
	mtpTypeId fileType() const {
		return _type;
	}
	const QByteArray &bytes() const {
		return _data;
	}
	QByteArray imageFormat() const;
	QPixmap imagePixmap() const;
	QString fileName() const {
		return _fname;
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

	virtual mtpFileLoader *mtpLoader() {
		return 0;
	}
	virtual const mtpFileLoader *mtpLoader() const {
		return 0;
	}
	virtual webFileLoader *webLoader() {
		return 0;
	}
	virtual const webFileLoader *webLoader() const {
		return 0;
	}
	virtual void stop() {
	}
	virtual ~FileLoader();

	void localLoaded(const StorageImageSaved &result, const QByteArray &imageFormat = QByteArray(), const QPixmap &imagePixmap = QPixmap());

signals:

	void progress(FileLoader *loader);
	void failed(FileLoader *loader, bool started);

protected:

	FileLoader *_prev, *_next;
	int32 _priority;
	FileLoaderQueue *_queue;

	bool _paused, _autoLoading, _inQueue, _complete;
	mutable LocalLoadStatus _localStatus;

	virtual bool tryLoadLocal() = 0;
	virtual void cancelRequests() = 0;

	void startLoading(bool loadFirst, bool prior);
	void removeFromQueue();
	void cancel(bool failed);

	void loadNext();
	virtual bool loadPart() = 0;

	QFile _file;
	QString _fname;
	bool _fileIsOpen;

	LoadToCacheSetting _toCache;
	LoadFromCloudSetting _fromCloud;

	QByteArray _data;

	int32 _size;
	mtpTypeId _type;
	LocationType _locationType;

	TaskId _localTaskId;
	mutable QByteArray _imageFormat;
	mutable QPixmap _imagePixmap;
	void readImage() const;

};

class StorageImageLocation;
class mtpFileLoader : public FileLoader, public RPCSender {
	Q_OBJECT

public:

	mtpFileLoader(const StorageImageLocation *location, int32 size, LoadFromCloudSetting fromCloud, bool autoLoading);
	mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, LocationType type, const QString &toFile, int32 size, LoadToCacheSetting toCache, LoadFromCloudSetting fromCloud, bool autoLoading);

	virtual int32 currentOffset(bool includeSkipped = false) const;

	uint64 objId() const {
		return _id;
	}

	virtual mtpFileLoader *mtpLoader() {
		return this;
	}
	virtual const mtpFileLoader *mtpLoader() const {
		return this;
	}

	virtual void stop() {
		rpcInvalidate();
	}

	~mtpFileLoader();

protected:

	virtual bool tryLoadLocal();
	virtual void cancelRequests();

	typedef QMap<mtpRequestId, int32> Requests;
	Requests _requests;

	virtual bool loadPart();
	void partLoaded(int32 offset, const MTPupload_File &result, mtpRequestId req);
	bool partFailed(const RPCError &error);

	bool _lastComplete;
	int32 _skippedBytes;
	int32 _nextRequestOffset;

	int32 _dc;
	const StorageImageLocation *_location;

	uint64 _id; // for other locations
	uint64 _access;

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

	void setProxySettings(const QNetworkProxy &proxy);

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

	QNetworkProxy _proxySettings;
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
