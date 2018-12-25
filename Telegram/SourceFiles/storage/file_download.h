/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/observer.h"
#include "data/data_file_origin.h"
#include "base/binary_guard.h"

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache

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

	base::Observable<void> &taskFinished() {
		return _taskFinishedObservable;
	}

	void requestedAmountIncrement(MTP::DcId dcId, int index, int amount);
	int chooseDcIndexForRequest(MTP::DcId dcId) const;

	~Downloader();

private:
	base::Observable<void> _taskFinishedObservable;
	int _priority = 1;

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

class mtpFileLoader;
class webFileLoader;

struct FileLoaderQueue;
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
	virtual Data::FileOrigin fileOrigin() const {
		return Data::FileOrigin();
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
		return (_localStatus == LocalStatus::Loading);
	}
	bool autoLoading() const {
		return _autoLoading;
	}

	virtual void stop() {
	}
	virtual ~FileLoader();

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
	virtual std::optional<Storage::Cache::Key> cacheKey() const = 0;
	virtual void cancelRequests() = 0;

	void startLoading(bool loadFirst, bool prior);
	void removeFromQueue();
	void cancel(bool failed);

	void notifyAboutProgress();
	static void LoadNextFromQueue(not_null<FileLoaderQueue*> queue);
	virtual bool loadPart() = 0;

	not_null<Storage::Downloader*> _downloader;
	FileLoader *_prev = nullptr;
	FileLoader *_next = nullptr;
	int _priority = 0;
	FileLoaderQueue *_queue = nullptr;

	bool _paused = false;
	bool _autoLoading = false;
	uint8 _cacheTag = 0;
	bool _inQueue = false;
	bool _finished = false;
	bool _cancelled = false;
	mutable LocalStatus _localStatus = LocalStatus::NotTried;

	QString _filename;
	QFile _file;
	bool _fileIsOpen = false;

	LoadToCacheSetting _toCache;
	LoadFromCloudSetting _fromCloud;

	QByteArray _data;

	int32 _size;
	LocationType _locationType;

	base::binary_guard _localLoading;
	mutable QByteArray _imageFormat;
	mutable QImage _imageData;

};

class StorageImageLocation;
class WebFileLocation;
class mtpFileLoader : public FileLoader, public RPCSender {
public:
	mtpFileLoader(
		not_null<StorageImageLocation*> location,
		Data::FileOrigin origin,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	mtpFileLoader(
		int32 dc,
		uint64 id,
		uint64 accessHash,
		const QByteArray &fileReference,
		Data::FileOrigin origin,
		LocationType type,
		const QString &toFile,
		int32 size,
		LoadToCacheSetting toCache,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	mtpFileLoader(
		const WebFileLocation *location,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	mtpFileLoader(
		const GeoPointLocation *location,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);

	int32 currentOffset(bool includeSkipped = false) const override;
	Data::FileOrigin fileOrigin() const override;

	uint64 objId() const override {
		return _id;
	}

	void stop() override {
		rpcInvalidate();
	}
	void refreshFileReferenceFrom(
		const Data::UpdatedFileReferences &data,
		int requestId,
		const QByteArray &current);

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
	std::optional<Storage::Cache::Key> cacheKey() const override;
	void cancelRequests() override;

	int partSize() const;
	RequestData prepareRequest(int offset) const;
	void makeRequest(int offset);

	MTPInputFileLocation computeLocation() const;
	bool loadPart() override;
	void normalPartLoaded(const MTPupload_File &result, mtpRequestId requestId);
	void webPartLoaded(const MTPupload_WebFile &result, mtpRequestId requestId);
	void cdnPartLoaded(const MTPupload_CdnFile &result, mtpRequestId requestId);
	void reuploadDone(const MTPVector<MTPFileHash> &result, mtpRequestId requestId);
	void requestMoreCdnFileHashes();
	void getCdnFileHashesDone(const MTPVector<MTPFileHash> &result, mtpRequestId requestId);

	bool feedPart(int offset, bytes::const_span buffer);
	void partLoaded(int offset, bytes::const_span buffer);

	bool partFailed(const RPCError &error, mtpRequestId requestId);
	bool cdnPartFailed(const RPCError &error, mtpRequestId requestId);

	void placeSentRequest(mtpRequestId requestId, const RequestData &requestData);
	int finishSentRequestGetOffset(mtpRequestId requestId);
	void switchToCDN(int offset, const MTPDupload_fileCdnRedirect &redirect);
	void addCdnHashes(const QVector<MTPFileHash> &hashes);
	void changeCDNParams(int offset, MTP::DcId dcId, const QByteArray &token, const QByteArray &encryptionKey, const QByteArray &encryptionIV, const QVector<MTPFileHash> &hashes);

	enum class CheckCdnHashResult {
		NoHash,
		Invalid,
		Good,
	};
	CheckCdnHashResult checkCdnFileHash(int offset, bytes::const_span buffer);

	std::map<mtpRequestId, RequestData> _sentRequests;

	bool _lastComplete = false;
	int32 _skippedBytes = 0;
	int32 _nextRequestOffset = 0;

	MTP::DcId _dcId = 0; // for photo locations
	StorageImageLocation *_location = nullptr;

	uint64 _id = 0; // for document locations
	uint64 _accessHash = 0;
	QByteArray _fileReference;

	const WebFileLocation *_urlLocation = nullptr; // for webdocument locations
	const GeoPointLocation *_geoLocation = nullptr; // for webdocument locations

	Data::FileOrigin _origin;

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
public:
	webFileLoader(
		const QString &url,
		const QString &to,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);

	int32 currentOffset(bool includeSkipped = false) const override;

	void onProgress(qint64 already, qint64 size);
	void onFinished(const QByteArray &data);
	void onError();

	void stop() override {
		cancelRequests();
	}

	~webFileLoader();

protected:
	void cancelRequests() override;
	std::optional<Storage::Cache::Key> cacheKey() const override;
	bool loadPart() override;

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

	void append(webFileLoader *loader, const QString &url);
	void stop(webFileLoader *reader);
	bool carries(webFileLoader *reader) const;

	~WebLoadManager();

signals:
	void processDelayed();

	void progress(webFileLoader *loader, qint64 already, qint64 size);
	void finished(webFileLoader *loader, QByteArray data);
	void error(webFileLoader *loader);

public slots:
	void onFailed(QNetworkReply *reply);
	void onFailed(QNetworkReply::NetworkError error);
	void onProgress(qint64 already, qint64 size);
	void onMeta();

	void process();
	void finish();

private:
	void clear();
	void sendRequest(webFileLoaderPrivate *loader, const QString &redirect = QString());
	bool handleReplyResult(webFileLoaderPrivate *loader, WebReplyProcessResult result);

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

public slots:
	void progress(webFileLoader *loader, qint64 already, qint64 size);
	void finished(webFileLoader *loader, QByteArray data);
	void error(webFileLoader *loader);

};

static FileLoader * const CancelledFileLoader = SharedMemoryLocation<FileLoader, 0>();
static mtpFileLoader * const CancelledMtpFileLoader = static_cast<mtpFileLoader*>(CancelledFileLoader);
static webFileLoader * const CancelledWebFileLoader = static_cast<webFileLoader*>(CancelledFileLoader);
static WebLoadManager * const FinishedWebLoadManager = SharedMemoryLocation<WebLoadManager, 0>();

void stopWebLoadManager();
