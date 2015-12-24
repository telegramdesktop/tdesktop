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
#include "stdafx.h"
#include "mainwidget.h"
#include "window.h"

#include "application.h"
#include "localstorage.h"

namespace {
	int32 GlobalPriority = 1;
	struct DataRequested {
		DataRequested() {
			memset(v, 0, sizeof(v));
		}
		int64 v[MTPDownloadSessionsCount];
	};
	QMap<int32, DataRequested> DataRequestedMap;
}
struct mtpFileLoaderQueue {
	mtpFileLoaderQueue() : queries(0), start(0), end(0) {
	}
	int32 queries;
	mtpFileLoader *start, *end;
};

namespace {
	typedef QMap<int32, mtpFileLoaderQueue> LoaderQueues;
	LoaderQueues queues;
}

mtpFileLoader::mtpFileLoader(const StorageImageLocation *location, int32 size, LoadFromCloudSetting fromCloud, bool autoLoading)
: prev(0)
, next(0)
, priority(0)
, _paused(false)
, _autoLoading(autoLoading)
, _inQueue(false)
, _complete(false)
, _localStatus(LocalNotTried)
, _skippedBytes(0)
, _nextRequestOffset(0)
, _lastComplete(false)
, _dc(location->dc())
, _locationType(UnknownFileLocation)
, _location(location)
, _id(0)
, _access(0)
, _fileIsOpen(false)
, _toCache(LoadToCacheAsWell)
, _fromCloud(fromCloud)
, _size(size)
, _type(mtpc_storage_fileUnknown)
, _localTaskId(0) {
	LoaderQueues::iterator i = queues.find(_dc);
	if (i == queues.cend()) {
		i = queues.insert(_dc, mtpFileLoaderQueue());
	}
	queue = &i.value();
}

mtpFileLoader::mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, LocationType type, const QString &to, int32 size, LoadToCacheSetting toCache, LoadFromCloudSetting fromCloud, bool autoLoading)
: prev(0)
, next(0)
, priority(0)
, _paused(false)
, _autoLoading(autoLoading)
, _inQueue(false)
, _complete(false)
, _localStatus(LocalNotTried)
, _skippedBytes(0)
, _nextRequestOffset(0)
, _lastComplete(false)
, _dc(dc)
, _locationType(type)
, _location(0)
, _id(id)
, _access(access)
, _file(to)
, _fname(to)
, _fileIsOpen(false)
, _toCache(toCache)
, _fromCloud(fromCloud)
, _size(size)
, _type(mtpc_storage_fileUnknown)
, _localTaskId(0) {
	LoaderQueues::iterator i = queues.find(MTP::dld[0] + _dc);
	if (i == queues.cend()) {
		i = queues.insert(MTP::dld[0] + _dc, mtpFileLoaderQueue());
	}
	queue = &i.value();
}

QByteArray mtpFileLoader::imageFormat() const {
	if (_imageFormat.isEmpty() && _locationType == UnknownFileLocation) {
		readImage();
	}
	return _imageFormat;
}

QPixmap mtpFileLoader::imagePixmap() const {
	if (_imagePixmap.isNull() && _locationType == UnknownFileLocation) {
		readImage();
	}
	return _imagePixmap;
}

void mtpFileLoader::readImage() const {
	QByteArray format;
	switch (_type) {
	case mtpc_storage_fileGif: format = "GIF"; break;
	case mtpc_storage_fileJpeg: format = "JPG"; break;
	case mtpc_storage_filePng: format = "PNG"; break;
	default: format = QByteArray(); break;
	}
	_imagePixmap = QPixmap::fromImage(App::readImage(_data, &format, false), Qt::ColorOnly);
	if (!_imagePixmap.isNull()) {
		_imageFormat = format;
	}
}

float64 mtpFileLoader::currentProgress() const {
	if (_complete) return 1;
	if (!fullSize()) return 0;
	return float64(currentOffset()) / fullSize();
}

int32 mtpFileLoader::currentOffset(bool includeSkipped) const {
	return (_fileIsOpen ? _file.size() : _data.size()) - (includeSkipped ? 0 : _skippedBytes);
}

int32 mtpFileLoader::fullSize() const {
	return _size;
}

bool mtpFileLoader::setFileName(const QString &fileName) {
	if (_toCache != LoadToCacheAsWell || !_fname.isEmpty()) return fileName.isEmpty();
	_fname = fileName;
	_file.setFileName(_fname);
	return true;
}

void mtpFileLoader::permitLoadFromCloud() {
	_fromCloud = LoadFromCloudOrLocal;
}

uint64 mtpFileLoader::objId() const {
	return _id;
}

void mtpFileLoader::loadNext() {
	if (queue->queries >= MaxFileQueries) return;
	for (mtpFileLoader *i = queue->start; i;) {
		if (i->loadPart()) {
			if (queue->queries >= MaxFileQueries) return;
		} else {
			i = i->next;
		}
	}
}

bool mtpFileLoader::loadPart() {
	if (_complete || _lastComplete || (!_requests.isEmpty() && !_size)) return false;
	if (_size && _nextRequestOffset >= _size) return false;

	int32 limit = DocumentDownloadPartSize;
	MTPInputFileLocation loc;
	if (_location) {
		loc = MTP_inputFileLocation(MTP_long(_location->volume()), MTP_int(_location->local()), MTP_long(_location->secret()));
		limit = DownloadPartSize;
	} else {
		switch (_locationType) {
		case VideoFileLocation:
			loc = MTP_inputVideoFileLocation(MTP_long(_id), MTP_long(_access));
		break;
		case AudioFileLocation:
			loc = MTP_inputAudioFileLocation(MTP_long(_id), MTP_long(_access));
		break;
		case DocumentFileLocation:
			loc = MTP_inputDocumentFileLocation(MTP_long(_id), MTP_long(_access));
		break;
		default:
			cancel(true);
			return false;
		break;
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

	mtpRequestId reqId = MTP::send(MTPupload_GetFile(MTPupload_getFile(loc, MTP_int(offset), MTP_int(limit))), rpcDone(&mtpFileLoader::partLoaded, offset), rpcFail(&mtpFileLoader::partFailed), MTP::dld[dcIndex] + _dc, 50);

	++queue->queries;
	dr.v[dcIndex] += limit;
	_requests.insert(reqId, dcIndex);
	_nextRequestOffset += limit;

	return true;
}

void mtpFileLoader::partLoaded(int32 offset, const MTPupload_File &result, mtpRequestId req) {
//	uint64 ms = getms();
	Requests::iterator i = _requests.find(req);
	if (i == _requests.cend()) return loadNext();

	int32 limit = (_locationType == UnknownFileLocation) ? DownloadPartSize : DocumentDownloadPartSize;
	int32 dcIndex = i.value();
	DataRequestedMap[_dc].v[dcIndex] -= limit;

	--queue->queries;
	_requests.erase(i);

	const MTPDupload_file &d(result.c_upload_file());
	const string &bytes(d.vbytes.c_string().v);
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
		_type = d.vtype.type();
		_complete = true;
		if (_fileIsOpen) {
			_file.close();
			_fileIsOpen = false;
			psPostprocessFile(QFileInfo(_file).absoluteFilePath());
		}
		removeFromQueue();

		emit App::wnd()->imageLoaded();

		if (!queue->queries) {
			App::app()->killDownloadSessionsStart(_dc);
		}

		if (_localStatus == LocalNotFound || _localStatus == LocalFailed) {
			if (_locationType != UnknownFileLocation) { // audio, video, document
				MediaKey mkey = mediaKey(_locationType, _dc, _id);
				if (!_fname.isEmpty()) {
					Local::writeFileLocation(mkey, FileLocation(mtpToStorageType(_type), _fname));
				}
				if (_toCache == LoadToCacheAsWell) {
					if (_locationType == DocumentFileLocation) {
						Local::writeStickerImage(mkey, _data);
					} else if (_locationType == AudioFileLocation) {
						Local::writeAudio(mkey, _data);
					}
				}
			} else {
				Local::writeImage(storageKey(*_location), StorageImageSaved(mtpToStorageType(_type), _data));
			}
		}
	}
	emit progress(this);
	loadNext();
}

bool mtpFileLoader::partFailed(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	cancel(true);
	return true;
}

void mtpFileLoader::removeFromQueue() {
	if (!_inQueue) return;
	if (next) {
		next->prev = prev;
	}
	if (prev) {
		prev->next = next;
	}
	if (queue->end == this) {
		queue->end = prev;
	}
	if (queue->start == this) {
		queue->start = next;
	}
	next = prev = 0;
	_inQueue = false;
}

void mtpFileLoader::pause() {
	removeFromQueue();
	_paused = true;
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
			MediaKey mkey = mediaKey(_locationType, _dc, _id);
			if (_locationType == DocumentFileLocation) {
				_localTaskId = Local::startStickerImageLoad(mkey, this);
			} else if (_locationType == AudioFileLocation) {
				_localTaskId = Local::startAudioLoad(mkey, this);
			}
		}
	}

	if (_localStatus != LocalNotTried) {
		return _complete;
	} else if (_localTaskId) {
		_localStatus = LocalLoading;
		return true;
	}
	_localStatus = LocalNotFound;
	return false;
}

void mtpFileLoader::localLoaded(const StorageImageSaved &result, const QByteArray &imageFormat, const QPixmap &imagePixmap) {
	_localTaskId = 0;
	if (result.type == StorageFileUnknown) {
		_localStatus = LocalFailed;
		start(true);
		return;
	}
	_data = result.data;
	_type = mtpFromStorageType(result.type);
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

	_complete = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		psPostprocessFile(QFileInfo(_file).absoluteFilePath());
	}
	emit App::wnd()->imageLoaded();
	emit progress(this);
	loadNext();
}

void mtpFileLoader::start(bool loadFirst, bool prior) {
	if (_paused) {
		_paused = false;
	}
	if (_complete || tryLoadLocal()) return;

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

	mtpFileLoader *before = 0, *after = 0;
	if (prior) {
		if (_inQueue && priority == GlobalPriority) {
			if (loadFirst) {
				if (!prev) return startLoading(loadFirst, prior);
				before = queue->start;
			} else {
				if (!next || next->priority < GlobalPriority) return startLoading(loadFirst, prior);
				after = next;
				while (after->next && after->next->priority == GlobalPriority) {
					after = after->next;
				}
			}
		} else {
			priority = GlobalPriority;
			if (loadFirst) {
				if (_inQueue && !prev) return startLoading(loadFirst, prior);
				before = queue->start;
			} else {
				if (_inQueue) {
					if (next && next->priority == GlobalPriority) {
						after = next;
					} else if (prev && prev->priority < GlobalPriority) {
						before = prev;
						while (before->prev && before->prev->priority < GlobalPriority) {
							before = before->prev;
						}
					} else {
						return startLoading(loadFirst, prior);
					}
				} else {
					if (queue->start && queue->start->priority == GlobalPriority) {
						after = queue->start;
					} else {
						before = queue->start;
					}
				}
				if (after) {
					while (after->next && after->next->priority == GlobalPriority) {
						after = after->next;
					}
				}
			}
		}
	} else {
		if (loadFirst) {
			if (_inQueue && (!prev || prev->priority == GlobalPriority)) return startLoading(loadFirst, prior);
			before = prev;
			while (before->prev && before->prev->priority != GlobalPriority) {
				before = before->prev;
			}
		} else {
			if (_inQueue && !next) return startLoading(loadFirst, prior);
			after = queue->end;
		}
	}

	removeFromQueue();

	_inQueue = true;
	if (!queue->start) {
		queue->start = queue->end = this;
	} else if (before) {
		if (before != next) {
			prev = before->prev;
			next = before;
			next->prev = this;
			if (prev) {
				prev->next = this;
			}
			if (queue->start->prev) queue->start = queue->start->prev;
		}
	} else if (after) {
		if (after != prev) {
			next = after->next;
			prev = after;
			after->next = this;
			if (next) {
				next->prev = this;
			}
			if (queue->end->next) queue->end = queue->end->next;
		}
	} else {
		LOG(("Queue Error: _start && !before && !after"));
	}
	return startLoading(loadFirst, prior);
}

void mtpFileLoader::cancel() {
	cancel(false);
}

void mtpFileLoader::cancel(bool fail) {
	bool started = currentOffset(true) > 0;
	cancelRequests();
	_type = mtpc_storage_fileUnknown;
	_complete = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		_file.remove();
	}
	_data = QByteArray();
	if (fail) {
		emit failed(this, started);
	} else {
		emit progress(this);
	}
	_fname = QString();
	_file.setFileName(_fname);
	loadNext();
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
	queue->queries -= _requests.size();
	_requests.clear();

	if (!queue->queries) {
		App::app()->killDownloadSessionsStart(_dc);
	}
}

void mtpFileLoader::startLoading(bool loadFirst, bool prior) {
	if ((queue->queries >= MaxFileQueries && (!loadFirst || !prior)) || _complete) return;
	loadPart();
}

mtpFileLoader::~mtpFileLoader() {
	if (_localTaskId) {
		Local::cancelTask(_localTaskId);
	}
	removeFromQueue();
	cancelRequests();
}

namespace MTP {
	void clearLoaderPriorities() {
		++GlobalPriority;
	}
}
