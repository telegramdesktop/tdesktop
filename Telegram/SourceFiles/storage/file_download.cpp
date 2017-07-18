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

Downloader::Downloader()
: _delayedLoadersDestroyer([this] { _delayedDestroyedLoaders.clear(); }) {
}

void Downloader::delayedDestroyLoader(std::unique_ptr<FileLoader> loader) {
	_delayedDestroyedLoaders.push_back(std::move(loader));
	_delayedLoadersDestroyer.call();
}

void Downloader::clearPriorities() {
	++_priority;
}

void Downloader::requestedAmountIncrement(MTP::DcId dcId, int index, int amount) {
	Expects(index >= 0 && index < MTP::kDownloadSessionsCount);
	auto it = _requestedBytesAmount.find(dcId);
	if (it == _requestedBytesAmount.cend()) {
		it = _requestedBytesAmount.emplace(dcId, RequestedInDc { { 0 } }).first;
	}
	it->second[index] += amount;
	if (it->second[index]) {
		Messenger::Instance().killDownloadSessionsStop(dcId);
	} else {
		Messenger::Instance().killDownloadSessionsStart(dcId);
	}
}

int Downloader::chooseDcIndexForRequest(MTP::DcId dcId) const {
	auto result = 0;
	auto it = _requestedBytesAmount.find(dcId);
	if (it != _requestedBytesAmount.cend()) {
		for (auto i = 1; i != MTP::kDownloadSessionsCount; ++i) {
			if (it->second[i] < it->second[result]) {
				result = i;
			}
		}
	}
	return result;
}

Downloader::~Downloader() {
	// The file loaders have pointer to downloader and they cancel
	// requests in destructor where they use that pointer, so all
	// of them need to be destroyed before any internal state of Downloader.
	_delayedDestroyedLoaders.clear();
}

} // namespace Storage

namespace {

constexpr auto kDownloadPhotoPartSize = 64 * 1024; // 64kb for photo
constexpr auto kDownloadDocumentPartSize = 128 * 1024; // 128kb for document
constexpr auto kMaxFileQueries = 16; // max 16 file parts downloaded at the same time
constexpr auto kMaxWebFileQueries = 8; // max 8 http[s] files downloaded at the same time

} // namespace

struct FileLoaderQueue {
	FileLoaderQueue(int queriesLimit) : queriesLimit(queriesLimit) {
	}
	int queriesCount = 0;
	int queriesLimit = 0;
	FileLoader *start = nullptr;
	FileLoader *end = nullptr;
};

namespace {

using LoaderQueues = QMap<int32, FileLoaderQueue>;
LoaderQueues queues;

FileLoaderQueue _webQueue(kMaxWebFileQueries);

QThread *_webLoadThread = nullptr;
WebLoadManager *_webLoadManager = nullptr;
WebLoadManager *webLoadManager() {
	return (_webLoadManager && _webLoadManager != FinishedWebLoadManager) ? _webLoadManager : nullptr;
}
WebLoadMainManager *_webLoadMainManager = nullptr;

} // namespace

FileLoader::FileLoader(const QString &toFile, int32 size, LocationType locationType, LoadToCacheSetting toCache, LoadFromCloudSetting fromCloud, bool autoLoading)
: _downloader(&AuthSession::Current().downloader())
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
	if (_queue->queriesCount >= _queue->queriesLimit) {
		return;
	}
	for (auto i = _queue->start; i;) {
		if (i->loadPart()) {
			if (_queue->queriesCount >= _queue->queriesLimit) {
				return;
			}
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
	if ((_queue->queriesCount >= _queue->queriesLimit && (!loadFirst || !prior)) || _finished) {
		return;
	}
	loadPart();
}

mtpFileLoader::mtpFileLoader(const StorageImageLocation *location, int32 size, LoadFromCloudSetting fromCloud, bool autoLoading)
: FileLoader(QString(), size, UnknownFileLocation, LoadToCacheAsWell, fromCloud, autoLoading)
, _dcId(location->dc())
, _location(location) {
	auto shiftedDcId = MTP::downloadDcId(_dcId, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(kMaxFileQueries));
	}
	_queue = &i.value();
}

mtpFileLoader::mtpFileLoader(int32 dc, uint64 id, uint64 accessHash, int32 version, LocationType type, const QString &to, int32 size, LoadToCacheSetting toCache, LoadFromCloudSetting fromCloud, bool autoLoading)
: FileLoader(to, size, type, toCache, fromCloud, autoLoading)
, _dcId(dc)
, _id(id)
, _accessHash(accessHash)
, _version(version) {
	auto shiftedDcId = MTP::downloadDcId(_dcId, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(kMaxFileQueries));
	}
	_queue = &i.value();
}

mtpFileLoader::mtpFileLoader(const WebFileImageLocation *location, int32 size, LoadFromCloudSetting fromCloud, bool autoLoading)
: FileLoader(QString(), size, UnknownFileLocation, LoadToCacheAsWell, fromCloud, autoLoading)
, _dcId(location->dc())
, _urlLocation(location) {
	auto shiftedDcId = MTP::downloadDcId(_dcId, 0);
	auto i = queues.find(shiftedDcId);
	if (i == queues.cend()) {
		i = queues.insert(shiftedDcId, FileLoaderQueue(kMaxFileQueries));
	}
	_queue = &i.value();
}

int32 mtpFileLoader::currentOffset(bool includeSkipped) const {
	return (_fileIsOpen ? _file.size() : _data.size()) - (includeSkipped ? 0 : _skippedBytes);
}

bool mtpFileLoader::loadPart() {
	if (_finished || _lastComplete || (!_sentRequests.empty() && !_size)) {
		return false;
	} else if (_size && _nextRequestOffset >= _size) {
		return false;
	}

	makeRequest(_nextRequestOffset);
	_nextRequestOffset += partSize();
	return true;
}

int mtpFileLoader::partSize() const {
	if (_locationType == UnknownFileLocation) {
		return kDownloadPhotoPartSize;
	}
	return kDownloadDocumentPartSize;
}

mtpFileLoader::RequestData mtpFileLoader::prepareRequest(int offset) const {
	auto result = RequestData();
	result.dcId = _cdnDcId ? _cdnDcId : _dcId;
	result.dcIndex = _size ? _downloader->chooseDcIndexForRequest(result.dcId) : 0;
	result.offset = offset;
	return result;
}

void mtpFileLoader::makeRequest(int offset) {
	auto requestData = prepareRequest(offset);
	auto send = [this, &requestData] {
		auto offset = requestData.offset;
		auto limit = partSize();
		auto shiftedDcId = MTP::downloadDcId(requestData.dcId, requestData.dcIndex);
		if (_cdnDcId) {
			t_assert(requestData.dcId == _cdnDcId);
			return MTP::send(MTPupload_GetCdnFile(MTP_bytes(_cdnToken), MTP_int(offset), MTP_int(limit)), rpcDone(&mtpFileLoader::cdnPartLoaded), rpcFail(&mtpFileLoader::cdnPartFailed), shiftedDcId, 50);
		} else if (_urlLocation) {
			t_assert(requestData.dcId == _dcId);
			return MTP::send(MTPupload_GetWebFile(MTP_inputWebFileLocation(MTP_bytes(_urlLocation->url()), MTP_long(_urlLocation->accessHash())), MTP_int(offset), MTP_int(limit)), rpcDone(&mtpFileLoader::webPartLoaded), rpcFail(&mtpFileLoader::partFailed), shiftedDcId, 50);
		} else {
			t_assert(requestData.dcId == _dcId);
			auto location = [this] {
				if (_location) {
					return MTP_inputFileLocation(MTP_long(_location->volume()), MTP_int(_location->local()), MTP_long(_location->secret()));
				}
				return MTP_inputDocumentFileLocation(MTP_long(_id), MTP_long(_accessHash), MTP_int(_version));
			};
			return MTP::send(MTPupload_GetFile(location(), MTP_int(offset), MTP_int(limit)), rpcDone(&mtpFileLoader::normalPartLoaded), rpcFail(&mtpFileLoader::partFailed), shiftedDcId, 50);
		}
	};
	placeSentRequest(send(), requestData);
}

void mtpFileLoader::normalPartLoaded(const MTPupload_File &result, mtpRequestId requestId) {
	Expects(result.type() == mtpc_upload_fileCdnRedirect || result.type() == mtpc_upload_file);

	auto offset = finishSentRequestGetOffset(requestId);
	if (result.type() == mtpc_upload_fileCdnRedirect) {
		return switchToCDN(offset, result.c_upload_fileCdnRedirect());
	}
	auto bytes = gsl::as_bytes(gsl::make_span(result.c_upload_file().vbytes.v));
	return partLoaded(offset, bytes);
}

void mtpFileLoader::webPartLoaded(const MTPupload_WebFile &result, mtpRequestId requestId) {
	Expects(result.type() == mtpc_upload_webFile);

	auto offset = finishSentRequestGetOffset(requestId);
	auto &webFile = result.c_upload_webFile();
	if (!_size) {
		_size = webFile.vsize.v;
	} else if (webFile.vsize.v != _size) {
		LOG(("MTP Error: Bad size provided by bot for webDocument: %1, real: %2").arg(_size).arg(webFile.vsize.v));
		return cancel(true);
	}
	auto bytes = gsl::as_bytes(gsl::make_span(webFile.vbytes.v));
	return partLoaded(offset, bytes);
}

void mtpFileLoader::cdnPartLoaded(const MTPupload_CdnFile &result, mtpRequestId requestId) {
	auto offset = finishSentRequestGetOffset(requestId);
	if (result.type() == mtpc_upload_cdnFileReuploadNeeded) {
		auto requestData = RequestData();
		requestData.dcId = _dcId;
		requestData.dcIndex = 0;
		requestData.offset = offset;
		auto shiftedDcId = MTP::downloadDcId(requestData.dcId, requestData.dcIndex);
		auto requestId = MTP::send(MTPupload_ReuploadCdnFile(MTP_bytes(_cdnToken), result.c_upload_cdnFileReuploadNeeded().vrequest_token), rpcDone(&mtpFileLoader::reuploadDone), rpcFail(&mtpFileLoader::cdnPartFailed), shiftedDcId);
		placeSentRequest(requestId, requestData);
		return;
	}
	Expects(result.type() == mtpc_upload_cdnFile);

	auto key = gsl::as_bytes(gsl::make_span(_cdnEncryptionKey));
	auto iv = gsl::as_bytes(gsl::make_span(_cdnEncryptionIV));
	Expects(key.size() == MTP::CTRState::KeySize);
	Expects(iv.size() == MTP::CTRState::IvecSize);

	auto state = MTP::CTRState();
	auto ivec = gsl::as_writeable_bytes(gsl::make_span(state.ivec));
	std::copy(iv.begin(), iv.end(), ivec.begin());

	auto counterOffset = static_cast<uint32>(offset) >> 4;
	state.ivec[15] = static_cast<uchar>(counterOffset & 0xFF);
	state.ivec[14] = static_cast<uchar>((counterOffset >> 8) & 0xFF);
	state.ivec[13] = static_cast<uchar>((counterOffset >> 16) & 0xFF);
	state.ivec[12] = static_cast<uchar>((counterOffset >> 24) & 0xFF);

	auto decryptInPlace = result.c_upload_cdnFile().vbytes.v;
	MTP::aesCtrEncrypt(decryptInPlace.data(), decryptInPlace.size(), key.data(), &state);
	auto bytes = gsl::as_bytes(gsl::make_span(decryptInPlace));
	return partLoaded(offset, bytes);
}

void mtpFileLoader::reuploadDone(const MTPBool &result, mtpRequestId requestId) {
	auto offset = finishSentRequestGetOffset(requestId);
	makeRequest(offset);
}

void mtpFileLoader::placeSentRequest(mtpRequestId requestId, const RequestData &requestData) {
	_downloader->requestedAmountIncrement(requestData.dcId, requestData.dcIndex, partSize());
	++_queue->queriesCount;
	_sentRequests.emplace(requestId, requestData);
}

int mtpFileLoader::finishSentRequestGetOffset(mtpRequestId requestId) {
	auto it = _sentRequests.find(requestId);
	Expects(it != _sentRequests.cend());

	auto requestData = it->second;
	_downloader->requestedAmountIncrement(requestData.dcId, requestData.dcIndex, -partSize());

	--_queue->queriesCount;
	_sentRequests.erase(it);

	return requestData.offset;
}

void mtpFileLoader::partLoaded(int offset, base::const_byte_span bytes) {
	if (bytes.size()) {
		if (_fileIsOpen) {
			auto fsize = _file.size();
			if (offset < fsize) {
				_skippedBytes -= bytes.size();
			} else if (offset > fsize) {
				_skippedBytes += offset - fsize;
			}
			_file.seek(offset);
			if (_file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()) != qint64(bytes.size())) {
				return cancel(true);
			}
		} else {
			_data.reserve(offset + bytes.size());
			if (offset > _data.size()) {
				_skippedBytes += offset - _data.size();
				_data.resize(offset);
			}
			if (offset == _data.size()) {
				_data.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			} else {
				_skippedBytes -= bytes.size();
				if (int64(offset + bytes.size()) > _data.size()) {
					_data.resize(offset + bytes.size());
				}
				auto src = bytes;
				auto dst = gsl::make_span(_data).subspan(offset, bytes.size());
				base::copy_bytes(gsl::as_writeable_bytes(dst), src);
			}
		}
	}
	if (!bytes.size() || (bytes.size() % 1024)) { // bad next offset
		_lastComplete = true;
	}
	if (_sentRequests.empty() && (_lastComplete || (_size && _nextRequestOffset >= _size))) {
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
			if (_urlLocation) {
				Local::writeImage(storageKey(*_urlLocation), StorageImageSaved(_data));
			} else if (_locationType != UnknownFileLocation) { // audio, video, document
				auto mkey = mediaKey(_locationType, _dcId, _id, _version);
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

bool mtpFileLoader::cdnPartFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("FILE_TOKEN_INVALID") || error.type() == qstr("REQUEST_TOKEN_INVALID")) {
		auto offset = finishSentRequestGetOffset(requestId);
		changeCDNParams(offset, 0, QByteArray(), QByteArray(), QByteArray());
		return true;
	}
	return partFailed(error);
}

void mtpFileLoader::cancelRequests() {
	while (!_sentRequests.empty()) {
		auto requestId = _sentRequests.begin()->first;
		MTP::cancel(requestId);
		finishSentRequestGetOffset(requestId);
	}
}

void mtpFileLoader::switchToCDN(int offset, const MTPDupload_fileCdnRedirect &redirect) {
	changeCDNParams(offset, redirect.vdc_id.v, redirect.vfile_token.v, redirect.vencryption_key.v, redirect.vencryption_iv.v);
}

void mtpFileLoader::changeCDNParams(int offset, MTP::DcId dcId, const QByteArray &token, const QByteArray &encryptionKey, const QByteArray &encryptionIV) {
	if (dcId != 0 && (encryptionKey.size() != MTP::CTRState::KeySize || encryptionIV.size() != MTP::CTRState::IvecSize)) {
		LOG(("Message Error: Wrong key (%1) / iv (%2) size in CDN params").arg(encryptionKey.size()).arg(encryptionIV.size()));
		cancel(true);
		return;
	}

	auto resendAllRequests = (_cdnDcId != dcId
		|| _cdnToken != token
		|| _cdnEncryptionKey != encryptionKey
		|| _cdnEncryptionIV != encryptionIV);
	_cdnDcId = dcId;
	_cdnToken = token;
	_cdnEncryptionKey = encryptionKey;
	_cdnEncryptionIV = encryptionIV;

	if (resendAllRequests && !_sentRequests.empty()) {
		auto resendOffsets = std::vector<int>();
		resendOffsets.reserve(_sentRequests.size());
		while (!_sentRequests.empty()) {
			auto requestId = _sentRequests.begin()->first;
			MTP::cancel(requestId);
			auto resendOffset = finishSentRequestGetOffset(requestId);
			resendOffsets.push_back(resendOffset);
		}
		for (auto resendOffset : resendOffsets) {
			makeRequest(resendOffset);
		}
	}
	makeRequest(offset);
}

bool mtpFileLoader::tryLoadLocal() {
	if (_localStatus == LocalNotFound || _localStatus == LocalLoaded || _localStatus == LocalFailed) {
		return false;
	}
	if (_localStatus == LocalLoading) {
		return true;
	}

	if (_urlLocation) {
		_localTaskId = Local::startImageLoad(storageKey(*_urlLocation), this);
	} else if (_location) {
		_localTaskId = Local::startImageLoad(storageKey(*_location), this);
	} else {
		if (_toCache == LoadToCacheAsWell) {
			MediaKey mkey = mediaKey(_locationType, _dcId, _id, _version);
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
		, _redirectsLeft(kMaxHttpRedirects) {
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
	static constexpr auto kMaxHttpRedirects = 5;

	webFileLoader *_interface = nullptr;
	QUrl _url;
	qint64 _already = 0;
	qint64 _size = 0;
	QNetworkReply *_reply = nullptr;
	int32 _redirectsLeft = kMaxHttpRedirects;
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
