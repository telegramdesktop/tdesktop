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

struct mtpFileLoaderQueue;
class mtpFileLoader : public QObject, public RPCSender {
	Q_OBJECT

public:

	mtpFileLoader(int32 dc, const uint64 &volume, int32 local, const uint64 &secret, int32 size = 0);
	mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, LocationType type, const QString &to, int32 size, bool todata = false);
	bool done() const {
		return complete;
	}
	mtpTypeId fileType() const {
		return type;
	}
	const QByteArray &bytes() const {
		return data;
	}
	QByteArray imageFormat() const;
	QPixmap imagePixmap() const;
	QString fileName() const {
		return fname;
	}
	float64 currentProgress() const;
	int32 currentOffset(bool includeSkipped = false) const;
	int32 fullSize() const;

	void setFileName(const QString &filename); // set filename for duplicateInData loader

	void pause();
	void start(bool loadFirst = false, bool prior = true);
	void cancel();
	bool loading() const;
	bool paused() const;
	bool tryingLocal() const;

	uint64 objId() const;

	~mtpFileLoader();

	void localLoaded(const StorageImageSaved &result, const QByteArray &imageFormat = QByteArray(), const QPixmap &imagePixmap = QPixmap());

	mtpFileLoader *prev, *next;
	int32 priority;

signals:

	void progress(mtpFileLoader *loader);
	void failed(mtpFileLoader *loader, bool started);

private:

	mtpFileLoaderQueue *queue;
	bool _paused, inQueue, complete;
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

	TaskId _localTaskId;
	mutable QByteArray _imageFormat;
	mutable QPixmap _imagePixmap;
	void readImage() const;

};
