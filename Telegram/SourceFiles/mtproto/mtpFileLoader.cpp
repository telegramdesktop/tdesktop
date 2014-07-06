/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "mainwidget.h"
#include "window.h"

namespace {
	int32 _priority = 1;
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

mtpFileLoader::mtpFileLoader(int32 dc, const int64 &volume, int32 local, const int64 &secret) : prev(0), next(0),
    priority(0), inQueue(false), complete(false), requestId(0),
    dc(dc), locationType(0), volume(volume), local(local), secret(secret),
    id(0), access(0), initialSize(0), size(0), type(MTP_storage_fileUnknown()) {
	LoaderQueues::iterator i = queues.find(dc);
	if (i == queues.cend()) {
		i = queues.insert(dc, mtpFileLoaderQueue());
	}
	queue = &i.value();
}

mtpFileLoader::mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, mtpTypeId locType, const QString &to, int32 size) : prev(0), next(0),
    priority(0), inQueue(false), complete(false), requestId(0),
	dc(dc), locationType(locType),
    id(id), access(access), file(to), initialSize(size), type(MTP_storage_fileUnknown()) {
	LoaderQueues::iterator i = queues.find(MTP::dld + dc);
	if (i == queues.cend()) {
		i = queues.insert(MTP::dld + dc, mtpFileLoaderQueue());
	}
	queue = &i.value();
}

QString mtpFileLoader::fileName() const {
	return file.fileName();
}

bool mtpFileLoader::done() const {
	return complete;
}

mtpTypeId mtpFileLoader::fileType() const {
	return type.type();
}

const QByteArray &mtpFileLoader::bytes() const {
	return data;
}

float64 mtpFileLoader::currentProgress() const {
	if (complete) return 1;
	if (!fullSize()) return 0;
	return float64(currentOffset()) / fullSize();
}

int32 mtpFileLoader::currentOffset() const {
	return file.isOpen() ? file.size() : data.size();
}

int32 mtpFileLoader::fullSize() const {
	return size;
}

uint64 mtpFileLoader::objId() const {
	return id;
}

void mtpFileLoader::loadNext() {
	if (queue->queries >= MaxFileQueries) return;
	for (mtpFileLoader *i = queue->start; i; i = i->next) {
		if (i->loadPart() && queue->queries >= MaxFileQueries) return;
	}
}

void mtpFileLoader::finishFail() {
	bool started = currentOffset() > 0;
	if (requestId) {
		requestId = 0;
		--queue->queries;
	}
	type = MTP_storage_fileUnknown();
	complete = true;
	if (file.isOpen()) {
		file.close();
		file.remove();
	}
	data = QByteArray();
	emit failed(this, started);
	file.setFileName(QString());
	loadNext();
}

bool mtpFileLoader::loadPart() {
	if (complete || requestId) return false;

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

	++queue->queries;
	int32 offset = currentOffset();
	MTPupload_GetFile request(MTPupload_getFile(loc, MTP_int(offset), MTP_int(limit)));
	requestId = MTP::send(request, rpcDone(&mtpFileLoader::partLoaded, offset), rpcFail(&mtpFileLoader::partFailed), MTP::dld + dc, 50);
	return true;
}

void mtpFileLoader::partLoaded(int32 offset, const MTPupload_File &result) {
	if (requestId) {
		--queue->queries;
		requestId = 0;
	}
	if (offset == currentOffset()) {
		int32 limit = locationType ? DocumentDownloadPartSize : DownloadPartSize;
		const MTPDupload_file &d(result.c_upload_file());
		const string &bytes(d.vbytes.c_string().v);
		if (bytes.size()) {
			if (file.isOpen()) {
				if (file.write(bytes.data(), bytes.size()) != qint64(bytes.size())) {
					return finishFail();
				}
			} else {
				data.append(bytes.data(), bytes.size());
			}
		}
		if (bytes.size() && !(bytes.size() % 1024)) { // good next offset
//			offset += bytes.size();
		} else {
			type = d.vtype;
			complete = true;
			if (file.isOpen()) {
				file.close();
				psPostprocessFile(QFileInfo(file).absoluteFilePath());
			}
			removeFromQueue();
			App::wnd()->update();
			App::wnd()->notifyUpdateAll();
		}
		emit progress(this);
	}
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

	if (!file.fileName().isEmpty()) {
		if (!file.open(QIODevice::WriteOnly)) {
			finishFail();
			return;
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
	bool started = currentOffset() > 0;
	if (requestId) {
		requestId = 0;
		--queue->queries;
	}
	type = MTP_storage_fileUnknown();
	complete = true;
	if (file.isOpen()) {
		file.close();
		file.remove();
	}
	data = QByteArray();
	file.setFileName(QString());
	emit progress(this);
	loadNext();
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
}

namespace MTP {
	void clearLoaderPriorities() {
		++_priority;
	}
}
