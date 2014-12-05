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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "mainwidget.h"
#include "window.h"

#include "application.h"
#include "localstorage.h"

namespace {
	int32 _priority = 1;
	struct DataRequested {
		DataRequested() {
			memset(v, 0, sizeof(v));
		}
		int64 v[MTPDownloadSessionsCount];
	};
	QMap<int32, DataRequested> _dataRequested;
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

mtpFileLoader::mtpFileLoader(int32 dc, const int64 &volume, int32 local, const int64 &secret, int32 size) : prev(0), next(0),
priority(0), inQueue(false), complete(false), triedLocal(false), skippedBytes(0), nextRequestOffset(0), lastComplete(false),
dc(dc), locationType(0), volume(volume), local(local), secret(secret),
id(0), access(0), fileIsOpen(false), size(size), type(mtpc_storage_fileUnknown) {
	LoaderQueues::iterator i = queues.find(dc);
	if (i == queues.cend()) {
		i = queues.insert(dc, mtpFileLoaderQueue());
	}
	queue = &i.value();
}

mtpFileLoader::mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, mtpTypeId locType, const QString &to, int32 size) : prev(0), next(0),
priority(0), inQueue(false), complete(false), triedLocal(false), skippedBytes(0), nextRequestOffset(0), lastComplete(false),
dc(dc), locationType(locType), volume(0), local(0), secret(0),
id(id), access(access), file(to), fname(to), fileIsOpen(false), duplicateInData(false), size(size), type(mtpc_storage_fileUnknown) {
	LoaderQueues::iterator i = queues.find(MTP::dld[0] + dc);
	if (i == queues.cend()) {
		i = queues.insert(MTP::dld[0] + dc, mtpFileLoaderQueue());
	}
	queue = &i.value();
}

mtpFileLoader::mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, mtpTypeId locType, const QString &to, int32 size, bool todata) : prev(0), next(0),
priority(0), inQueue(false), complete(false), triedLocal(false), skippedBytes(0), nextRequestOffset(0), lastComplete(false),
dc(dc), locationType(locType), volume(0), local(0), secret(0),
id(id), access(access), file(to), fname(to), fileIsOpen(false), duplicateInData(todata), size(size), type(mtpc_storage_fileUnknown) {
	LoaderQueues::iterator i = queues.find(MTP::dld[0] + dc);
	if (i == queues.cend()) {
		i = queues.insert(MTP::dld[0] + dc, mtpFileLoaderQueue());
	}
	queue = &i.value();
}

QString mtpFileLoader::fileName() const {
	return fname;
}

bool mtpFileLoader::done() const {
	return complete;
}

mtpTypeId mtpFileLoader::fileType() const {
	return type;
}

const QByteArray &mtpFileLoader::bytes() const {
	return data;
}

float64 mtpFileLoader::currentProgress() const {
	if (complete) return 1;
	if (!fullSize()) return 0;
	return float64(currentOffset()) / fullSize();
}

int32 mtpFileLoader::currentOffset(bool includeSkipped) const {
	return (fileIsOpen ? file.size() : data.size()) - (includeSkipped ? 0 : skippedBytes);
}

int32 mtpFileLoader::fullSize() const {
	return size;
}

void mtpFileLoader::setFileName(const QString &fileName) {
	if (duplicateInData && fname.isEmpty()) {
		file.setFileName(fname = fileName);
	}
}

uint64 mtpFileLoader::objId() const {
	return id;
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

void mtpFileLoader::finishFail() {
	bool started = currentOffset(true) > 0;
	cancelRequests();
	type = mtpc_storage_fileUnknown;
	complete = true;
	if (fileIsOpen) {
		file.close();
		fileIsOpen = false;
		file.remove();
	}
	data = QByteArray();
	emit failed(this, started);
	file.setFileName(fname = QString());
	loadNext();
}

bool mtpFileLoader::loadPart() {
	if (complete || lastComplete || (!requests.isEmpty() && !size)) return false;
	if (size && nextRequestOffset >= size) return false;

	int32 limit = DocumentDownloadPartSize;
	MTPInputFileLocation loc;
	switch (locationType) {
	case 0: loc = MTP_inputFileLocation(MTP_long(volume), MTP_int(local), MTP_long(secret)); limit = DownloadPartSize; break;
	case mtpc_inputVideoFileLocation: loc = MTP_inputVideoFileLocation(MTP_long(id), MTP_long(access)); break;
	case mtpc_inputAudioFileLocation: loc = MTP_inputAudioFileLocation(MTP_long(id), MTP_long(access)); break;
	case mtpc_inputDocumentFileLocation: loc = MTP_inputDocumentFileLocation(MTP_long(id), MTP_long(access)); break;
	default:
		finishFail();
		return false;
	break;
	}

	int32 offset = nextRequestOffset, dcIndex = 0;
	DataRequested &dr(_dataRequested[dc]);
	if (size) {
		for (int32 i = 1; i < MTPDownloadSessionsCount; ++i) {
			if (dr.v[i] < dr.v[dcIndex]) {
				dcIndex = i;
			}
		}
	}

	App::app()->killDownloadSessionsStop(dc);

	mtpRequestId reqId = MTP::send(MTPupload_GetFile(MTPupload_getFile(loc, MTP_int(offset), MTP_int(limit))), rpcDone(&mtpFileLoader::partLoaded, offset), rpcFail(&mtpFileLoader::partFailed), MTP::dld[dcIndex] + dc, 50);

	++queue->queries;
	dr.v[dcIndex] += limit;
	requests.insert(reqId, dcIndex);
	nextRequestOffset += limit;

	return true;
}

void mtpFileLoader::partLoaded(int32 offset, const MTPupload_File &result, mtpRequestId req) {
	Requests::iterator i = requests.find(req);
	if (i == requests.cend()) return loadNext();

	int32 limit = locationType ? DocumentDownloadPartSize : DownloadPartSize;
	int32 dcIndex = i.value();
	_dataRequested[dc].v[dcIndex] -= limit;

	--queue->queries;
	requests.erase(i);

	const MTPDupload_file &d(result.c_upload_file());
	const string &bytes(d.vbytes.c_string().v);
	if (bytes.size()) {
		if (fileIsOpen) {
			int64 fsize = file.size();
			if (offset < fsize) {
				skippedBytes -= bytes.size();
			} else if (offset > fsize) {
				skippedBytes += offset - fsize;
			}
			file.seek(offset);
			if (file.write(bytes.data(), bytes.size()) != qint64(bytes.size())) {
				return finishFail();
			}
		} else {
			data.reserve(offset + bytes.size());
			if (offset > data.size()) {
				skippedBytes += offset - data.size();
				data.resize(offset);
			}
			if (offset == data.size()) {
				data.append(bytes.data(), bytes.size());
			} else {
				skippedBytes -= bytes.size();
				if (int64(offset + bytes.size()) > data.size()) {
					data.resize(offset + bytes.size());
				}
				memcpy(data.data() + offset, bytes.data(), bytes.size());
			}
		}
	}
	if (!bytes.size() || (bytes.size() % 1024)) { // bad next offset
		lastComplete = true;
	}
	if (requests.isEmpty() && (lastComplete || (size && nextRequestOffset >= size))) {
		if (!fname.isEmpty() && duplicateInData) {
			if (!fileIsOpen) fileIsOpen = file.open(QIODevice::WriteOnly);
			if (!fileIsOpen) {
				return finishFail();
			}
			if (file.write(data) != qint64(data.size())) {
				return finishFail();
			}
		}
		type = d.vtype.type();
		complete = true;
		if (fileIsOpen) {
			file.close();
			fileIsOpen = false;
			psPostprocessFile(QFileInfo(file).absoluteFilePath());
		}
		removeFromQueue();
		App::wnd()->update();
		App::wnd()->notifyUpdateAllPhotos();
		if (!queue->queries) {
			App::app()->killDownloadSessionsStart(dc);
		}

		if (!locationType && triedLocal && (fname.isEmpty() || duplicateInData)) {
			Local::writeImage(storageKey(dc, volume, local), StorageImageSaved(type, data));
		} else if (locationType && triedLocal && !fname.isEmpty()) {
			Local::writeFileLocation(mediaKey(locationType, dc, id), FileLocation(type, fname));
		}
	}
	emit progress(this);
	loadNext();
}

bool mtpFileLoader::partFailed(const RPCError &error) {
	finishFail();
	return true;
}

void mtpFileLoader::removeFromQueue() {
	if (!inQueue) return;
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
	inQueue = false;
}

void mtpFileLoader::pause() {
	removeFromQueue();
}

void mtpFileLoader::start(bool loadFirst, bool prior) {
	if (complete) return;
	if (!triedLocal) {
		if (!locationType) {
			triedLocal = true;
			StorageImageSaved cached = Local::readImage(storageKey(dc, volume, local));
			if (cached.type != mtpc_storage_fileUnknown) {
				data = cached.data;
				if (!fname.isEmpty() && duplicateInData) {
					if (!fileIsOpen) fileIsOpen = file.open(QIODevice::WriteOnly);
					if (!fileIsOpen) {
						return finishFail();
					}
					if (file.write(data) != qint64(data.size())) {
						return finishFail();
					}
				}
				type = cached.type;
				complete = true;
				if (fileIsOpen) {
					file.close();
					fileIsOpen = false;
					psPostprocessFile(QFileInfo(file).absoluteFilePath());
				}
				App::wnd()->update();
				App::wnd()->notifyUpdateAllPhotos();
				emit progress(this);
				return loadNext();
			}
		} else if (locationType && !fname.isEmpty()) {
			triedLocal = true;
		}
	}

	if (!fname.isEmpty() && !duplicateInData && !fileIsOpen) {
		fileIsOpen = file.open(QIODevice::WriteOnly);
		if (!fileIsOpen) {
			return finishFail();
		}
	}

	mtpFileLoader *before = 0, *after = 0;
	if (prior) {
		if (inQueue && priority == _priority) {
			if (loadFirst) {
				if (!prev) return started(loadFirst, prior);
				before = queue->start;
			} else {
				if (!next || next->priority < _priority) return started(loadFirst, prior);
				after = next;
				while (after->next && after->next->priority == _priority) {
					after = after->next;
				}
			}
		} else {
			priority = _priority;
			if (loadFirst) {
				if (inQueue && !prev) return started(loadFirst, prior);
				before = queue->start;
			} else {
				if (inQueue) {
					if (next && next->priority == _priority) {
						after = next;
					} else if (prev && prev->priority < _priority) {
						before = prev;
						while (before->prev && before->prev->priority < _priority) {
							before = before->prev;
						}
					} else {
						return started(loadFirst, prior);
					}
				} else {
					if (queue->start && queue->start->priority == _priority) {
						after = queue->start;
					} else {
						before = queue->start;
					}
				}
				if (after) {
					while (after->next && after->next->priority == _priority) {
						after = after->next;
					}
				}
			}
		}
	} else {
		if (loadFirst) {
			if (inQueue && (!prev || prev->priority == _priority)) return started(loadFirst, prior);
			before = prev;
			while (before->prev && before->prev->priority != _priority) {
				before = before->prev;
			}
		} else {
			if (inQueue && !next) return started(loadFirst, prior);
			after = queue->end;
		}
	}

	removeFromQueue();

	inQueue = true;
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
	return started(loadFirst, prior);
}

void mtpFileLoader::cancel() {
	cancelRequests();
	type = mtpc_storage_fileUnknown;
	complete = true;
	if (fileIsOpen) {
		file.close();
		fileIsOpen = false;
		file.remove();
	}
	data = QByteArray();
	file.setFileName(QString());
	emit progress(this);
	loadNext();
}

void mtpFileLoader::cancelRequests() {
	if (requests.isEmpty()) return;

	int32 limit = locationType ? DocumentDownloadPartSize : DownloadPartSize;
	DataRequested &dr(_dataRequested[dc]);
	for (Requests::const_iterator i = requests.cbegin(), e = requests.cend(); i != e; ++i) {
		MTP::cancel(i.key());
		int32 dcIndex = i.value();
		dr.v[dcIndex] -= limit;
	}
	queue->queries -= requests.size();
	requests.clear();

	if (!queue->queries) {
		App::app()->killDownloadSessionsStart(dc);
	}
}

bool mtpFileLoader::loading() const {
	return inQueue;
}

void mtpFileLoader::started(bool loadFirst, bool prior) {
	if ((queue->queries >= MaxFileQueries && (!loadFirst || !prior)) || complete) return;
	loadPart();
}

mtpFileLoader::~mtpFileLoader() {
	removeFromQueue();
	cancelRequests();
}

namespace MTP {
	void clearLoaderPriorities() {
		++_priority;
	}
}
