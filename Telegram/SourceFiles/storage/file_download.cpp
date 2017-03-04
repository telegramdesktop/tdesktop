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
#include "storage/file_download.h"

#include "mainwidget.h"
#include "mainwindow.h"
#include "messenger.h"
#include "storage/localstorage.h"
#include "platform/platform_file_utilities.h"
#include "auth_session.h"

namespace Storage {

void Downloader::clearPriorities() {
	++_priority;
}

} // namespace Storage

namespace {

struct DataRequested {
	DataRequested() {
		memset(v, 0, sizeof(v));
	}
	int64 v[MTPDownloadSessionsCount];
};
QMap<int32, DataRequested> DataRequestedMap;

} // namespace

struct FileLoaderQueue {
	FileLoaderQueue(int32 limit) : limit(limit) {
	}
	int queries = 0;
	int limit = 0;
	FileLoader *start = nullptr;
	FileLoader *end = nullptr;
};

namespace {
	typedef QMap<int32, FileLoaderQueue> LoaderQueues;
	LoaderQueues queues;

	FileLoaderQueue _webQueue(MaxWebFileQueries);

	QThread *_webLoadThread = 0;
	WebLoadManager *_webLoadManager = 0;
	WebLoadManager *webLoadManager() {
		return (_webLoadManager && _webLoadManager != FinishedWebLoadManager) ? _webLoadManager : 0;
	}
	WebLoadMainManager *_webLoadMainManager = 0;
}

FileLoader::FileLoader(const QString &toFile, int32 size, LocationType locationType, LoadToCacheSetting toCache, LoadFromCloudSetting fromCloud, bool autoLoading)
: _downloader(AuthSession::Current().downloader())
, _autoLoading(autoLoading)
, _file(toFile)
, _fname(toFile)
, _toCache(toCache)
, _fromCloud(fromCloud)
, _size(size)
, _locationType(locationType) {
}

QByteArray FileLoader::imageFormat(const QSize &shrinkBox) const {
	if (_imageFormat.isEmpty() && _locationType == UnknownFileLocation) {
		readImage(shrinkBox);
	}
	return _imageFormat;
}

QPixmap FileLoader::imagePixmap(const QSize &shrinkBox) const {
	if (_imagePixmap.isNull() && _locationType == UnknownFileLocation) {
		readImage(shrinkBox);
	}
	return _imagePixmap;
}

void FileLoader::readImage(const QSize &shrinkBox) const {
	auto format = QByteArray();
	auto image = App::readImage(_data, &format, false);
	if (!image.isNull()) {
		if (!shrinkBox.isEmpty() && (image.width() > shrinkBox.width() || image.height() > shrinkBox.height())) {
			_imagePixmap = App::pixmapFromImageInPlace(image.scaled(shrinkBox, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		} else {
			_imagePixmap = App::pixmapFromImageInPlace(std::move(image));
		}
		_imageFormat = format;
	}
}

float64 FileLoader::currentProgress() const {
	if (_finished) return 1.;
	if (!fullSize()) return 0.;
	return snap(float64(currentOffset()) / fullSize(), 0., 1.);
}

int32 FileLoader::fullSize() const {
	return _size;
}

bool FileLoader::setFileName(const QString &fileName) {
	if (_toCache != LoadToCacheAsWell || !_fname.isEmpty()) {
		return fileName.isEmpty() || (fileName == _fname);
	}
	_fname = fileName;
	_file.setFileName(_fname);
	return true;
}

void FileLoader::permitLoadFromCloud() {
	_fromCloud = LoadFromCloudOrLocal;
}

void FileLoader::loadNext() {
	if (_queue->queries >= _queue->limit) return;
	for (FileLoader *i = _queue->start; i;) {
		if (i->loadPart()) {
			if (_queue->queries >= _queue->limit) return;
		} else {
			i = i->_next;
		}
	}
}

void FileLoader::removeFromQueue() {
	if (!_inQueue) return;
	if (_next) {
		_next->_prev = _prev;
	}
	if (_prev) {
		_prev->_next = _next;
	}
	if (_queue->end == this) {
		_queue->end = _prev;
	}
	if (_queue->start == this) {
		_queue->start = _next;
	}
	_next = _prev = 0;
	_inQueue = false;
}

void FileLoader::pause() {
	removeFromQueue();
	_paused = true;
}

FileLoader::~FileLoader() {
	if (_localTaskId) {
		Local::cancelTask(_localTaskId);
	}
	removeFromQueue();
}

void FileLoader::localLoaded(const StorageImageSaved &result, const QByteArray &imageFormat, const QPixmap &imagePixmap) {
	_localTaskId = 0;
	if (result.data.isEmpty()) {
		_localStatus = LocalFailed;
		start(true);
		return;
	}
	_data = result.data;
	if (!imagePixmap.isNull()) {
		_imageFormat = imageFormat;
		_imagePixmap = imagePixmap;
	}
	_localStatus = LocalLoaded;
	if (!_fname.isEmpty() && _toCache == LoadToCacheAsWell) {
		if (!_fileIsOpen) _fileIsOpen = _file.open(QIODevice::WriteOnly);
		if (!_fileIsOpen) {
			cancel(true);
			return;
		}
		if (_file.write(_data) != qint64(_data.size())) {
			cancel(true);
			return;
		}
	}

	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		Platform::File::PostprocessDownloaded(QFileInfo(_file).absoluteFilePath());
	}
	_downloader->taskFinished().notify();

	emit progress(this);

	loadNext();
}

void FileLoader::start(bool loadFirst, bool prior) {
	if (_paused) {
		_paused = false;
	}
	if (_finished || tryLoadLocal()) return;

	if (_fromCloud == LoadFromLocalOnly) {
		cancel();
		return;
	}

	if (!_fname.isEmpty() && _toCache == LoadToFileOnly && !_fileIsOpen) {
		_fileIsOpen = _file.open(QIODevice::WriteOnly);
		if (!_fileIsOpen) {
			return cancel(true);
		}
	}

	auto currentPriority = _downloader->currentPriority();
	FileLoader *before = 0, *after = 0;
	if (prior) {
		if (_inQueue && _priority == currentPriority) {
			if (loadFirst) {
				if (!_prev) return startLoading(loadFirst, prior);
				before = _queue->start;
			} else {
				if (!_next || _next->_priority < currentPriority) return startLoading(loadFirst, prior);
				after = _next;
				while (after->_next && after->_next->_priority == currentPriority) {
					after = after->_next;
				}
			}
		} else {
			_priority = currentPriority;
			if (loadFirst) {
				if (_inQueue && !_prev) return startLoading(loadFirst, prior);
				before = _queue->start;
			} else {
				if (_inQueue) {
					if (_next && _next->_priority == currentPriority) {
						after = _next;
					} else if (_prev && _prev->_priority < currentPriority) {
						before = _prev;
						while (before->_prev && before->_prev->_priority < currentPriority) {
							before = before->_prev;
						}
					} else {
						return startLoading(loadFirst, prior);
					}
				} else {
					if (_queue->start && _queue->start->_priority == currentPriority) {
						after = _queue->start;
					} else {
						before = _queue->start;
					}
				}
				if (after) {
					while (after->_next && after->_next->_priority == currentPriority) {
						after = after->_next;
					}
				}
			}
		}
	} else {
		if (loadFirst) {
			if (_inQueue && (!_prev || _prev->_priority == currentPriority)) return startLoading(loadFirst, prior);
			before = _prev;
			while (before->_prev && before->_prev->_priority != currentPriority) {
				before = before->_prev;
			}
		} else {
			if (_inQueue && !_next) return startLoading(loadFirst, prior);
			after = _queue->end;
		}
	}

	removeFromQueue();

	_inQueue = true;
	if (!_queue->start) {
		_queue->start = _queue->end = this;
	} else if (before) {
		if (before != _next) {
			_prev = before->_prev;
			_next = before;
			_next->_prev = this;
			if (_prev) {
				_prev->_next = this;
			}
			if (_queue->start->_prev) _queue->start = _queue->start->_prev;
		}
	} else if (after) {
		if (after != _prev) {
			_next = after->_next;
			_prev = after;
			after->_next = this;
			if (_next) {
				_next->_prev = this;
			}
			if (_queue->end->_next) _queue->end = _queue->end->_next;
		}
	} else {
		LOG(("Queue Error: _start && !before && !after"));
	}
	return startLoading(loadFirst, prior);
}

void FileLoader::cancel() {
	cancel(false);
}

void FileLoader::cancel(bool fail) {
	bool started = currentOffset(true) > 0;
	cancelRequests();
	_cancelled = true;
	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		_file.remove();
	}
	_data = QByteArray();
	_fname = QString();
	_file.setFileName(_fname);

	if (fail) {
		emit failed(this, started);
	} else {
		emit progress(this);
	}

	loadNext();
}

void FileLoader::startLoading(bool loadFirst, bool prior) {
	if ((_queue->queries >= _queue->limit && (!loadFirst || !prior)) || _finished) return;
	loadPart();
}

mtpFileLoader::mtpFileLoader(const StorageImageLocation *location, int32 size, LoadFromCloudSetting fromCloud, bool autoLoading)
: FileLoader(QString(), size, UnknownFileLocation, LoadToCacheAsWell, fromCloud, autoLoading)
, _dc(location->dc())
, _location(location) {
	auto shiftedDcId = MTP::downloadDcId(_dc, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(MaxFileQueries));
	}
	_queue = &i.value();
}

mtpFileLoader::mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, int32 version, LocationType type, const QString &to, int32 size, LoadToCacheSetting toCache, LoadFromCloudSetting fromCloud, bool autoLoading)
: FileLoader(to, size, type, toCache, fromCloud, autoLoading)
, _dc(dc)
, _id(id)
, _access(access)
, _version(version) {
	auto shiftedDcId = MTP::downloadDcId(_dc, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(MaxFileQueries));
	}
	_queue = &i.value();
}

int32 mtpFileLoader::currentOffset(bool includeSkipped) const {
	return (_fileIsOpen ? _file.size() : _data.size()) - (includeSkipped ? 0 : _skippedBytes);
}

namespace {
	QString serializereqs(const QMap<mtpRequestId, int32> &reqs) { // serialize requests map in json-like format
		QString result;
		result.reserve(reqs.size() * 16 + 4);
		result.append(qsl("{ "));
		for (auto i = reqs.cbegin(), e = reqs.cend(); i != e;) {
			result.append(QString::number(i.key())).append(qsl(" : ")).append(QString::number(i.value()));
			if (++i == e) {
				break;
			} else {
				result.append(qsl(", "));
			}
		}
		result.append(qsl(" }"));
		return result;
	}
}

bool mtpFileLoader::loadPart() {
	if (_finished || _lastComplete || (!_requests.isEmpty() && !_size)) {
		if (DebugLogging::FileLoader() && _id) {
			DEBUG_LOG(("FileLoader(%1): loadPart() returned, _finished=%2, _lastComplete=%3, _requests.size()=%4, _size=%5").arg(_id).arg(Logs::b(_finished)).arg(Logs::b(_lastComplete)).arg(_requests.size()).arg(_size));
		}
		return false;
	}
	if (_size && _nextRequestOffset >= _size) {
		if (DebugLogging::FileLoader() && _id) {
			DEBUG_LOG(("FileLoader(%1): loadPart() returned, _size=%2, _nextRequestOffset=%3, _requests=%4").arg(_id).arg(_size).arg(_nextRequestOffset).arg(serializereqs(_requests)));
		}
		return false;
	}

	int32 limit = DocumentDownloadPartSize;
	MTPInputFileLocation loc;
	if (_location) {
		loc = MTP_inputFileLocation(MTP_long(_location->volume()), MTP_int(_location->local()), MTP_long(_location->secret()));
		limit = DownloadPartSize;
	} else {
		switch (_locationType) {
		case VideoFileLocation:
		case AudioFileLocation:
		case DocumentFileLocation: loc = MTP_inputDocumentFileLocation(MTP_long(_id), MTP_long(_access), MTP_int(_version)); break;
		default: cancel(true); return false; break;
		}
	}
	int32 offset = _nextRequestOffset, dcIndex = 0;
	DataRequested &dr(DataRequestedMap[_dc]);
	if (_size) {
		for (int32 i = 1; i < MTPDownloadSessionsCount; ++i) {
			if (dr.v[i] < dr.v[dcIndex]) {
				dcIndex = i;
			}
		}
	}

	App::app()->killDownloadSessionsStop(_dc);

	mtpRequestId reqId = MTP::send(MTPupload_GetFile(loc, MTP_int(offset), MTP_int(limit)), rpcDone(&mtpFileLoader::partLoaded, offset), rpcFail(&mtpFileLoader::partFailed), MTP::downloadDcId(_dc, dcIndex), 50);

	++_queue->queries;
	dr.v[dcIndex] += limit;
	_requests.insert(reqId, dcIndex);
	_nextRequestOffset += limit;

	if (DebugLogging::FileLoader() && _id) {
		DEBUG_LOG(("FileLoader(%1): requested part with offset=%2, _queue->queries=%3, _nextRequestOffset=%4, _requests=%5").arg(_id).arg(offset).arg(_queue->queries).arg(_nextRequestOffset).arg(serializereqs(_requests)));
	}

	return true;
}

void mtpFileLoader::partLoaded(int32 offset, const MTPupload_File &result, mtpRequestId req) {
	Requests::iterator i = _requests.find(req);
	if (i == _requests.cend()) {
		if (DebugLogging::FileLoader() && _id) {
			DEBUG_LOG(("FileLoader(%1): request req=%2 for offset=%3 not found in _requests=%4").arg(_id).arg(req).arg(offset).arg(serializereqs(_requests)));
		}
		return loadNext();
	}
	if (result.type() != mtpc_upload_file) {
		if (DebugLogging::FileLoader() && _id) {
			DEBUG_LOG(("FileLoader(%1): bad cons received! %2").arg(_id).arg(result.type()));
		}
		return cancel(true);
	}

	int32 limit = (_locationType == UnknownFileLocation) ? DownloadPartSize : DocumentDownloadPartSize;
	int32 dcIndex = i.value();
	DataRequestedMap[_dc].v[dcIndex] -= limit;

	--_queue->queries;
	_requests.erase(i);

	auto &d = result.c_upload_file();
	auto &bytes = d.vbytes.c_string().v;

	if (DebugLogging::FileLoader() && _id) {
		DEBUG_LOG(("FileLoader(%1): got part with offset=%2, bytes=%3, _queue->queries=%4, _nextRequestOffset=%5, _requests=%6").arg(_id).arg(offset).arg(bytes.size()).arg(_queue->queries).arg(_nextRequestOffset).arg(serializereqs(_requests)));
	}

	if (bytes.size()) {
		if (_fileIsOpen) {
			int64 fsize = _file.size();
			if (offset < fsize) {
				_skippedBytes -= bytes.size();
			} else if (offset > fsize) {
				_skippedBytes += offset - fsize;
			}
			_file.seek(offset);
			if (_file.write(bytes.data(), bytes.size()) != qint64(bytes.size())) {
				return cancel(true);
			}
		} else {
			_data.reserve(offset + bytes.size());
			if (offset > _data.size()) {
				_skippedBytes += offset - _data.size();
				_data.resize(offset);
			}
			if (offset == _data.size()) {
				_data.append(bytes.data(), bytes.size());
			} else {
				_skippedBytes -= bytes.size();
				if (int64(offset + bytes.size()) > _data.size()) {
					_data.resize(offset + bytes.size());
				}
				memcpy(_data.data() + offset, bytes.data(), bytes.size());
			}
		}
	}
	if (!bytes.size() || (bytes.size() % 1024)) { // bad next offset
		_lastComplete = true;
	}
	if (_requests.isEmpty() && (_lastComplete || (_size && _nextRequestOffset >= _size))) {
		if (!_fname.isEmpty() && (_toCache == LoadToCacheAsWell)) {
			if (!_fileIsOpen) _fileIsOpen = _file.open(QIODevice::WriteOnly);
			if (!_fileIsOpen) {
				return cancel(true);
			}
			if (_file.write(_data) != qint64(_data.size())) {
				return cancel(true);
			}
		}
		_finished = true;
		if (_fileIsOpen) {
			_file.close();
			_fileIsOpen = false;
			Platform::File::PostprocessDownloaded(QFileInfo(_file).absoluteFilePath());
		}
		removeFromQueue();

		if (!_queue->queries) {
			App::app()->killDownloadSessionsStart(_dc);
		}

		if (_localStatus == LocalNotFound || _localStatus == LocalFailed) {
			if (_locationType != UnknownFileLocation) { // audio, video, document
				MediaKey mkey = mediaKey(_locationType, _dc, _id, _version);
				if (!_fname.isEmpty()) {
					Local::writeFileLocation(mkey, FileLocation(_fname));
				}
				if (_toCache == LoadToCacheAsWell) {
					if (_locationType == DocumentFileLocation) {
						Local::writeStickerImage(mkey, _data);
					} else if (_locationType == AudioFileLocation) {
						Local::writeAudio(mkey, _data);
					}
				}
			} else {
				Local::writeImage(storageKey(*_location), StorageImageSaved(_data));
			}
		}
	} else {
		if (DebugLogging::FileLoader() && _id) {
			DEBUG_LOG(("FileLoader(%1): not done yet, _lastComplete=%2, _size=%3, _nextRequestOffset=%4, _requests=%5").arg(_id).arg(Logs::b(_lastComplete)).arg(_size).arg(_nextRequestOffset).arg(serializereqs(_requests)));
		}
	}
	if (_finished) {
		_downloader->taskFinished().notify();
	}

	emit progress(this);

	loadNext();
}

bool mtpFileLoader::partFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	cancel(true);
	return true;
}

void mtpFileLoader::cancelRequests() {
	if (_requests.isEmpty()) return;

	int32 limit = (_locationType == UnknownFileLocation) ? DownloadPartSize : DocumentDownloadPartSize;
	DataRequested &dr(DataRequestedMap[_dc]);
	for (Requests::const_iterator i = _requests.cbegin(), e = _requests.cend(); i != e; ++i) {
		MTP::cancel(i.key());
		int32 dcIndex = i.value();
		dr.v[dcIndex] -= limit;
	}
	_queue->queries -= _requests.size();
	_requests.clear();

	if (!_queue->queries) {
		Messenger::Instance().killDownloadSessionsStart(_dc);
	}
}

bool mtpFileLoader::tryLoadLocal() {
	if (_localStatus == LocalNotFound || _localStatus == LocalLoaded || _localStatus == LocalFailed) {
		return false;
	}
	if (_localStatus == LocalLoading) {
		return true;
	}

	if (_location) {
		_localTaskId = Local::startImageLoad(storageKey(*_location), this);
	} else {
		if (_toCache == LoadToCacheAsWell) {
			MediaKey mkey = mediaKey(_locationType, _dc, _id, _version);
			if (_locationType == DocumentFileLocation) {
				_localTaskId = Local::startStickerImageLoad(mkey, this);
			} else if (_locationType == AudioFileLocation) {
				_localTaskId = Local::startAudioLoad(mkey, this);
			}
		}
	}

	emit progress(this);

	if (_localStatus != LocalNotTried) {
		return _finished;
	} else if (_localTaskId) {
		_localStatus = LocalLoading;
		return true;
	}
	_localStatus = LocalNotFound;
	return false;
}

mtpFileLoader::~mtpFileLoader() {
	cancelRequests();
}

webFileLoader::webFileLoader(const QString &url, const QString &to, LoadFromCloudSetting fromCloud, bool autoLoading)
: FileLoader(QString(), 0, UnknownFileLocation, LoadToCacheAsWell, fromCloud, autoLoading)
, _url(url)
, _requestSent(false)
, _already(0) {
	_queue = &_webQueue;
}

bool webFileLoader::loadPart() {
	if (_finished || _requestSent || _webLoadManager == FinishedWebLoadManager) return false;
	if (!_webLoadManager) {
		_webLoadMainManager = new WebLoadMainManager();

		_webLoadThread = new QThread();
		_webLoadManager = new WebLoadManager(_webLoadThread);

		_webLoadThread->start();
	}

	_requestSent = true;
	_webLoadManager->append(this, _url);
	return false;
}

int32 webFileLoader::currentOffset(bool includeSkipped) const {
	return _already;
}

void webFileLoader::onProgress(qint64 already, qint64 size) {
	_size = size;
	_already = already;

	emit progress(this);
}

void webFileLoader::onFinished(const QByteArray &data) {
	if (_fileIsOpen) {
		if (_file.write(data.constData(), data.size()) != qint64(data.size())) {
			return cancel(true);
		}
	} else {
		_data = data;
	}
	if (!_fname.isEmpty() && (_toCache == LoadToCacheAsWell)) {
		if (!_fileIsOpen) _fileIsOpen = _file.open(QIODevice::WriteOnly);
		if (!_fileIsOpen) {
			return cancel(true);
		}
		if (_file.write(_data) != qint64(_data.size())) {
			return cancel(true);
		}
	}
	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		Platform::File::PostprocessDownloaded(QFileInfo(_file).absoluteFilePath());
	}
	removeFromQueue();

	if (_localStatus == LocalNotFound || _localStatus == LocalFailed) {
		Local::writeWebFile(_url, _data);
	}
	_downloader->taskFinished().notify();

	emit progress(this);

	loadNext();
}

void webFileLoader::onError() {
	cancel(true);
}

bool webFileLoader::tryLoadLocal() {
	if (_localStatus == LocalNotFound || _localStatus == LocalLoaded || _localStatus == LocalFailed) {
		return false;
	}
	if (_localStatus == LocalLoading) {
		return true;
	}

	_localTaskId = Local::startWebFileLoad(_url, this);
	if (_localStatus != LocalNotTried) {
		return _finished;
	} else if (_localTaskId) {
		_localStatus = LocalLoading;
		return true;
	}
	_localStatus = LocalNotFound;
	return false;
}

void webFileLoader::cancelRequests() {
	if (!webLoadManager()) return;
	webLoadManager()->stop(this);
}

webFileLoader::~webFileLoader() {
}

class webFileLoaderPrivate {
public:
	webFileLoaderPrivate(webFileLoader *loader, const QString &url)
		: _interface(loader)
		, _url(url)
		, _already(0)
		, _size(0)
		, _reply(0)
		, _redirectsLeft(MaxHttpRedirects) {
	}

	QNetworkReply *reply() {
		return _reply;
	}

	QNetworkReply *request(QNetworkAccessManager &manager, const QString &redirect) {
		if (!redirect.isEmpty()) _url = redirect;

		QNetworkRequest req(_url);
		QByteArray rangeHeaderValue = "bytes=" + QByteArray::number(_already) + "-";
		req.setRawHeader("Range", rangeHeaderValue);
		_reply = manager.get(req);
		return _reply;
	}

	bool oneMoreRedirect() {
		if (_redirectsLeft) {
			--_redirectsLeft;
			return true;
		}
		return false;
	}

	void setData(const QByteArray &data) {
		_data = data;
	}
	void addData(const QByteArray &data) {
		_data.append(data);
	}
	const QByteArray &data() {
		return _data;
	}
	void setProgress(qint64 already, qint64 size) {
		_already = already;
		_size = qMax(size, 0LL);
	}

	qint64 size() const {
		return _size;
	}
	qint64 already() const {
		return _already;
	}

private:
	webFileLoader *_interface;
	QUrl _url;
	qint64 _already, _size;
	QNetworkReply *_reply;
	int32 _redirectsLeft;
	QByteArray _data;

	friend class WebLoadManager;
};

void reinitWebLoadManager() {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	if (webLoadManager()) {
		webLoadManager()->setProxySettings(App::getHttpProxySettings());
	}
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
}

void stopWebLoadManager() {
	if (webLoadManager()) {
		_webLoadThread->quit();
		DEBUG_LOG(("Waiting for webloadThread to finish"));
		_webLoadThread->wait();
		delete _webLoadManager;
		delete _webLoadMainManager;
		delete _webLoadThread;
		_webLoadThread = 0;
		_webLoadMainManager = 0;
		_webLoadManager = FinishedWebLoadManager;
	}
}

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
void WebLoadManager::setProxySettings(const QNetworkProxy &proxy) {
	QMutexLocker lock(&_loaderPointersMutex);
	_proxySettings = proxy;
	emit proxyApplyDelayed();
}
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

WebLoadManager::WebLoadManager(QThread *thread) {
	moveToThread(thread);
	_manager.moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(process()));
	connect(thread, SIGNAL(finished()), this, SLOT(finish()));
	connect(this, SIGNAL(processDelayed()), this, SLOT(process()), Qt::QueuedConnection);
	connect(this, SIGNAL(proxyApplyDelayed()), this, SLOT(proxyApply()), Qt::QueuedConnection);

	connect(this, SIGNAL(progress(webFileLoader*,qint64,qint64)), _webLoadMainManager, SLOT(progress(webFileLoader*,qint64,qint64)));
	connect(this, SIGNAL(finished(webFileLoader*,QByteArray)), _webLoadMainManager, SLOT(finished(webFileLoader*,QByteArray)));
	connect(this, SIGNAL(error(webFileLoader*)), _webLoadMainManager, SLOT(error(webFileLoader*)));

	connect(&_manager, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)), this, SLOT(onFailed(QNetworkReply*)));
#ifndef OS_MAC_OLD
	connect(&_manager, SIGNAL(sslErrors(QNetworkReply*,const QList<QSslError>&)), this, SLOT(onFailed(QNetworkReply*)));
#endif // OS_MAC_OLD
}

void WebLoadManager::append(webFileLoader *loader, const QString &url) {
	loader->_private = new webFileLoaderPrivate(loader, url);

	QMutexLocker lock(&_loaderPointersMutex);
	_loaderPointers.insert(loader, loader->_private);
	emit processDelayed();
}

void WebLoadManager::stop(webFileLoader *loader) {
	QMutexLocker lock(&_loaderPointersMutex);
	_loaderPointers.remove(loader);
	emit processDelayed();
}

bool WebLoadManager::carries(webFileLoader *loader) const {
	QMutexLocker lock(&_loaderPointersMutex);
	return _loaderPointers.contains(loader);
}

bool WebLoadManager::handleReplyResult(webFileLoaderPrivate *loader, WebReplyProcessResult result) {
	QMutexLocker lock(&_loaderPointersMutex);
	LoaderPointers::iterator it = _loaderPointers.find(loader->_interface);
	if (it != _loaderPointers.cend() && it.key()->_private != loader) {
		it = _loaderPointers.end(); // it is a new loader which was realloced in the same address
	}
	if (it == _loaderPointers.cend()) {
		return false;
	}

	if (result == WebReplyProcessProgress) {
		if (loader->size() > AnimationInMemory) {
			LOG(("API Error: too large file is loaded to cache: %1").arg(loader->size()));
			result = WebReplyProcessError;
		}
	}
	if (result == WebReplyProcessError) {
		if (it != _loaderPointers.cend()) {
			emit error(it.key());
		}
		return false;
	}
	if (loader->already() < loader->size() || !loader->size()) {
		emit progress(it.key(), loader->already(), loader->size());
		return true;
	}
	emit finished(it.key(), loader->data());
	return false;
}

void WebLoadManager::onFailed(QNetworkReply::NetworkError error) {
	onFailed(qobject_cast<QNetworkReply*>(QObject::sender()));
}

void WebLoadManager::onFailed(QNetworkReply *reply) {
	if (!reply) return;
	reply->deleteLater();

	Replies::iterator j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	webFileLoaderPrivate *loader = j.value();
	_replies.erase(j);

	LOG(("Network Error: Failed to request '%1', error %2 (%3)").arg(QString::fromLatin1(loader->_url.toEncoded())).arg(int(reply->error())).arg(reply->errorString()));

	if (!handleReplyResult(loader, WebReplyProcessError)) {
		_loaders.remove(loader);
		delete loader;
	}
}

void WebLoadManager::onProgress(qint64 already, qint64 size) {
	QNetworkReply *reply = qobject_cast<QNetworkReply*>(QObject::sender());
	if (!reply) return;

	Replies::iterator j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	webFileLoaderPrivate *loader = j.value();

	WebReplyProcessResult result = WebReplyProcessProgress;
	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	int32 status = statusCode.isValid() ? statusCode.toInt() : 200;
	if (status != 200 && status != 206 && status != 416) {
		if (status == 301 || status == 302) {
			QString loc = reply->header(QNetworkRequest::LocationHeader).toString();
			if (!loc.isEmpty()) {
				if (loader->oneMoreRedirect()) {
					sendRequest(loader, loc);
					return;
				} else {
					LOG(("Network Error: Too many HTTP redirects in onFinished() for web file loader: %1").arg(loc));
					result = WebReplyProcessError;
				}
			}
		} else {
			LOG(("Network Error: Bad HTTP status received in WebLoadManager::onProgress(): %1").arg(statusCode.toInt()));
			result = WebReplyProcessError;
		}
	} else {
		loader->setProgress(already, size);
		QByteArray r = reply->readAll();
		if (!r.isEmpty()) {
			loader->addData(r);
		}
		if (size == 0) {
			LOG(("Network Error: Zero size received for HTTP download progress in WebLoadManager::onProgress(): %1 / %2").arg(already).arg(size));
			result = WebReplyProcessError;
		}
	}
	if (!handleReplyResult(loader, result)) {
		_replies.erase(j);
		_loaders.remove(loader);
		delete loader;

		reply->abort();
		reply->deleteLater();
	}
}

void WebLoadManager::onMeta() {
	QNetworkReply *reply = qobject_cast<QNetworkReply*>(QObject::sender());
	if (!reply) return;

	Replies::iterator j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	webFileLoaderPrivate *loader = j.value();

	typedef QList<QNetworkReply::RawHeaderPair> Pairs;
	Pairs pairs = reply->rawHeaderPairs();
	for (Pairs::iterator i = pairs.begin(), e = pairs.end(); i != e; ++i) {
		if (QString::fromUtf8(i->first).toLower() == "content-range") {
			QRegularExpressionMatch m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(i->second));
			if (m.hasMatch()) {
				loader->setProgress(qMax(qint64(loader->data().size()), loader->already()), m.captured(1).toLongLong());
				if (!handleReplyResult(loader, WebReplyProcessProgress)) {
					_replies.erase(j);
					_loaders.remove(loader);
					delete loader;

					reply->abort();
					reply->deleteLater();
				}
			}
		}
	}
}

void WebLoadManager::process() {
	Loaders newLoaders;
	{
		QMutexLocker lock(&_loaderPointersMutex);
		for (LoaderPointers::iterator i = _loaderPointers.begin(), e = _loaderPointers.end(); i != e; ++i) {
			Loaders::iterator it = _loaders.find(i.value());
			if (i.value()) {
				if (it == _loaders.cend()) {
					_loaders.insert(i.value());
					newLoaders.insert(i.value());
				}
				i.value() = 0;
			}
		}
		for (auto i = _loaders.begin(), e = _loaders.end(); i != e;) {
			LoaderPointers::iterator it = _loaderPointers.find((*i)->_interface);
			if (it != _loaderPointers.cend() && it.key()->_private != (*i)) {
				it = _loaderPointers.end();
			}
			if (it == _loaderPointers.cend()) {
				if (QNetworkReply *reply = (*i)->reply()) {
					_replies.remove(reply);
					reply->abort();
					reply->deleteLater();
				}
				delete (*i);
				i = _loaders.erase(i);
			} else {
				++i;
			}
		}
	}
	for_const (webFileLoaderPrivate *loader, newLoaders) {
		if (_loaders.contains(loader)) {
			sendRequest(loader);
		}
	}
}

void WebLoadManager::sendRequest(webFileLoaderPrivate *loader, const QString &redirect) {
	Replies::iterator j = _replies.find(loader->reply());
	if (j != _replies.cend()) {
		QNetworkReply *r = j.key();
		_replies.erase(j);

		r->abort();
		r->deleteLater();
	}

	QNetworkReply *r = loader->request(_manager, redirect);
	connect(r, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(onProgress(qint64, qint64)));
	connect(r, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onFailed(QNetworkReply::NetworkError)));
	connect(r, SIGNAL(metaDataChanged()), this, SLOT(onMeta()));
	_replies.insert(r, loader);
}

void WebLoadManager::proxyApply() {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	QMutexLocker lock(&_loaderPointersMutex);
	_manager.setProxy(_proxySettings);
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
}

void WebLoadManager::finish() {
	clear();
}

void WebLoadManager::clear() {
	QMutexLocker lock(&_loaderPointersMutex);
	for (LoaderPointers::iterator i = _loaderPointers.begin(), e = _loaderPointers.end(); i != e; ++i) {
		if (i.value()) {
			i.key()->_private = 0;
		}
	}
	_loaderPointers.clear();

	for_const (webFileLoaderPrivate *loader, _loaders) {
		delete loader;
	}
	_loaders.clear();

	for (Replies::iterator i = _replies.begin(), e = _replies.end(); i != e; ++i) {
		delete i.key();
	}
	_replies.clear();
}

WebLoadManager::~WebLoadManager() {
	clear();
}

void WebLoadMainManager::progress(webFileLoader *loader, qint64 already, qint64 size) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->onProgress(already, size);
	}
}

void WebLoadMainManager::finished(webFileLoader *loader, QByteArray data) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->onFinished(data);
	}
}

void WebLoadMainManager::error(webFileLoader *loader) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->onError();
	}
}
