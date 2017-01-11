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
#pragma once

#include "mainwindow.h"
#include "pspecific.h"
#include "core/single_timer.h"

class UpdateChecker;
class Application : public QApplication {
	Q_OBJECT

public:
	Application(int &argc, char **argv);

	bool event(QEvent *e) override;

// Single instance application
public slots:
	void socketConnected();
	void socketError(QLocalSocket::LocalSocketError e);
	void socketDisconnected();
	void socketWritten(qint64 bytes);
	void socketReading();
	void newInstanceConnected();

	void readClients();
	void removeClients();

	void startApplication(); // will be done in exec()
	void closeApplication(); // will be done in aboutToQuit()

	void onMainThreadTask();

private:
	typedef QPair<QLocalSocket*, QByteArray> LocalClient;
	typedef QList<LocalClient> LocalClients;

	QString _localServerName, _localSocketReadData;
	QLocalServer _localServer;
	QLocalSocket _localSocket;
	LocalClients _localClients;
	bool _secondInstance = false;

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
	QNetworkReply *_updateReply = nullptr;
	QNetworkAccessManager _updateManager;
	QThread *_updateThread = nullptr;
	UpdateChecker *_updateChecker = nullptr;

#endif // !TDESKTOP_DISABLE_AUTOUPDATE
};

namespace Sandbox {

	QRect availableGeometry();
	QRect screenGeometry(const QPoint &p);
	void setActiveWindow(QWidget *window);
	bool isSavingSession();

	void installEventFilter(QObject *filter);
	void removeEventFilter(QObject *filter);

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

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

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
	static MainWindow *wnd();
	static MainWidget *main();

	FileUploader *uploader();
	void uploadProfilePhoto(const QImage &tosend, const PeerId &peerId);
	void regPhotoUpdate(const PeerId &peer, const FullMsgId &msgId);
	bool isPhotoUpdating(const PeerId &peer);
	void cancelPhotoUpdate(const PeerId &peer);

	void selfPhotoCleared(const MTPUserProfilePhoto &result);
	void chatPhotoCleared(PeerId peer, const MTPUpdates &updates);
	void selfPhotoDone(const MTPphotos_Photo &result);
	void chatPhotoDone(PeerId peerId, const MTPUpdates &updates);
	bool peerPhotoFail(PeerId peerId, const RPCError &e);
	void peerClearPhoto(PeerId peer);

	void writeUserConfigIn(TimeMs ms);

	void killDownloadSessionsStart(int32 dc);
	void killDownloadSessionsStop(int32 dc);

	void checkLocalTime();
	void checkMapVersion();

signals:

	void peerPhotoDone(PeerId peer);
	void peerPhotoFail(PeerId peer);

	void adjustSingleTimers();

public slots:
	void photoUpdated(const FullMsgId &msgId, bool silent, const MTPInputFile &file);

	void onSwitchDebugMode();
	void onSwitchWorkMode();
	void onSwitchTestMode();

	void killDownloadSessions();
	void onAppStateChanged(Qt::ApplicationState state);

	void call_handleHistoryUpdate();
	void call_handleUnreadCounterUpdate();
	void call_handleFileDialogQueue();
	void call_handleDelayedPeerUpdates();
	void call_handleObservables();

private:
	void loadLanguage();

	QMap<FullMsgId, PeerId> photoUpdates;

	QMap<int32, TimeMs> killDownloadSessionTimes;
	SingleTimer killDownloadSessionsTimer;

	TimeMs _lastActionTime = 0;

	MainWindow *_window = nullptr;
	FileUploader *_uploader = nullptr;
	Translator *_translator = nullptr;

};
