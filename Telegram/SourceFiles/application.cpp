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
#include "application.h"
#include "style.h"

#include "pspecific.h"
#include "fileuploader.h"
#include "mainwidget.h"
#include "supporttl.h"

#include "lang.h"
#include "boxes/confirmbox.h"
#include "langloaderplain.h"

namespace {
	Application *mainApp = 0;
	FileUploader *uploader = 0;
	QString lng;

	void mtpStateChanged(int32 dc, int32 state) {
		if (App::wnd()) {
			App::wnd()->mtpStateChanged(dc, state);
		}
	}

	class _DebugWaiter : public QObject {
	public:

		_DebugWaiter(QObject *parent) : QObject(parent), _debugState(0) {

		}
		bool eventFilter(QObject *o, QEvent *e) {
			if (e->type() == QEvent::KeyPress) {
				QKeyEvent *ev = static_cast<QKeyEvent*>(e);
				switch (_debugState) {
				case 0: if (ev->key() == Qt::Key_F12) _debugState = 1; break;
				case 1: if (ev->key() == Qt::Key_F11) _debugState = 2; else if (ev->key() != Qt::Key_F12) _debugState = 0; break;
				case 2: if (ev->key() == Qt::Key_F10) _debugState = 3; else if (ev->key() != Qt::Key_F11) _debugState = 0; break;
				case 3: if (ev->key() == Qt::Key_F11) _debugState = 4; else if (ev->key() != Qt::Key_F10) _debugState = 0; break;
				case 4: if (ev->key() == Qt::Key_F12) offerDebug(); if (ev->key() != Qt::Key_F11) _debugState = 0; break;
				}
			}
			return QObject::eventFilter(o, e);
		}
		void offerDebug() {
			ConfirmBox *box = new ConfirmBox(lang(lng_sure_enable_debug));
			connect(box, SIGNAL(confirmed()), App::app(), SLOT(onEnableDebugMode()));
			App::wnd()->showLayer(box);
		}

	private:

		int _debugState;
	};
}

Application::Application(int argc, char *argv[]) : PsApplication(argc, argv),
    serverName(psServerPrefix() + cGUIDStr()), closing(false),
	updateRequestId(0), updateThread(0), updateDownloader(0), updateReply(0) {
	if (mainApp) {
		DEBUG_LOG(("Application Error: another Application was created, terminating.."));
		exit(0);
	}
	mainApp = this;

	installEventFilter(new _DebugWaiter(this));

	QFontDatabase::addApplicationFont(qsl(":/gui/art/OpenSans-Regular.ttf"));
	QFontDatabase::addApplicationFont(qsl(":/gui/art/OpenSans-Bold.ttf"));
	QFontDatabase::addApplicationFont(qsl(":/gui/art/OpenSans-Semibold.ttf"));

	float64 dpi = primaryScreen()->logicalDotsPerInch();
	if (dpi <= 108) { // 0-96-108
		cSetScreenScale(dbisOne);
	} else if (dpi <= 132) { // 108-120-132
		cSetScreenScale(dbisOneAndQuarter);
	} else if (dpi <= 168) { // 132-144-168
		cSetScreenScale(dbisOneAndHalf);
	} else { // 168-192-inf
		cSetScreenScale(dbisTwo);
	}

	if (!cLangFile().isEmpty()) {
		LangLoaderPlain loader(cLangFile());
		if (!loader.errors().isEmpty()) {
			LOG(("Lang load errors: %1").arg(loader.errors()));
		} else if (!loader.warnings().isEmpty()) {
			LOG(("Lang load warnings: %1").arg(loader.warnings()));
		}
	}

	style::startManager();
	anim::startManager();
	historyInit();

	window = new Window();

	psInstallEventFilter();

	updateCheckTimer.setSingleShot(true);

	connect(&socket, SIGNAL(connected()), this, SLOT(socketConnected()));
	connect(&socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));
	connect(&socket, SIGNAL(error(QLocalSocket::LocalSocketError)), this, SLOT(socketError(QLocalSocket::LocalSocketError)));
	connect(&socket, SIGNAL(bytesWritten(qint64)), this, SLOT(socketWritten(qint64)));
	connect(&socket, SIGNAL(readyRead()), this, SLOT(socketReading()));
	connect(&server, SIGNAL(newConnection()), this, SLOT(newInstanceConnected()));
	connect(this, SIGNAL(aboutToQuit()), this, SLOT(closeApplication()));
	connect(&updateCheckTimer, SIGNAL(timeout()), this, SLOT(startUpdateCheck()));
	connect(this, SIGNAL(updateFailed()), this, SLOT(onUpdateFailed()));
	connect(this, SIGNAL(updateReady()), this, SLOT(onUpdateReady()));
	connect(&writeUserConfigTimer, SIGNAL(timeout()), this, SLOT(onWriteUserConfig()));
	writeUserConfigTimer.setSingleShot(true);

    if (cManyInstance()) {
		startApp();
	} else {
        DEBUG_LOG(("Application Info: connecting local socket to %1..").arg(serverName));
		socket.connectToServer(serverName);
	}
}

void Application::onAppUpdate(const MTPhelp_AppUpdate &response) {
	updateRequestId = 0;
	cSetLastUpdateCheck(unixtime());
	App::writeConfig();
	if (response.type() == mtpc_help_noAppUpdate) {
		startUpdateCheck();
	} else {
		updateThread = new QThread();
		updateDownloader = new PsUpdateDownloader(updateThread, response.c_help_appUpdate());
		updateThread->start();
	}
}

bool Application::onAppUpdateFail() {
	updateRequestId = 0;
	cSetLastUpdateCheck(unixtime());
	App::writeConfig();
	startUpdateCheck();
	return true;
}

void Application::updateGotCurrent() {
	if (!updateReply || updateThread) return;

	cSetLastUpdateCheck(unixtime());
	QRegularExpressionMatch m = QRegularExpression(qsl("^\\s*(\\d+)\\s*:\\s*([\\x21-\\x7f]+)\\s*$")).match(QString::fromUtf8(updateReply->readAll()));
	if (m.hasMatch()) {
		int32 currentVersion = m.captured(1).toInt();
		if (currentVersion > AppVersion) {
			updateThread = new QThread();
			updateDownloader = new PsUpdateDownloader(updateThread, m.captured(2));
			updateThread->start();
		}
	}
	if (updateReply) updateReply->deleteLater();
	updateReply = 0;
	if (!updateThread) {
		QDir updates(cWorkingDir() + "tupdates");
		if (updates.exists()) {
			QFileInfoList list = updates.entryInfoList(QDir::Files);
			for (QFileInfoList::iterator i = list.begin(), e = list.end(); i != e; ++i) {
				if (QRegularExpression("^tupdate\\d+$", QRegularExpression::CaseInsensitiveOption).match(i->fileName()).hasMatch()) {
					QFile(i->absoluteFilePath()).remove();
				}
			}
		}
		emit updateLatest();
	}
	startUpdateCheck(true);
}

void Application::updateFailedCurrent(QNetworkReply::NetworkError e) {
	LOG(("App Error: could not get current version (update check): %1").arg(e));
	if (updateReply) updateReply->deleteLater();
	updateReply = 0;

	emit updateFailed();
	startUpdateCheck(true);
}

void Application::onUpdateReady() {
	if (updateDownloader) {
		updateDownloader->deleteLater();
		updateDownloader = 0;
	}
	updateCheckTimer.stop();

	cSetLastUpdateCheck(unixtime());
	App::writeConfig();
}

void Application::onUpdateFailed() {
	if (updateDownloader) {
		updateDownloader->deleteLater();
		updateDownloader = 0;
		if (updateThread) updateThread->deleteLater();
		updateThread = 0;
	}

	cSetLastUpdateCheck(unixtime());
	App::writeConfig();
}

void Application::regPhotoUpdate(const PeerId &peer, MsgId msgId) {
	photoUpdates.insert(msgId, peer);
}

void Application::clearPhotoUpdates() {
	photoUpdates.clear();
}

bool Application::isPhotoUpdating(const PeerId &peer) {
	for (QMap<MsgId, PeerId>::iterator i = photoUpdates.begin(), e = photoUpdates.end(); i != e; ++i) {
		if (i.value() == peer) {
			return true;
		}
	}
	return false;
}

void Application::cancelPhotoUpdate(const PeerId &peer) {
	for (QMap<MsgId, PeerId>::iterator i = photoUpdates.begin(), e = photoUpdates.end(); i != e;) {
		if (i.value() == peer) {
			i = photoUpdates.erase(i);
		} else {
			++i;
		}
	}
}

void Application::selfPhotoCleared(const MTPUserProfilePhoto &result) {
	if (!App::self()) return;
	App::self()->setPhoto(result);
	emit peerPhotoDone(App::self()->id);
}

void Application::chatPhotoCleared(PeerId peer, const MTPmessages_StatedMessage &result) {
	if (App::main()) {
		App::main()->sentFullDataReceived(0, result);
	}
	cancelPhotoUpdate(peer);
	emit peerPhotoDone(peer);
}

void Application::selfPhotoDone(const MTPphotos_Photo &result) {
	if (!App::self()) return;
	const MTPDphotos_photo &photo(result.c_photos_photo());
	App::feedPhoto(photo.vphoto);
	App::feedUsers(photo.vusers);
	cancelPhotoUpdate(App::self()->id);
	emit peerPhotoDone(App::self()->id);
}

void Application::chatPhotoDone(PeerId peer, const MTPmessages_StatedMessage &result) {
	if (App::main()) {
		App::main()->sentFullDataReceived(0, result);
	}
	cancelPhotoUpdate(peer);
	emit peerPhotoDone(peer);
}

bool Application::peerPhotoFail(PeerId peer, const RPCError &e) {
	LOG(("Application Error: update photo failed %1: %2").arg(e.type()).arg(e.description()));
	cancelPhotoUpdate(peer);
	emit peerPhotoFail(peer);
	return true;
}

void Application::peerClearPhoto(PeerId peer) {
	if (App::self() && App::self()->id == peer) {
		MTP::send(MTPphotos_UpdateProfilePhoto(MTP_inputPhotoEmpty(), MTP_inputPhotoCropAuto()), rpcDone(&Application::selfPhotoCleared), rpcFail(&Application::peerPhotoFail, peer));
	} else {
		MTP::send(MTPmessages_EditChatPhoto(MTP_int(int32(peer & 0xFFFFFFFF)), MTP_inputChatPhotoEmpty()), rpcDone(&Application::chatPhotoCleared, peer), rpcFail(&Application::peerPhotoFail, peer));
	}
}

void Application::writeUserConfigIn(uint64 ms) {
	if (!writeUserConfigTimer.isActive()) {
		writeUserConfigTimer.start(ms);
	}
}

void Application::onWriteUserConfig() {
	App::writeUserConfig();
}

void Application::photoUpdated(MsgId msgId, const MTPInputFile &file) {
	if (!App::self()) return;

	QMap<MsgId, PeerId>::iterator i = photoUpdates.find(msgId);
	if (i != photoUpdates.end()) {
		PeerId peer = i.value();
		if (peer == App::self()->id) {
			MTP::send(MTPphotos_UploadProfilePhoto(file, MTP_string(""), MTP_inputGeoPointEmpty(), MTP_inputPhotoCrop(MTP_double(0), MTP_double(0), MTP_double(100))), rpcDone(&Application::selfPhotoDone), rpcFail(&Application::peerPhotoFail, peer));
		} else {
			MTP::send(MTPmessages_EditChatPhoto(MTP_int(peer & 0xFFFFFFFF), MTP_inputChatUploadedPhoto(file, MTP_inputPhotoCrop(MTP_double(0), MTP_double(0), MTP_double(100)))), rpcDone(&Application::chatPhotoDone, peer), rpcFail(&Application::peerPhotoFail, peer));
		}
	}
}

void Application::onEnableDebugMode() {
	if (!cDebug()) {
		logsInitDebug();
		cSetDebug(true);
	}
	App::wnd()->hideLayer();
}

Application::UpdatingState Application::updatingState() {
	if (!updateThread) return Application::UpdatingNone;
	if (!updateDownloader) return Application::UpdatingReady;
	return Application::UpdatingDownload;
}

int32 Application::updatingSize() {
	if (!updateDownloader) return 0;
	return updateDownloader->size();
}

int32 Application::updatingReady() {
	if (!updateDownloader) return 0;
	return updateDownloader->ready();
}

FileUploader *Application::uploader() {
	if (!::uploader) ::uploader = new FileUploader();
	return ::uploader;
}

void Application::uploadProfilePhoto(const QImage &tosend, const PeerId &peerId) {
	PreparedPhotoThumbs photoThumbs;
	QVector<MTPPhotoSize> photoSizes;

	QPixmap thumb = QPixmap::fromImage(tosend.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	photoThumbs.insert('a', thumb);
	photoSizes.push_back(MTP_photoSize(MTP_string("a"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

	QPixmap full = QPixmap::fromImage(tosend);
	photoThumbs.insert('c', full);
	photoSizes.push_back(MTP_photoSize(MTP_string("c"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0)));

	QByteArray jpeg;
	QBuffer jpegBuffer(&jpeg);
	full.save(&jpegBuffer, "JPG", 87);

	PhotoId id = MTP::nonce<PhotoId>();

	MTPPhoto photo(MTP_photo(MTP_long(id), MTP_long(0), MTP_int(MTP::authedId()), MTP_int(unixtime()), MTP_string(""), MTP_geoPointEmpty(), MTP_vector<MTPPhotoSize>(photoSizes)));

	QString file, filename;
	int32 filesize = 0;
	QByteArray data;

	ReadyLocalMedia ready(ToPreparePhoto, file, filename, filesize, data, id, id, peerId, photo, photoThumbs, MTP_documentEmpty(MTP_long(0)), jpeg);

	connect(App::uploader(), SIGNAL(photoReady(MsgId, const MTPInputFile &)), App::app(), SLOT(photoUpdated(MsgId, const MTPInputFile &)), Qt::UniqueConnection);

	MsgId newId = clientMsgId();
	App::app()->regPhotoUpdate(peerId, newId);
	App::uploader()->uploadMedia(newId, ready);
}

void Application::stopUpdate() {
	if (updateReply) {
		updateReply->abort();
		updateReply->deleteLater();
		updateReply = 0;
	}
	if (updateDownloader) {
		updateDownloader->deleteLater();
		updateDownloader = 0;
		if (updateThread) updateThread->deleteLater();
		updateThread = 0;
	}
}

void Application::startUpdateCheck(bool forceWait) {
	updateCheckTimer.stop();
	if (updateRequestId || updateThread || updateReply || !cAutoUpdate()) return;
	
	int32 updateInSecs = cLastUpdateCheck() + 3600 + (rand() % 3600) - unixtime();
	bool sendRequest = (updateInSecs <= 0 || updateInSecs > 7200);
	if (!sendRequest && !forceWait) {
		QDir updates(cWorkingDir() + "tupdates");
		if (updates.exists()) {
			QFileInfoList list = updates.entryInfoList(QDir::Files);
			for (QFileInfoList::iterator i = list.begin(), e = list.end(); i != e; ++i) {
				if (QRegularExpression("^tupdate\\d+$", QRegularExpression::CaseInsensitiveOption).match(i->fileName()).hasMatch()) {
					sendRequest = true;
				}
			}
		}
	}
	if (cManyInstance() && !cDebug()) return; // only main instance is updating

	if (sendRequest) {
		QNetworkRequest checkVersion(QUrl(qsl("http://tdesktop.com/win/tupdates/current")));
		if (updateReply) updateReply->deleteLater();

		App::setProxySettings(updateManager);
		updateReply = updateManager.get(checkVersion);
		connect(updateReply, SIGNAL(finished()), this, SLOT(updateGotCurrent()));
		connect(updateReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(updateFailedCurrent(QNetworkReply::NetworkError)));
//		updateRequestId = MTP::send(MTPhelp_GetAppUpdate(MTP_string(cApiDeviceModel()), MTP_string(cApiSystemVersion()), MTP_string(cApiAppVersion()), MTP_string(cApiLang())), rpcDone(&Application::onAppUpdate), rpcFail(&Application::onAppUpdateFail);
		emit updateChecking();
	} else {
		updateCheckTimer.start((updateInSecs + 5) * 1000);
	}
}

void Application::socketConnected() {
	DEBUG_LOG(("Application Info: socket connected, this is not the first application instance, sending show command.."));
	closing = true;
	socket.write("CMD:show;");
}

void Application::socketWritten(qint64/* bytes*/) {
	if (socket.state() != QLocalSocket::ConnectedState) {
		DEBUG_LOG(("Application Error: socket is not connected %1").arg(socket.state()));
		return;
	}
	if (socket.bytesToWrite()) {
		return;
	}
	DEBUG_LOG(("Application Info: show command written, waiting response.."));
}

void Application::socketReading() {
	if (socket.state() != QLocalSocket::ConnectedState) {
		DEBUG_LOG(("Application Error: socket is not connected %1").arg(socket.state()));
		return;
	}
	socketRead.append(socket.readAll());
	if (QRegularExpression("RES:(\\d+);").match(socketRead).hasMatch()) {
		uint64 pid = socketRead.mid(4, socketRead.length() - 5).toULongLong();
		psActivateProcess(pid);
		DEBUG_LOG(("Application Info: show command response received, pid = %1, activating and quiting..").arg(pid));
		return App::quit();
	}
}

void Application::socketError(QLocalSocket::LocalSocketError e) {
	if (closing) {
		DEBUG_LOG(("Application Error: could not write show command, error %1, quiting..").arg(e));
		return App::quit();
	}

	if (e == QLocalSocket::ServerNotFoundError) {
		DEBUG_LOG(("Application Info: this is the only instance of Telegram, starting server and app.."));
	} else {
		DEBUG_LOG(("Application Info: socket connect error %1, starting server and app..").arg(e));
	}
	socket.close();

	psCheckLocalSocket(serverName);
  
	if (!server.listen(serverName)) {
		DEBUG_LOG(("Application Error: failed to start listening to %1 server").arg(serverName));
		return App::quit();
	}

	if (!cNoStartUpdate() && psCheckReadyUpdate()) {
		cSetRestartingUpdate(true);
		DEBUG_LOG(("Application Info: installing update instead of starting app.."));
		return App::quit();
	}

	startApp();
}

void Application::startApp() {
	App::readUserConfig();
	if (!MTP::localKey().created()) {
		MTP::createLocalKey(QByteArray());
		cSetNeedConfigResave(true);
	}
	if (cNeedConfigResave()) {
		App::writeConfig();
		App::writeUserConfig();
		cSetNeedConfigResave(false);
	}

	window->createWinId();
	window->init();

	readSupportTemplates();

	MTP::setLayer(mtpLayerMax);
	MTP::start();
	
	MTP::setStateChangedHandler(mtpStateChanged);

	App::initMedia();

	if (MTP::authedId()) {
		window->setupMain(false);
	} else {
		window->setupIntro(false);
	}

	window->psFirstShow();

	if (cStartToSettings()) {
		window->showSettings();
	}

	QNetworkProxyFactory::setUseSystemConfiguration(true);
}

void Application::socketDisconnected() {
	if (closing) {
		DEBUG_LOG(("Application Error: socket disconnected before command response received, quiting.."));
		return App::quit();
	}
}

void Application::newInstanceConnected() {
	DEBUG_LOG(("Application Info: new local socket connected"));
	for (QLocalSocket *client = server.nextPendingConnection(); client; client = server.nextPendingConnection()) {
		clients.push_back(ClientSocket(client, QByteArray()));
		connect(client, SIGNAL(readyRead()), this, SLOT(readClients()));
		connect(client, SIGNAL(disconnected()), this, SLOT(removeClients()));
	}
}

void Application::readClients() {
	for (ClientSockets::iterator i = clients.begin(), e = clients.end(); i != e; ++i) {
		i->second.append(i->first->readAll());
		if (i->second.size()) {
			QString cmds(i->second);
			int32 from = 0, l = cmds.length();
			for (int32 to = cmds.indexOf(QChar(';'), from); to >= from; to = (from < l) ? cmds.indexOf(QChar(';'), from) : -1) {
				QStringRef cmd(&cmds, from, to - from);
				if (cmd.indexOf("CMD:") == 0) {
					execExternal(cmds.mid(from + 4, to - from - 4));
					QByteArray response(QString("RES:%1;").arg(QCoreApplication::applicationPid()).toUtf8());
					i->first->write(response.data(), response.size());
				} else {
					LOG(("Application Error: unknown command %1 passed in local socket").arg(QString(cmd.constData(), cmd.length())));
				}
				from = to + 1;
			}
			if (from > 0) {
				i->second = i->second.mid(from);
			}
		}
	}
}

void Application::removeClients() {
	DEBUG_LOG(("Application Info: remove clients slot called, clients %1").arg(clients.size()));
	for (ClientSockets::iterator i = clients.begin(), e = clients.end(); i != e;) {
		if (i->first->state() != QLocalSocket::ConnectedState) {
			DEBUG_LOG(("Application Info: removing client"));
			i = clients.erase(i);
			e = clients.end();
		} else {
			++i;
		}
	}
}

void Application::execExternal(const QString &cmd) {
	DEBUG_LOG(("Application Info: executing external command '%1'").arg(cmd));
	if (cmd == "show") {
		window->activate();
	}
}

void Application::closeApplication() {
	// close server
	server.close();
	for (ClientSockets::iterator i = clients.begin(), e = clients.end(); i != e; ++i) {
		disconnect(i->first, SIGNAL(disconnected()), this, SLOT(removeClients()));
		i->first->close();
	}
	clients.clear();

	MTP::stop();
}

Application::~Application() {
	App::setQuiting();
	window->setParent(0);

	anim::stopManager();

	socket.close();
	closeApplication();
	App::deinitMedia();
	mainApp = 0;
	delete updateReply;
	delete ::uploader;
	updateReply = 0;
	delete updateDownloader;
	updateDownloader = 0;
	delete updateThread;
	updateThread = 0;

	delete window;

	style::stopManager();
}

Application *Application::app() {
	return mainApp;
}

Window *Application::wnd() {
	return mainApp ? mainApp->window : 0;
}

QString Application::lang() {
	if (!lng.length()) {
		lng = psCurrentLanguage();
	}
	if (!lng.length()) {
		lng = "en";
	}
	return lng;
}

MainWidget *Application::main() {
	return mainApp ? mainApp->window->mainWidget() : 0;
}
