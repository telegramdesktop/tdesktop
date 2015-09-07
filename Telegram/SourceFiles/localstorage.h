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

#include "types.h"

namespace _local_inner {

	class Manager : public QObject {
	Q_OBJECT

	public:

		Manager();

		void writeMap(bool fast);
		void writingMap();
		void writeLocations(bool fast);
		void writingLocations();
		void finish();

	public slots:

		void mapWriteTimeout();
		void locationsWriteTimeout();

	private:

		QTimer _mapWriteTimer;
		QTimer _locationsWriteTimer;

	};

}

namespace Local {

	void start();
	void stop();

	void readSettings();
	void writeSettings();
	void writeUserSettings();
	void writeMtpData();

	void reset();

	bool checkPasscode(const QByteArray &passcode);
	void setPasscode(const QByteArray &passcode);
	
	enum ClearManagerTask {
		ClearManagerAll = 0xFFFF,
		ClearManagerDownloads = 0x01,
		ClearManagerStorage = 0x02,
	};

	struct ClearManagerData;
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
	int32 oldMapVersion();

	int32 oldSettingsVersion();

	struct MessageDraft {
		MessageDraft(MsgId replyTo = 0, QString text = QString(), bool previewCancelled = false) : replyTo(replyTo), text(text), previewCancelled(previewCancelled) {
		}
		MsgId replyTo;
		QString text;
		bool previewCancelled;
	};
	void writeDraft(const PeerId &peer, const MessageDraft &draft);
	MessageDraft readDraft(const PeerId &peer);
	void writeDraftPositions(const PeerId &peer, const MessageCursor &cur);
	MessageCursor readDraftPositions(const PeerId &peer);
	bool hasDraftPositions(const PeerId &peer);

	void writeFileLocation(MediaKey location, const FileLocation &local);
	FileLocation readFileLocation(MediaKey location, bool check = true);

	void writeImage(const StorageKey &location, const ImagePtr &img);
	void writeImage(const StorageKey &location, const StorageImageSaved &jpeg, bool overwrite = true);
	StorageImageSaved readImage(const StorageKey &location);
	int32 hasImages();
	qint64 storageImagesSize();

	void writeStickerImage(const StorageKey &location, const QByteArray &data, bool overwrite = true);
	QByteArray readStickerImage(const StorageKey &location);
	int32 hasStickers();
	qint64 storageStickersSize();

	void writeAudio(const StorageKey &location, const QByteArray &data, bool overwrite = true);
	QByteArray readAudio(const StorageKey &location);
	int32 hasAudios();
	qint64 storageAudiosSize();

	void writeStickers();
	void readStickers();

	void writeBackground(int32 id, const QImage &img);
	bool readBackground();

	void writeRecentHashtags();
	void readRecentHashtags();

	void addSavedPeer(PeerData *peer, const QDateTime &position);
	void removeSavedPeer(PeerData *peer);
	void readSavedPeers();

	void writeReportSpamStatuses();

};
