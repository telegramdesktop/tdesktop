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

#include "types.h"

namespace _local_inner {

	class Manager : public QObject {
	Q_OBJECT

	public:

		Manager();

		void writeMap(bool fast);
		void writingMap();
		void finish();

	public slots:

		void mapWriteTimeout();

	private:

		QTimer _mapWriteTimer;
	};

}

namespace Local {

	mtpAuthKey &oldKey();
	void createOldKey(QByteArray *salt = 0);

	void start();
	void stop();
	
	enum ClearManagerTask {
		ClearManagerAll = 0xFFFF,
		ClearManagerDownloads = 0x01,
		ClearManagerImages = 0x02,
	};

	class ClearManagerData;
	class ClearManager : public QObject {
		Q_OBJECT

	public:
		ClearManager();
		bool addTask(int task);
		bool hasTask(ClearManagerTask task);
		void start();
		~ClearManager();

	public slots:
		void onStart();

	signals:
		void succeed(int task, void *manager);
		void failed(int task, void *manager);

	private:
		ClearManagerData *data;

	};

	enum ReadMapState {
		ReadMapFailed = 0,
		ReadMapDone = 1,
		ReadMapPassNeeded = 2,
	};
	ReadMapState readMap(const QByteArray &pass);

	void writeDraft(const PeerId &peer, const QString &text);
	QString readDraft(const PeerId &peer);
	void writeDraftPositions(const PeerId &peer, const MessageCursor &cur);
	MessageCursor readDraftPositions(const PeerId &peer);
	bool hasDraftPositions(const PeerId &peer);

	void writeImage(const StorageKey &location, const ImagePtr &img);
	void writeImage(const StorageKey &location, const StorageImageSaved &jpeg, bool overwrite = true);
	StorageImageSaved readImage(const StorageKey &location);
	int32 hasImages();
	qint64 storageFilesSize();

};
