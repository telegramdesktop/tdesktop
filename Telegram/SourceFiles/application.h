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

#include <QtNetwork/QLocalSocket>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QNetworkReply>

#include "window.h"
#include "pspecific.h"

class MainWidget;
class FileUploader;

class Application : public PsApplication, public RPCSender {
	Q_OBJECT

public:

	Application(int &argc, char **argv);
	~Application();
	
	static Application *app();
	static Window *wnd();
	static QString lang();
	static MainWidget *main();

	void onAppUpdate(const MTPhelp_AppUpdate &response);
	bool onAppUpdateFail();

	enum UpdatingState {
		UpdatingNone,
		UpdatingDownload,
		UpdatingReady,
	};
	UpdatingState updatingState();
	int32 updatingSize();
	int32 updatingReady();

	FileUploader *uploader();
	void uploadProfilePhoto(const QImage &tosend, const PeerId &peerId);
	void regPhotoUpdate(const PeerId &peer, MsgId msgId);
	void clearPhotoUpdates();
	bool isPhotoUpdating(const PeerId &peer);
	void cancelPhotoUpdate(const PeerId &peer);

	void stopUpdate();

	void selfPhotoCleared(const MTPUserProfilePhoto &result);
	void chatPhotoCleared(PeerId peer, const MTPmessages_StatedMessage &result);
	void selfPhotoDone(const MTPphotos_Photo &result);
	void chatPhotoDone(PeerId peerId, const MTPmessages_StatedMessage &rersult);
	bool peerPhotoFail(PeerId peerId, const RPCError &e);
	void peerClearPhoto(PeerId peer);

	void writeUserConfigIn(uint64 ms);

signals:

	void peerPhotoDone(PeerId peer);
	void peerPhotoFail(PeerId peer);

public slots:

	void startUpdateCheck(bool forceWait = false);
	void socketConnected();
	void socketError(QLocalSocket::LocalSocketError e);
	void socketDisconnected();
	void socketWritten(qint64 bytes);
	void socketReading();
	void newInstanceConnected();
	void closeApplication();

	void readClients();
	void removeClients();

	void updateGotCurrent();
	void updateFailedCurrent(QNetworkReply::NetworkError e);

	void onUpdateReady();
	void onUpdateFailed();

	void photoUpdated(MsgId msgId, const MTPInputFile &file);

	void onEnableDebugMode();
	void onWriteUserConfig();

private:

	QMap<MsgId, PeerId> photoUpdates;

	void startApp();

	typedef QPair<QLocalSocket*, QByteArray> ClientSocket;
	typedef QVector<ClientSocket> ClientSockets;

	QString serverName;
	QLocalSocket socket;
	QString socketRead;
	QLocalServer server;
	ClientSockets clients;
	bool closing;

	void execExternal(const QString &cmd);

	Window *window;

	mtpRequestId updateRequestId;
	QNetworkAccessManager updateManager;
	QNetworkReply *updateReply;
	QTimer updateCheckTimer;
	QThread *updateThread;
	PsUpdateDownloader *updateDownloader;

	QTimer writeUserConfigTimer;
	
};
