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
#include "stdafx.h"
#include "application.h"
#include "style.h"

#include "pspecific.h"
#include "fileuploader.h"
#include "mainwidget.h"

#include "lang.h"
#include "boxes/confirmbox.h"
#include "langloaderplain.h"

#include "localstorage.h"

#include "autoupdater.h"

namespace {
	Application *mainApp = 0;
	FileUploader *uploader = 0;
	QString lng;

	void mtpStateChanged(int32 dc, int32 state) {
		if (App::wnd()) {
			App::wnd()->mtpStateChanged(dc, state);
		}
	}

	void mtpSessionReset(int32 dc) {
		if (App::main() && dc == MTP::maindc()) {
			App::main()->getDifference();
		}
	}

	class EventFilterForKeys : public QObject {
	public:

		EventFilterForKeys(QObject *parent) : QObject(parent) {

		}
		bool eventFilter(QObject *o, QEvent *e) {
			if (e->type() == QEvent::KeyPress) {
				QKeyEvent *ev = static_cast<QKeyEvent*>(e);
				if (cPlatform() == dbipMac) {
					if (ev->key() == Qt::Key_W && (ev->modifiers() & Qt::ControlModifier)) {
						if (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray) {
							App::wnd()->minimizeToTray();
							return true;
						} else {
							App::wnd()->hide();
							App::wnd()->updateIsActive(cOfflineBlurTimeout());
							App::wnd()->updateGlobalMenu();
							return true;
						}
					} else if (ev->key() == Qt::Key_M && (ev->modifiers() & Qt::ControlModifier)) {
						App::wnd()->setWindowState(Qt::WindowMinimized);
						return true;
					}
				}
				if (ev->key() == Qt::Key_MediaPlay) {
					if (App::main()) App::main()->player()->playPressed();
				} else if (ev->key() == Qt::Key_MediaPause) {
					if (App::main()) App::main()->player()->pausePressed();
				} else if (ev->key() == Qt::Key_MediaTogglePlayPause) {
					if (App::main()) App::main()->player()->playPausePressed();
				} else if (ev->key() == Qt::Key_MediaStop) {
					if (App::main()) App::main()->player()->stopPressed();
				} else if (ev->key() == Qt::Key_MediaPrevious) {
					if (App::main()) App::main()->player()->prevPressed();
				} else if (ev->key() == Qt::Key_MediaNext) {
					if (App::main()) App::main()->player()->nextPressed();
				}
			}
			return QObject::eventFilter(o, e);
		}

	};
}

Application::Application(int &argc, char **argv) : PsApplication(argc, argv),
    serverName(psServerPrefix() + cGUIDStr()), closing(false),
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	updateRequestId(0), updateReply(0), updateThread(0), updateDownloader(0),
	#endif
	_translator(0) {

	DEBUG_LOG(("Application Info: creation.."));

	QByteArray d(QDir((cPlatform() == dbipWindows ? cExeDir() : cWorkingDir()).toLower()).absolutePath().toUtf8());
	char h[33] = { 0 };
	hashMd5Hex(d.constData(), d.size(), h);
	serverName = psServerPrefix() + h + '-' + cGUIDStr();

	if (mainApp) {
		DEBUG_LOG(("Application Error: another Application was created, terminating.."));
		exit(0);
	}
	mainApp = this;

	installEventFilter(new EventFilterForKeys(this));

    QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Regular.ttf"));
    QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Bold.ttf"));
    QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Semibold.ttf"));

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

    if (devicePixelRatio() > 1) {
        cSetRetina(true);
        cSetRetinaFactor(devicePixelRatio());
        cSetIntRetinaFactor(int32(cRetinaFactor()));
		cSetConfigScale(dbisOne);
		cSetRealScale(dbisOne);
    }

	if (cLang() < languageTest) {
		cSetLang(languageId());
	}
	if (cLang() == languageTest) {
		if (QFileInfo(cLangFile()).exists()) {
			LangLoaderPlain loader(cLangFile());
			cSetLangErrors(loader.errors());
			if (!cLangErrors().isEmpty()) {
				LOG(("Lang load errors: %1").arg(cLangErrors()));
			} else if (!loader.warnings().isEmpty()) {
				LOG(("Lang load warnings: %1").arg(loader.warnings()));
			}
		} else {
			cSetLang(languageDefault);
		}
	} else if (cLang() > languageDefault && cLang() < languageCount) {
		LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[cLang()] + qsl(".strings"));
		if (!loader.errors().isEmpty()) {
			LOG(("Lang load errors: %1").arg(loader.errors()));
		} else if (!loader.warnings().isEmpty()) {
			LOG(("Lang load warnings: %1").arg(loader.warnings()));
		}
	}

	installTranslator(_translator = new Translator());

	style::startManager();
	anim::startManager();
	historyInit();

	DEBUG_LOG(("Application Info: inited.."));

    window = new Window();

	psInstallEventFilter();

	connect(&socket, SIGNAL(connected()), this, SLOT(socketConnected()));
	connect(&socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));
	connect(&socket, SIGNAL(error(QLocalSocket::LocalSocketError)), this, SLOT(socketError(QLocalSocket::LocalSocketError)));
	connect(&socket, SIGNAL(bytesWritten(qint64)), this, SLOT(socketWritten(qint64)));
	connect(&socket, SIGNAL(readyRead()), this, SLOT(socketReading()));
	connect(&server, SIGNAL(newConnection()), this, SLOT(newInstanceConnected()));
	connect(this, SIGNAL(aboutToQuit()), this, SLOT(closeApplication()));
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	connect(&updateCheckTimer, SIGNAL(timeout()), this, SLOT(startUpdateCheck()));
	connect(this, SIGNAL(updateFailed()), this, SLOT(onUpdateFailed()));
	connect(this, SIGNAL(updateReady()), this, SLOT(onUpdateReady()));
	#endif
	connect(this, SIGNAL(applicationStateChanged(Qt::ApplicationState)), this, SLOT(onAppStateChanged(Qt::ApplicationState)));
	//connect(&writeUserConfigTimer, SIGNAL(timeout()), this, SLOT(onWriteUserConfig()));
	//writeUserConfigTimer.setSingleShot(true);

	connect(&killDownloadSessionsTimer, SIGNAL(timeout()), this, SLOT(killDownloadSessions()));

	if (cManyInstance()) {
		startApp();
	} else {
        DEBUG_LOG(("Application Info: connecting local socket to %1..").arg(serverName));
		socket.connectToServer(serverName);
	}
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void Application::updateGotCurrent() {
	if (!updateReply || updateThread) return;

	cSetLastUpdateCheck(unixtime());
	QRegularExpressionMatch m = QRegularExpression(qsl("^\\s*(\\d+)\\s*:\\s*([\\x21-\\x7f]+)\\s*$")).match(QString::fromUtf8(updateReply->readAll()));
	if (m.hasMatch()) {
		int32 currentVersion = m.captured(1).toInt();
		if (currentVersion > AppVersion) {
			updateThread = new QThread();
			connect(updateThread, SIGNAL(finished()), updateThread, SLOT(deleteLater()));
			updateDownloader = new UpdateDownloader(updateThread, m.captured(2));
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
                if (QRegularExpression("^(tupdate|tmacupd|tmac32upd|tlinuxupd|tlinux32upd)\\d+$", QRegularExpression::CaseInsensitiveOption).match(i->fileName()).hasMatch()) {
					QFile(i->absoluteFilePath()).remove();
				}
			}
		}
		emit updateLatest();
	}
	startUpdateCheck(true);
	Local::writeSettings();
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
	Local::writeSettings();
}

void Application::onUpdateFailed() {
	if (updateDownloader) {
		updateDownloader->deleteLater();
		updateDownloader = 0;
		if (updateThread) updateThread->quit();
		updateThread = 0;
	}

	cSetLastUpdateCheck(unixtime());
	Local::writeSettings();
}
#endif

void Application::regPhotoUpdate(const PeerId &peer, const FullMsgId &msgId) {
	photoUpdates.insert(msgId, peer);
}

void Application::clearPhotoUpdates() {
	photoUpdates.clear();
}

bool Application::isPhotoUpdating(const PeerId &peer) {
	for (QMap<FullMsgId, PeerId>::iterator i = photoUpdates.begin(), e = photoUpdates.end(); i != e; ++i) {
		if (i.value() == peer) {
			return true;
		}
	}
	return false;
}

void Application::cancelPhotoUpdate(const PeerId &peer) {
	for (QMap<FullMsgId, PeerId>::iterator i = photoUpdates.begin(), e = photoUpdates.end(); i != e;) {
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

void Application::chatPhotoCleared(PeerId peer, const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
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

void Application::chatPhotoDone(PeerId peer, const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	cancelPhotoUpdate(peer);
	emit peerPhotoDone(peer);
}

bool Application::peerPhotoFail(PeerId peer, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	LOG(("Application Error: update photo failed %1: %2").arg(error.type()).arg(error.description()));
	cancelPhotoUpdate(peer);
	emit peerPhotoFail(peer);
	return true;
}

void Application::peerClearPhoto(PeerId id) {
	if (MTP::authedId() && peerToUser(id) == MTP::authedId()) {
		MTP::send(MTPphotos_UpdateProfilePhoto(MTP_inputPhotoEmpty(), MTP_inputPhotoCropAuto()), rpcDone(&Application::selfPhotoCleared), rpcFail(&Application::peerPhotoFail, id));
	} else if (peerIsChat(id)) {
		MTP::send(MTPmessages_EditChatPhoto(peerToBareMTPInt(id), MTP_inputChatPhotoEmpty()), rpcDone(&Application::chatPhotoCleared, id), rpcFail(&Application::peerPhotoFail, id));
	} else if (peerIsChannel(id)) {
		if (ChannelData *channel = App::channelLoaded(id)) {
			MTP::send(MTPchannels_EditPhoto(channel->inputChannel, MTP_inputChatPhotoEmpty()), rpcDone(&Application::chatPhotoCleared, id), rpcFail(&Application::peerPhotoFail, id));
		}
	}
}

void Application::killDownloadSessionsStart(int32 dc) {
	if (killDownloadSessionTimes.constFind(dc) == killDownloadSessionTimes.cend()) {
		killDownloadSessionTimes.insert(dc, getms() + MTPAckSendWaiting + MTPKillFileSessionTimeout);
	}
	if (!killDownloadSessionsTimer.isActive()) {
		killDownloadSessionsTimer.start(MTPAckSendWaiting + MTPKillFileSessionTimeout + 5);
	}
}

void Application::killDownloadSessionsStop(int32 dc) {
	killDownloadSessionTimes.remove(dc);
	if (killDownloadSessionTimes.isEmpty() && killDownloadSessionsTimer.isActive()) {
		killDownloadSessionsTimer.stop();
	}
}

void Application::checkLocalTime() {
	if (App::main()) App::main()->checkLastUpdate(checkms());
}

void Application::onAppStateChanged(Qt::ApplicationState state) {
	checkLocalTime();
	if (window) window->updateIsActive((state == Qt::ApplicationActive) ? cOnlineFocusTimeout() : cOfflineBlurTimeout());
}

void Application::killDownloadSessions() {
	uint64 ms = getms(), left = MTPAckSendWaiting + MTPKillFileSessionTimeout;
	for (QMap<int32, uint64>::iterator i = killDownloadSessionTimes.begin(); i != killDownloadSessionTimes.end(); ) {
		if (i.value() <= ms) {
			for (int j = 0; j < MTPDownloadSessionsCount; ++j) {
				MTP::stopSession(MTP::dld[j] + i.key());
			}
			i = killDownloadSessionTimes.erase(i);
		} else {
			if (i.value() - ms < left) {
				left = i.value() - ms;
			}
			++i;
		}
	}
	if (!killDownloadSessionTimes.isEmpty()) {
		killDownloadSessionsTimer.start(left);
	}
}

void Application::photoUpdated(const FullMsgId &msgId, const MTPInputFile &file) {
	if (!App::self()) return;

	QMap<FullMsgId, PeerId>::iterator i = photoUpdates.find(msgId);
	if (i != photoUpdates.end()) {
		PeerId id = i.value();
		if (MTP::authedId() && peerToUser(id) == MTP::authedId()) {
			MTP::send(MTPphotos_UploadProfilePhoto(file, MTP_string(""), MTP_inputGeoPointEmpty(), MTP_inputPhotoCrop(MTP_double(0), MTP_double(0), MTP_double(100))), rpcDone(&Application::selfPhotoDone), rpcFail(&Application::peerPhotoFail, id));
		} else if (peerIsChat(id)) {
			History *hist = App::history(id);
			hist->sendRequestId = MTP::send(MTPmessages_EditChatPhoto(hist->peer->asChat()->inputChat, MTP_inputChatUploadedPhoto(file, MTP_inputPhotoCrop(MTP_double(0), MTP_double(0), MTP_double(100)))), rpcDone(&Application::chatPhotoDone, id), rpcFail(&Application::peerPhotoFail, id), 0, 0, hist->sendRequestId);
		} else if (peerIsChannel(id)) {
			History *hist = App::history(id);
			hist->sendRequestId = MTP::send(MTPchannels_EditPhoto(hist->peer->asChannel()->inputChannel, MTP_inputChatUploadedPhoto(file, MTP_inputPhotoCrop(MTP_double(0), MTP_double(0), MTP_double(100)))), rpcDone(&Application::chatPhotoDone, id), rpcFail(&Application::peerPhotoFail, id), 0, 0, hist->sendRequestId);
		}
	}
}

void Application::onSwitchDebugMode() {
	if (cDebug()) {
		QFile(cWorkingDir() + qsl("tdata/withdebug")).remove();
		cSetDebug(false);
		cSetRestarting(true);
		cSetRestartingToSettings(true);
		App::quit();
	} else {
		logsInitDebug();
		cSetDebug(true);
		QFile f(cWorkingDir() + qsl("tdata/withdebug"));
		if (f.open(QIODevice::WriteOnly)) {
			f.write("1");
			f.close();
		}
		App::wnd()->hideLayer();
	}
}

void Application::onSwitchTestMode() {
	if (cTestMode()) {
		QFile(cWorkingDir() + qsl("tdata/withtestmode")).remove();
		cSetTestMode(false);
	} else {
		QFile f(cWorkingDir() + qsl("tdata/withtestmode"));
		if (f.open(QIODevice::WriteOnly)) {
			f.write("1");
			f.close();
		}
		cSetTestMode(true);
	}
	cSetRestarting(true);
	cSetRestartingToSettings(true);
	App::quit();
}

Application::UpdatingState Application::updatingState() {
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (!updateThread) return Application::UpdatingNone;
	if (!updateDownloader) return Application::UpdatingReady;
	return Application::UpdatingDownload;
	#else
	return Application::UpdatingNone;
	#endif
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
int32 Application::updatingSize() {
	if (!updateDownloader) return 0;
	return updateDownloader->size();
}

int32 Application::updatingReady() {
	if (!updateDownloader) return 0;
	return updateDownloader->ready();
}
#endif

FileUploader *Application::uploader() {
	if (!::uploader) ::uploader = new FileUploader();
	return ::uploader;
}

void Application::uploadProfilePhoto(const QImage &tosend, const PeerId &peerId) {
	PreparedPhotoThumbs photoThumbs;
	QVector<MTPPhotoSize> photoSizes;

	QPixmap thumb = QPixmap::fromImage(tosend.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly);
	photoThumbs.insert('a', thumb);
	photoSizes.push_back(MTP_photoSize(MTP_string("a"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

	QPixmap medium = QPixmap::fromImage(tosend.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly);
	photoThumbs.insert('b', medium);
	photoSizes.push_back(MTP_photoSize(MTP_string("b"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

	QPixmap full = QPixmap::fromImage(tosend, Qt::ColorOnly);
	photoThumbs.insert('c', full);
	photoSizes.push_back(MTP_photoSize(MTP_string("c"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0)));

	QByteArray jpeg;
	QBuffer jpegBuffer(&jpeg);
	full.save(&jpegBuffer, "JPG", 87);

	PhotoId id = MTP::nonce<PhotoId>();

	MTPPhoto photo(MTP_photo(MTP_long(id), MTP_long(0), MTP_int(unixtime()), MTP_vector<MTPPhotoSize>(photoSizes)));

	QString file, filename;
	int32 filesize = 0;
	QByteArray data;

	ReadyLocalMedia ready(ToPreparePhoto, file, filename, filesize, data, id, id, qsl("jpg"), peerId, photo, MTP_audioEmpty(MTP_long(0)), photoThumbs, MTP_documentEmpty(MTP_long(0)), jpeg, false, false, 0);

	connect(App::uploader(), SIGNAL(photoReady(const FullMsgId&, const MTPInputFile&)), App::app(), SLOT(photoUpdated(const FullMsgId&, const MTPInputFile&)), Qt::UniqueConnection);

	FullMsgId newId(peerToChannel(peerId), clientMsgId());
	App::app()->regPhotoUpdate(peerId, newId);
	App::uploader()->uploadMedia(newId, ready);
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void Application::stopUpdate() {
	if (updateReply) {
		updateReply->abort();
		updateReply->deleteLater();
		updateReply = 0;
	}
	if (updateDownloader) {
		updateDownloader->deleteLater();
		updateDownloader = 0;
		if (updateThread) updateThread->quit();
		updateThread = 0;
	}
}

void Application::startUpdateCheck(bool forceWait) {
	updateCheckTimer.stop();
	if (updateRequestId || updateThread || updateReply || !cAutoUpdate()) return;
	
	int32 updateInSecs = cLastUpdateCheck() + UpdateDelayConstPart + (rand() % UpdateDelayRandPart) - unixtime();
	bool sendRequest = (updateInSecs <= 0 || updateInSecs > (UpdateDelayConstPart + UpdateDelayRandPart));
	if (!sendRequest && !forceWait) {
		QDir updates(cWorkingDir() + "tupdates");
		if (updates.exists()) {
			QFileInfoList list = updates.entryInfoList(QDir::Files);
			for (QFileInfoList::iterator i = list.begin(), e = list.end(); i != e; ++i) {
                if (QRegularExpression("^(tupdate|tmacupd|tmac32upd|tlinuxupd|tlinux32upd)\\d+$", QRegularExpression::CaseInsensitiveOption).match(i->fileName()).hasMatch()) {
					sendRequest = true;
				}
			}
		}
	}
    if (cManyInstance() && !cDebug()) return; // only main instance is updating

	if (sendRequest) {
		QUrl url(cUpdateURL());
		if (cDevVersion()) {
			url.setQuery(qsl("version=%1&dev=1").arg(AppVersion));
		} else {
			url.setQuery(qsl("version=%1").arg(AppVersion));
		}
		QString u = url.toString();
		QNetworkRequest checkVersion(url);
		if (updateReply) updateReply->deleteLater();

		App::setProxySettings(updateManager);
		updateReply = updateManager.get(checkVersion);
		connect(updateReply, SIGNAL(finished()), this, SLOT(updateGotCurrent()));
		connect(updateReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(updateFailedCurrent(QNetworkReply::NetworkError)));
		emit updateChecking();
	} else {
		updateCheckTimer.start((updateInSecs + 5) * 1000);
	}
}
#endif

namespace {
	QChar _toHex(ushort v) {
		v = v & 0x000F;
		return QChar::fromLatin1((v >= 10) ? ('a' + (v - 10)) : ('0' + v));
	}
	ushort _fromHex(QChar c) {
		return ((c.unicode() >= uchar('a')) ? (c.unicode() - uchar('a') + 10) : (c.unicode() - uchar('0'))) & 0x000F;
	}

	QString _escapeTo7bit(const QString &str) {
		QString result;
		result.reserve(str.size() * 2);
		for (int i = 0, l = str.size(); i != l; ++i) {
			QChar ch(str.at(i));
			ushort uch(ch.unicode());
			if (uch < 32 || uch > 127 || uch == ushort(uchar('%'))) {
				result.append('%').append(_toHex(uch >> 12)).append(_toHex(uch >> 8)).append(_toHex(uch >> 4)).append(_toHex(uch));
			} else {
				result.append(ch);
			}
		}
		return result;
	}

	QString _escapeFrom7bit(const QString &str) {
		QString result;
		result.reserve(str.size());
		for (int i = 0, l = str.size(); i != l; ++i) {
			QChar ch(str.at(i));
			if (ch == QChar::fromLatin1('%') && i + 4 < l) {
				result.append(QChar(ushort((_fromHex(str.at(i + 1)) << 12) | (_fromHex(str.at(i + 2)) << 8) | (_fromHex(str.at(i + 3)) << 4) | _fromHex(str.at(i + 4)))));
				i += 4;
			} else {
				result.append(ch);
			}
		}
		return result;
	}
}

void Application::socketConnected() {
	DEBUG_LOG(("Application Info: socket connected, this is not the first application instance, sending show command.."));
	closing = true;
	QString commands;
	const QStringList &lst(cSendPaths());
	for (QStringList::const_iterator i = lst.cbegin(), e = lst.cend(); i != e; ++i) {
		commands += qsl("SEND:") + _escapeTo7bit(*i) + ';';
	}
	if (!cStartUrl().isEmpty()) {
		commands += qsl("OPEN:") + _escapeTo7bit(cStartUrl()) + ';';
	}
	commands += qsl("CMD:show;");
	DEBUG_LOG(("Application Info: writing commands %1").arg(commands));
	socket.write(commands.toLocal8Bit());
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
		DEBUG_LOG(("Application Error: failed to start listening to %1 server, error %2").arg(serverName).arg(int(server.serverError())));
		return App::quit();
	}

	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (!cNoStartUpdate() && checkReadyUpdate()) {
		cSetRestartingUpdate(true);
		DEBUG_LOG(("Application Info: installing update instead of starting app.."));
		return App::quit();
	}
	#endif

	startApp();
}

void Application::checkMapVersion() {
    if (Local::oldMapVersion() < AppVersion) {
		if (Local::oldMapVersion()) {
			QString versionFeatures;
			if (cDevVersion() && Local::oldMapVersion() < 9004) {
				versionFeatures = QString::fromUtf8("\xe2\x80\x94 New design for all modal windows\n\xe2\x80\x94 Toggle notifications from tray menu\n\xe2\x80\x94 Bug fixes and other minor improvements");// .replace('@', qsl("@") + QChar(0x200D));
			} else if (Local::oldMapVersion() < 9005) {
				versionFeatures = lang(lng_new_version_text).trimmed();
			} else {
				versionFeatures = lang(lng_new_version_minor).trimmed();
			}
			if (!versionFeatures.isEmpty()) {
				versionFeatures = lng_new_version_wrap(lt_version, QString::fromStdWString(AppVersionStr), lt_changes, versionFeatures, lt_link, qsl("https://desktop.telegram.org/#changelog"));
				window->serviceNotification(versionFeatures);
			}
		}
	}
}

void Application::startApp() {
	cChangeTimeFormat(QLocale::system().timeFormat(QLocale::ShortFormat));

	DEBUG_LOG(("Application Info: starting app.."));

	QMimeDatabase().mimeTypeForName(qsl("text/plain")); // create mime database

	window->createWinId();
	window->init();

	DEBUG_LOG(("Application Info: window created.."));

	initImageLinkManager();
	App::initMedia();

	Local::ReadMapState state = Local::readMap(QByteArray());
	if (state == Local::ReadMapPassNeeded) {
		cSetHasPasscode(true);
		DEBUG_LOG(("Application Info: passcode nneded.."));
	} else {
		DEBUG_LOG(("Application Info: local map read.."));
		MTP::start();
	}

	MTP::setStateChangedHandler(mtpStateChanged);
	MTP::setSessionResetHandler(mtpSessionReset);

	DEBUG_LOG(("Application Info: MTP started.."));

	DEBUG_LOG(("Application Info: showing."));
	if (state == Local::ReadMapPassNeeded) {
		window->setupPasscode(false);
	} else {
		if (MTP::authedId()) {
			window->setupMain(false);
		} else {
			window->setupIntro(false);
		}
	}
	window->firstShow();

	if (cStartToSettings()) {
		window->showSettings();
	}

	QNetworkProxyFactory::setUseSystemConfiguration(true);
	
	if (state != Local::ReadMapPassNeeded) {
		checkMapVersion();
	}

	window->updateIsActive(cOnlineFocusTimeout());
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
	QString startUrl;
	QStringList toSend;
	for (ClientSockets::iterator i = clients.begin(), e = clients.end(); i != e; ++i) {
		i->second.append(i->first->readAll());
		if (i->second.size()) {
			QString cmds(QString::fromLocal8Bit(i->second));
			int32 from = 0, l = cmds.length();
			for (int32 to = cmds.indexOf(QChar(';'), from); to >= from; to = (from < l) ? cmds.indexOf(QChar(';'), from) : -1) {
				QStringRef cmd(&cmds, from, to - from);
				if (cmd.startsWith(qsl("CMD:"))) {
					execExternal(cmds.mid(from + 4, to - from - 4));
					QByteArray response(qsl("RES:%1;").arg(QCoreApplication::applicationPid()).toUtf8());
					i->first->write(response.data(), response.size());
				} else if (cmd.startsWith(qsl("SEND:"))) {
					if (cSendPaths().isEmpty()) {
						toSend.append(_escapeFrom7bit(cmds.mid(from + 5, to - from - 5)));
					}
				} else if (cmd.startsWith(qsl("OPEN:"))) {
					if (cStartUrl().isEmpty()) {
						startUrl = _escapeFrom7bit(cmds.mid(from + 5, to - from - 5));
					}
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
	if (!toSend.isEmpty()) {
		QStringList paths(cSendPaths());
		paths.append(toSend);
		cSetSendPaths(paths);
	}
	if (!cSendPaths().isEmpty()) {
		if (App::wnd()) {
			App::wnd()->sendPaths();
		}
	}
	if (!startUrl.isEmpty()) {
		cSetStartUrl(startUrl);
	}
	if (!cStartUrl().isEmpty() && App::main() && App::self()) {
		App::main()->openLocalUrl(cStartUrl());
		cSetStartUrl(QString());
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
	deinitImageLinkManager();
	mainApp = 0;
	delete ::uploader;
	#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	delete updateReply;
	updateReply = 0;
	if (updateDownloader) updateDownloader->deleteLater();
	updateDownloader = 0;
	if (updateThread) updateThread->quit();
	updateThread = 0;
	#endif

	delete window;

	delete cChatBackground();
	cSetChatBackground(0);

	delete cChatDogImage();
	cSetChatDogImage(0);

	style::stopManager();
	
	delete _translator;
}

Application *Application::app() {
	return mainApp;
}

Window *Application::wnd() {
	return mainApp ? mainApp->window : 0;
}

QString Application::language() {
	if (!lng.length()) {
		lng = psCurrentLanguage();
	}
	if (!lng.length()) {
		lng = "en";
	}
	return lng;
}

int32 Application::languageId() {
	QByteArray l = language().toLatin1();
	for (int32 i = 0; i < languageCount; ++i) {
		if (l.at(0) == LanguageCodes[i][0] && l.at(1) == LanguageCodes[i][1]) {
			return i;
		}
	}
	return languageDefault;
}

MainWidget *Application::main() {
	return mainApp ? mainApp->window->mainWidget() : 0;
}
