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
#include "data/data_file_origin.h"
#include "mtproto/facade.h"

#include <QtNetwork/QNetworkReply>

class ApiWrap;
class FileLoader;

namespace Main {
class Session;
} // namespace Main

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache

// This value is used in local cache database settings!
constexpr auto kMaxFileInMemory = 10 * 1024 * 1024; // 10 MB max file could be hold in memory

constexpr auto kMaxVoiceInMemory = 2 * 1024 * 1024; // 2 MB audio is hold in memory and auto loaded
constexpr auto kMaxStickerInMemory = 2 * 1024 * 1024; // 2 MB stickers hold in memory, auto loaded and displayed inline
constexpr auto kMaxWallPaperInMemory = kMaxFileInMemory;
constexpr auto kMaxAnimationInMemory = kMaxFileInMemory; // 10 MB gif and mp4 animations held in memory while playing
constexpr auto kMaxWallPaperDimension = 4096; // 4096x4096 is max area.

class Downloader final : public base::has_weak_ptr {
public:
	explicit Downloader(not_null<ApiWrap*> api);
	~Downloader();

	[[nodiscard]] ApiWrap &api() const {
		return *_api;
	}

	void enqueue(not_null<FileLoader*> loader);
	void remove(not_null<FileLoader*> loader);

	[[nodiscard]] base::Observable<void> &taskFinished() {
		return _taskFinishedObservable;
	}

	// dcId == 0 is for web requests.
	void requestedAmountIncrement(MTP::DcId dcId, int index, int amount);
	[[nodiscard]] int chooseDcIndexForRequest(MTP::DcId dcId);

private:
	class Queue final {
	public:
		void enqueue(not_null<FileLoader*> loader);
		void remove(not_null<FileLoader*> loader);
		void resetGeneration();
		[[nodiscard]] FileLoader *nextLoader() const;

	private:
		std::vector<not_null<FileLoader*>> _loaders;
		std::vector<not_null<FileLoader*>> _previousGeneration;

	};

	void checkSendNext();

	void killDownloadSessionsStart(MTP::DcId dcId);
	void killDownloadSessionsStop(MTP::DcId dcId);
	void killDownloadSessions();

	void resetGeneration();

	const not_null<ApiWrap*> _api;

	base::Observable<void> _taskFinishedObservable;

	using RequestedInDc = std::array<int64, MTP::kDownloadSessionsCount>;
	base::flat_map<MTP::DcId, RequestedInDc> _requestedBytesAmount;

	base::flat_map<MTP::DcId, crl::time> _killDownloadSessionTimes;
	base::Timer _killDownloadSessionsTimer;

	base::flat_map<MTP::DcId, Queue> _mtprotoLoaders;
	Queue _webLoaders;
	bool _resettingGeneration = false;

};

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
		MTP::DcId dcId,
		int32 size,
		LocationType locationType,
		LoadToCacheSetting toCache,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);

	Main::Session &session() const;

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
	virtual Data::FileOrigin fileOrigin() const;
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
	friend class Storage::Downloader;

	enum class LocalStatus {
		NotTried,
		NotFound,
		Loading,
		Loaded,
	};

	[[nodiscard]] MTP::DcId dcId() const {
		return _dcId;
	}

	void readImage(const QSize &shrinkBox) const;

	bool tryLoadLocal();
	void loadLocal(const Storage::Cache::Key &key);
	virtual Storage::Cache::Key cacheKey() const = 0;
	virtual std::optional<MediaKey> fileLocationKey() const = 0;
	virtual void cancelRequests() = 0;

	void cancel(bool failed);

	void notifyAboutProgress();
	[[nodiscard]] virtual bool readyToRequest() const = 0;
	virtual void loadPart(int dcIndex) = 0;

	bool writeResultPart(int offset, bytes::const_span buffer);
	bool finalizeResult();
	[[nodiscard]] QByteArray readLoadedPartBack(int offset, int size);

	const MTP::DcId _dcId = 0;
	const not_null<Storage::Downloader*> _downloader;

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

class StorageImageLocation;
class WebFileLocation;
class mtpFileLoader final : public FileLoader, public RPCSender {
public:
	mtpFileLoader(
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
		const WebFileLocation &location,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	mtpFileLoader(
		const GeoPointLocation &location,
		int32 size,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);

	Data::FileOrigin fileOrigin() const override;

	uint64 objId() const override;

	void stop() override {
		rpcInvalidate();
	}
	void refreshFileReferenceFrom(
		const Data::UpdatedFileReferences &updates,
		int requestId,
		const QByteArray &current);

	~mtpFileLoader();

private:
	friend class Downloader;

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
	Storage::Cache::Key cacheKey() const override;
	std::optional<MediaKey> fileLocationKey() const override;
	void cancelRequests() override;

	[[nodiscard]] RequestData prepareRequest(int offset, int dcIndex) const;
	void makeRequest(int offset, int dcIndex);
	void makeRequest(int offset);

	bool readyToRequest() const override;
	void loadPart(int dcIndex) override;
	void normalPartLoaded(const MTPupload_File &result, mtpRequestId requestId);
	void webPartLoaded(const MTPupload_WebFile &result, mtpRequestId requestId);
	void cdnPartLoaded(const MTPupload_CdnFile &result, mtpRequestId requestId);
	void reuploadDone(const MTPVector<MTPFileHash> &result, mtpRequestId requestId);
	void requestMoreCdnFileHashes();
	void getCdnFileHashesDone(const MTPVector<MTPFileHash> &result, mtpRequestId requestId);

	void partLoaded(int offset, bytes::const_span buffer);
	bool feedPart(int offset, bytes::const_span buffer);

	bool partFailed(const RPCError &error, mtpRequestId requestId);
	bool normalPartFailed(QByteArray fileReference, const RPCError &error, mtpRequestId requestId);
	bool cdnPartFailed(const RPCError &error, mtpRequestId requestId);

	mtpRequestId sendRequest(const RequestData &requestData);
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
	int32 _nextRequestOffset = 0;

	base::variant<
		StorageFileLocation,
		WebFileLocation,
		GeoPointLocation> _location;
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

class webFileLoader final : public FileLoader {
public:
	webFileLoader(
		const QString &url,
		const QString &to,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);

	int currentOffset() const override;

	void loadProgress(qint64 already, qint64 size);
	void loadFinished(const QByteArray &data);
	void loadError();

	void stop() override {
		cancelRequests();
	}

	~webFileLoader();

private:
	void cancelRequests() override;
	Storage::Cache::Key cacheKey() const override;
	std::optional<MediaKey> fileLocationKey() const override;
	bool readyToRequest() const override;
	void loadPart(int dcIndex) override;

	void markAsSent();
	void markAsNotSent();

	QString _url;

	bool _requestSent = false;
	int32 _already = 0;

	friend class WebLoadManager;
	webFileLoaderPrivate *_private = nullptr;

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

static WebLoadManager * const FinishedWebLoadManager = SharedMemoryLocation<WebLoadManager, 0>();

void stopWebLoadManager();
