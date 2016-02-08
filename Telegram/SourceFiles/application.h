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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "window.h"
#include "pspecific.h"

class UpdateChecker;
class Application : public QApplication {
	Q_OBJECT

public:

	Application(int &argc, char **argv);
	~Application();

// Single instance application
public slots:

	void socketConnected();
	void socketError(QLocalSocket::LocalSocketError e);
	void socketDisconnected();
	void socketWritten(qint64 bytes);
	void socketReading();
	void newInstanceConnected();
	void closeApplication();

	void readClients();
	void removeClients();

private:

	typedef QPair<QLocalSocket*, QByteArray> LocalClient;
	typedef QList<LocalClient> LocalClients;

	QString _localServerName, _localSocketReadData;
	QLocalServer _localServer;
	QLocalSocket _localSocket;
	LocalClients _localClients;
	bool _secondInstance;

	void singleInstanceChecked();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

// Autoupdating
public:

	void startUpdateCheck(bool forceWait);
	void stopUpdate();

	enum UpdatingState {
		UpdatingNone,
		UpdatingDownload,
		UpdatingReady,
	};
	UpdatingState updatingState();
	int32 updatingSize();
	int32 updatingReady();

signals:

	void updateChecking();
	void updateLatest();
	void updateProgress(qint64 ready, qint64 total);
	void updateReady();
	void updateFailed();

public slots:

	void updateCheck();

	void updateGotCurrent();
	void updateFailedCurrent(QNetworkReply::NetworkError e);

	void onUpdateReady();
	void onUpdateFailed();

private:

	SingleTimer _updateCheckTimer;
	QNetworkReply *_updateReply;
	QNetworkAccessManager _updateManager;
	QThread *_updateThread;
	UpdateChecker *_updateChecker;

#endif
};

namespace Sandbox {

	QRect availableGeometry();
	QRect screenGeometry(const QPoint &p);
	void setActiveWindow(QWidget *window);
	bool isSavingSession();

	void installEventFilter(QObject *filter);

	void execExternal(const QString &cmd);

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

	void startUpdateCheck();
	void stopUpdate();

	Application::UpdatingState updatingState();
	int32 updatingSize();
	int32 updatingReady();

	void updateChecking();
	void updateLatest();
	void updateProgress(qint64 ready, qint64 total);
	void updateFailed();
	void updateReady();

#endif

	void connect(const char *signal, QObject *object, const char *method);

	void launch();

}

class MainWidget;
class FileUploader;
class Translator;

class AppClass : public QObject, public RPCSender {
	Q_OBJECT

public:

	AppClass();
	~AppClass();

	static AppClass *app();
	static Window *wnd();
	static MainWidget *main();

	FileUploader *uploader();
	void uploadProfilePhoto(const QImage &tosend, const PeerId &peerId);
	void regPhotoUpdate(const PeerId &peer, const FullMsgId &msgId);
	void clearPhotoUpdates();
	bool isPhotoUpdating(const PeerId &peer);
	void cancelPhotoUpdate(const PeerId &peer);

	void mtpPause();
	void mtpUnpause();

	void selfPhotoCleared(const MTPUserProfilePhoto &result);
	void chatPhotoCleared(PeerId peer, const MTPUpdates &updates);
	void selfPhotoDone(const MTPphotos_Photo &result);
	void chatPhotoDone(PeerId peerId, const MTPUpdates &updates);
	bool peerPhotoFail(PeerId peerId, const RPCError &e);
	void peerClearPhoto(PeerId peer);

	void writeUserConfigIn(uint64 ms);

	void killDownloadSessionsStart(int32 dc);
	void killDownloadSessionsStop(int32 dc);

	void checkLocalTime();
	void checkMapVersion();

signals:

	void peerPhotoDone(PeerId peer);
	void peerPhotoFail(PeerId peer);

	void adjustSingleTimers();

public slots:

	void doMtpUnpause();

	void photoUpdated(const FullMsgId &msgId, const MTPInputFile &file);

	void onSwitchDebugMode();
	void onSwitchTestMode();

	void killDownloadSessions();
	void onAppStateChanged(Qt::ApplicationState state);

private:

	QMap<FullMsgId, PeerId> photoUpdates;

	QMap<int32, uint64> killDownloadSessionTimes;
	SingleTimer killDownloadSessionsTimer;

	uint64 _lastActionTime;

	Window _window;
	FileUploader *_uploader;
	Translator *_translator;

	SingleTimer _mtpUnpauseTimer;

};
