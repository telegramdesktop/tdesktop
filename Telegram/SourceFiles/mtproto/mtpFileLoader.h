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

enum LocalLoadStatus {
	LocalNotTried,
	LocalNotFound,
	LocalLoading,
	LocalLoaded,
	LocalFailed,
};

struct mtpFileLoaderQueue;
class mtpFileLoader : public QObject, public RPCSender {
	Q_OBJECT

public:

	mtpFileLoader(int32 dc, const uint64 &volume, int32 local, const uint64 &secret, int32 size = 0);
	mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, LocationType type, const QString &to, int32 size, bool todata = false);
	bool done() const;
	mtpTypeId fileType() const;
	const QByteArray &bytes() const;
	QString fileName() const;
	float64 currentProgress() const;
	int32 currentOffset(bool includeSkipped = false) const;
	int32 fullSize() const;

	void setFileName(const QString &filename); // set filename for duplicateInData loader

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
	LocalLoadStatus _localStatus;

	bool tryLoadLocal();
	void cancelRequests();

	typedef QMap<mtpRequestId, int32> Requests;
	Requests requests;
	int32 skippedBytes;
	int32 nextRequestOffset;
	bool lastComplete;

	void started(bool loadFirst, bool prior);
	void removeFromQueue();

	void loadNext();
	void finishFail();
	bool loadPart();
	void partLoaded(int32 offset, const MTPupload_File &result, mtpRequestId req);
	bool partFailed(const RPCError &error);

	int32 dc;
	LocationType _locationType;

	uint64 volume; // for photo locations
	int32 local;
	uint64 secret;

	uint64 id; // for other locations
	uint64 access;
	QFile file;
	QString fname;
	bool fileIsOpen;
	bool duplicateInData;

	QByteArray data;

	int32 size;
	mtpTypeId type;

};
