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
#pragma once

namespace MTP {
	void clearLoaderPriorities();
}

struct mtpFileLoaderQueue;
class mtpFileLoader : public QObject, public RPCSender {
	Q_OBJECT

public:

	mtpFileLoader(int32 dc, const int64 &volume, int32 local, const int64 &secret);
	mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, mtpTypeId locType, const QString &to, int32 size);
	bool done() const;
	mtpTypeId fileType() const;
	const QByteArray &bytes() const;
	QString fileName() const;
	float64 currentProgress() const;
	int32 currentOffset() const;
	int32 fullSize() const;

	void pause();
	void start(bool loadFirst = false, bool prior = true);
	void cancel();
	bool loading() const;

	uint64 objId() const;

	~mtpFileLoader();

	mtpFileLoader *prev, *next;
	int32 priority;

signals:

	void progress(mtpFileLoader *loader);
	void failed(mtpFileLoader *loader, bool started);

private:

	mtpFileLoaderQueue *queue;
	bool inQueue, complete;
	int32 requestId;
	void started(bool loadFirst, bool prior);
	void removeFromQueue();

	void loadNext();
	void finishFail();
	bool loadPart();
	void partLoaded(int32 offset, const MTPupload_File &result);
	bool partFailed(const RPCError &error);

	int32 dc;
	mtpTypeId locationType; // 0 or mtpc_inputVideoFileLocation / mtpc_inputAudioFileLocation / mtpc_inputDocumentFileLocation

	int64 volume; // for photo locations
	int32 local;
	int64 secret;

	uint64 id; // for other locations
	uint64 access;
	QFile file;
	int32 initialSize;

	QByteArray data;

	int32 size;
	MTPstorage_FileType type;

};
