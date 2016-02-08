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
				if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
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
				} else {
					if ((ev->key() == Qt::Key_W || ev->key() == Qt::Key_F4) && (ev->modifiers() & Qt::ControlModifier)) {
						if (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray) {
							App::wnd()->minimizeToTray();
							return true;
						} else {
							App::wnd()->close();
							return true;
						}
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

AppClass *AppObject = 0;

Application::Application(int &argc, char **argv) : QApplication(argc, argv)
, _secondInstance(false)
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
, _updateReply(0)
, _updateThread(0)
, _updateChecker(0)
#endif
{
	QByteArray d(QDir(cWorkingDir()).absolutePath().toUtf8());
	char h[33] = { 0 };
	hashMd5Hex(d.constData(), d.size(), h);
	_localServerName = psServerPrefix() + h + '-' + cGUIDStr();

	connect(&_localSocket, SIGNAL(connected()), this, SLOT(socketConnected()));
	connect(&_localSocket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));
	connect(&_localSocket, SIGNAL(error(QLocalSocket::LocalSocketError)), this, SLOT(socketError(QLocalSocket::LocalSocketError)));
	connect(&_localSocket, SIGNAL(bytesWritten(qint64)), this, SLOT(socketWritten(qint64)));
	connect(&_localSocket, SIGNAL(readyRead()), this, SLOT(socketReading()));
	connect(&_localServer, SIGNAL(newConnection()), this, SLOT(newInstanceConnected()));

	connect(this, SIGNAL(aboutToQuit()), this, SLOT(closeApplication()));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	connect(&_updateCheckTimer, SIGNAL(timeout()), this, SLOT(updateCheck()));
	connect(this, SIGNAL(updateFailed()), this, SLOT(onUpdateFailed()));
	connect(this, SIGNAL(updateReady()), this, SLOT(onUpdateReady()));
#endif

	if (cManyInstance()) {
		LOG(("Many instance allowed, starting.."));
		singleInstanceChecked();
	} else {
        LOG(("Connecting local socket to %1..").arg(_localServerName));
		_localSocket.connectToServer(_localServerName);
	}
}

Application::~Application() {
	App::setQuiting();

	Sandbox::finish();

	delete AppObject;

	_localSocket.close();
	closeApplication();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	delete _updateReply;
	_updateReply = 0;
	if (_updateChecker) _updateChecker->deleteLater();
	_updateChecker = 0;
	if (_updateThread) _updateThread->quit();
	_updateThread = 0;
#endif
}


void Application::socketConnected() {
	LOG(("Socket connected, this is not the first application instance, sending show command.."));
	_secondInstance = true;

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
	_localSocket.write(commands.toLatin1());
}

void Application::socketWritten(qint64/* bytes*/) {
	if (_localSocket.state() != QLocalSocket::ConnectedState) {
		LOG(("Socket is not connected %1").arg(_localSocket.state()));
		return;
	}
	if (_localSocket.bytesToWrite()) {
		return;
	}
	LOG(("Show command written, waiting response.."));
}

void Application::socketReading() {
	if (_localSocket.state() != QLocalSocket::ConnectedState) {
		LOG(("Socket is not connected %1").arg(_localSocket.state()));
		return;
	}
	_localSocketReadData.append(_localSocket.readAll());
	if (QRegularExpression("RES:(\\d+);").match(_localSocketReadData).hasMatch()) {
		uint64 pid = _localSocketReadData.mid(4, _localSocketReadData.length() - 5).toULongLong();
		psActivateProcess(pid);
		LOG(("Show command response received, pid = %1, activating and quiting..").arg(pid));
		return App::quit();
	}
}

void Application::socketError(QLocalSocket::LocalSocketError e) {
	if (App::quiting()) return;

	if (_secondInstance) {
		LOG(("Could not write show command, error %1, quiting..").arg(e));
		return App::quit();
	}

	if (e == QLocalSocket::ServerNotFoundError) {
		LOG(("This is the only instance of Telegram, starting server and app.."));
	} else {
		LOG(("Socket connect error %1, starting server and app..").arg(e));
	}
	_localSocket.close();

	psCheckLocalSocket(_localServerName);

	if (!_localServer.listen(_localServerName)) {
		LOG(("Failed to start listening to %1 server, error %2").arg(_localServerName).arg(int(_localServer.serverError())));
		return App::quit();
	}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (!cNoStartUpdate() && checkReadyUpdate()) {
		cSetRestartingUpdate(true);
		DEBUG_LOG(("Application Info: installing update instead of starting app.."));
		return App::quit();
	}
#endif

	singleInstanceChecked();
}

void Application::singleInstanceChecked() {
	if (cManyInstance()) {
		Logs::multipleInstances();
	}

	Sandbox::start();

	if (!Logs::started() || (!cManyInstance() && !Logs::instanceChecked())) {
		new NotStartedWindow();
	} else {
		SignalHandlers::Status status = SignalHandlers::start();
		if (status == SignalHandlers::CantOpen) {
			new NotStartedWindow();
		} else if (status == SignalHandlers::LastCrashed) {
			if (Sandbox::LastCrashDump().isEmpty()) { // don't handle bad closing for now
				if (SignalHandlers::restart() == SignalHandlers::CantOpen) {
					new NotStartedWindow();
				} else {
					Sandbox::launch();
				}
			} else {
				new LastCrashedWindow();
			}
		} else {
			Sandbox::launch();
		}
	}
}

void Application::socketDisconnected() {
	if (_secondInstance) {
		DEBUG_LOG(("Application Error: socket disconnected before command response received, quiting.."));
		return App::quit();
	}
}

void Application::newInstanceConnected() {
	DEBUG_LOG(("Application Info: new local socket connected"));
	for (QLocalSocket *client = _localServer.nextPendingConnection(); client; client = _localServer.nextPendingConnection()) {
		_localClients.push_back(LocalClient(client, QByteArray()));
		connect(client, SIGNAL(readyRead()), this, SLOT(readClients()));
		connect(client, SIGNAL(disconnected()), this, SLOT(removeClients()));
	}
}

void Application::readClients() {
	QString startUrl;
	QStringList toSend;
	for (LocalClients::iterator i = _localClients.begin(), e = _localClients.end(); i != e; ++i) {
		i->second.append(i->first->readAll());
		if (i->second.size()) {
			QString cmds(QString::fromLatin1(i->second));
			int32 from = 0, l = cmds.length();
			for (int32 to = cmds.indexOf(QChar(';'), from); to >= from; to = (from < l) ? cmds.indexOf(QChar(';'), from) : -1) {
				QStringRef cmd(&cmds, from, to - from);
				if (cmd.startsWith(qsl("CMD:"))) {
					Sandbox::execExternal(cmds.mid(from + 4, to - from - 4));
					QByteArray response(qsl("RES:%1;").arg(QCoreApplication::applicationPid()).toLatin1());
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
	DEBUG_LOG(("Application Info: remove clients slot called, clients %1").arg(_localClients.size()));
	for (LocalClients::iterator i = _localClients.begin(), e = _localClients.end(); i != e;) {
		if (i->first->state() != QLocalSocket::ConnectedState) {
			DEBUG_LOG(("Application Info: removing client"));
			i = _localClients.erase(i);
			e = _localClients.end();
		} else {
			++i;
		}
	}
}

void Application::closeApplication() {
	_localServer.close();
	for (LocalClients::iterator i = _localClients.begin(), e = _localClients.end(); i != e; ++i) {
		disconnect(i->first, SIGNAL(disconnected()), this, SLOT(removeClients()));
		i->first->close();
	}
	_localClients.clear();

	MTP::stop();
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void Application::updateCheck() {
	startUpdateCheck(false);
}

void Application::updateGotCurrent() {
	if (!_updateReply || _updateThread) return;

	cSetLastUpdateCheck(unixtime());
	QRegularExpressionMatch m = QRegularExpression(qsl("^\\s*(\\d+)\\s*:\\s*([\\x21-\\x7f]+)\\s*$")).match(QString::fromLatin1(_updateReply->readAll()));
	if (m.hasMatch()) {
		uint64 currentVersion = m.captured(1).toULongLong();
		QString url = m.captured(2);
		bool betaVersion = false;
		if (url.startsWith(qstr("beta_"))) {
			betaVersion = true;
			url = url.mid(5) + '_' + countBetaVersionSignature(currentVersion);
		}
		if ((!betaVersion || cBetaVersion()) && currentVersion > (betaVersion ? cBetaVersion() : uint64(AppVersion))) {
			_updateThread = new QThread();
			connect(_updateThread, SIGNAL(finished()), _updateThread, SLOT(deleteLater()));
			_updateChecker = new UpdateChecker(_updateThread, url);
			_updateThread->start();
		}
	}
	if (_updateReply) _updateReply->deleteLater();
	_updateReply = 0;
	if (!_updateThread) {
		QDir updates(cWorkingDir() + "tupdates");
		if (updates.exists()) {
			QFileInfoList list = updates.entryInfoList(QDir::Files);
			for (QFileInfoList::iterator i = list.begin(), e = list.end(); i != e; ++i) {
                if (QRegularExpression("^(tupdate|tmacupd|tmac32upd|tlinuxupd|tlinux32upd)\\d+(_[a-z\\d]+)?$", QRegularExpression::CaseInsensitiveOption).match(i->fileName()).hasMatch()) {
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
	if (_updateReply) _updateReply->deleteLater();
	_updateReply = 0;

	emit updateFailed();
	startUpdateCheck(true);
}

void Application::onUpdateReady() {
	if (_updateChecker) {
		_updateChecker->deleteLater();
		_updateChecker = 0;
	}
	_updateCheckTimer.stop();

	cSetLastUpdateCheck(unixtime());
	Local::writeSettings();
}

void Application::onUpdateFailed() {
	if (_updateChecker) {
		_updateChecker->deleteLater();
		_updateChecker = 0;
		if (_updateThread) _updateThread->quit();
		_updateThread = 0;
	}

	cSetLastUpdateCheck(unixtime());
	Local::writeSettings();
}

Application::UpdatingState Application::updatingState() {
	if (!_updateThread) return Application::UpdatingNone;
	if (!_updateChecker) return Application::UpdatingReady;
	return Application::UpdatingDownload;
}

int32 Application::updatingSize() {
	if (!_updateChecker) return 0;
	return _updateChecker->size();
}

int32 Application::updatingReady() {
	if (!_updateChecker) return 0;
	return _updateChecker->ready();
}

void Application::stopUpdate() {
	if (_updateReply) {
		_updateReply->abort();
		_updateReply->deleteLater();
		_updateReply = 0;
	}
	if (_updateChecker) {
		_updateChecker->deleteLater();
		_updateChecker = 0;
		if (_updateThread) _updateThread->quit();
		_updateThread = 0;
	}
}

void Application::startUpdateCheck(bool forceWait) {
	_updateCheckTimer.stop();
	if (_updateThread || _updateReply || !cAutoUpdate()) return;

	int32 constDelay = cBetaVersion() ? 600 : UpdateDelayConstPart, randDelay = cBetaVersion() ? 300 : UpdateDelayRandPart;
	int32 updateInSecs = cLastUpdateCheck() + constDelay + int32(rand() % randDelay) - unixtime();
	bool sendRequest = (updateInSecs <= 0 || updateInSecs > (constDelay + randDelay));
	if (!sendRequest && !forceWait) {
		QDir updates(cWorkingDir() + "tupdates");
		if (updates.exists()) {
			QFileInfoList list = updates.entryInfoList(QDir::Files);
			for (QFileInfoList::iterator i = list.begin(), e = list.end(); i != e; ++i) {
				if (QRegularExpression("^(tupdate|tmacupd|tmac32upd|tlinuxupd|tlinux32upd)\\d+(_[a-z\\d]+)?$", QRegularExpression::CaseInsensitiveOption).match(i->fileName()).hasMatch()) {
					sendRequest = true;
				}
			}
		}
	}
	if (cManyInstance() && !cDebug()) return; // only main instance is updating

	if (sendRequest) {
		QUrl url(cUpdateURL());
		if (cBetaVersion()) {
			url.setQuery(qsl("version=%1&beta=%2").arg(AppVersion).arg(cBetaVersion()));
		} else if (cDevVersion()) {
			url.setQuery(qsl("version=%1&dev=1").arg(AppVersion));
		} else {
			url.setQuery(qsl("version=%1").arg(AppVersion));
		}
		QString u = url.toString();
		QNetworkRequest checkVersion(url);
		if (_updateReply) _updateReply->deleteLater();

		App::setProxySettings(_updateManager);
		_updateReply = _updateManager.get(checkVersion);
		connect(_updateReply, SIGNAL(finished()), this, SLOT(updateGotCurrent()));
		connect(_updateReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(updateFailedCurrent(QNetworkReply::NetworkError)));
		emit updateChecking();
	} else {
		_updateCheckTimer.start((updateInSecs + 5) * 1000);
	}
}

#endif

inline Application *application() {
	return qobject_cast<Application*>(QApplication::instance());
}

namespace Sandbox {

	QRect availableGeometry() {
		if (Application *a = application()) {
			return a->desktop()->availableGeometry();
		}
		return QDesktopWidget().availableGeometry();
	}

	QRect screenGeometry(const QPoint &p) {
		if (Application *a = application()) {
			return a->desktop()->screenGeometry(p);
		}
		return QDesktopWidget().screenGeometry(p);
	}

	void setActiveWindow(QWidget *window) {
		if (Application *a = application()) {
			a->setActiveWindow(window);
		}
	}

	bool isSavingSession() {
		if (Application *a = application()) {
			return a->isSavingSession();
		}
		return false;
	}

	void installEventFilter(QObject *filter) {
		if (Application *a = application()) {
			a->installEventFilter(filter);
		}
	}

	void execExternal(const QString &cmd) {
		DEBUG_LOG(("Application Info: executing external command '%1'").arg(cmd));
		if (cmd == "show") {
			if (App::wnd()) {
				App::wnd()->activate();
			} else if (PreLaunchWindow::instance()) {
				PreLaunchWindow::instance()->activate();
			}
		}
	}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

	void startUpdateCheck() {
		if (Application *a = application()) {
			return a->startUpdateCheck(false);
		}
	}

	void stopUpdate() {
		if (Application *a = application()) {
			return a->stopUpdate();
		}
	}

	Application::UpdatingState updatingState() {
		if (Application *a = application()) {
			return a->updatingState();
		}
		return Application::UpdatingNone;
	}

	int32 updatingSize() {
		if (Application *a = application()) {
			return a->updatingSize();
		}
		return 0;
	}

	int32 updatingReady() {
		if (Application *a = application()) {
			return a->updatingReady();
		}
		return 0;
	}

	void updateChecking() {
		if (Application *a = application()) {
			emit a->updateChecking();
		}
	}

	void updateLatest() {
		if (Application *a = application()) {
			emit a->updateLatest();
		}
	}

	void updateProgress(qint64 ready, qint64 total) {
		if (Application *a = application()) {
			emit a->updateProgress(ready, total);
		}
	}

	void updateFailed() {
		if (Application *a = application()) {
			emit a->updateFailed();
		}
	}

	void updateReady() {
		if (Application *a = application()) {
			emit a->updateReady();
		}
	}

#endif

	void connect(const char *signal, QObject *object, const char *method) {
		if (Application *a = application()) {
			a->connect(a, signal, object, method);
		}
	}

	void launch() {
		t_assert(application() != 0);

		float64 dpi = Application::primaryScreen()->logicalDotsPerInch();
		if (dpi <= 108) { // 0-96-108
			cSetScreenScale(dbisOne);
		} else if (dpi <= 132) { // 108-120-132
			cSetScreenScale(dbisOneAndQuarter);
		} else if (dpi <= 168) { // 132-144-168
			cSetScreenScale(dbisOneAndHalf);
		} else { // 168-192-inf
			cSetScreenScale(dbisTwo);
		}

		if (application()->devicePixelRatio() > 1) {
			cSetRetina(true);
			cSetRetinaFactor(application()->devicePixelRatio());
			cSetIntRetinaFactor(int32(cRetinaFactor()));
			cSetConfigScale(dbisOne);
			cSetRealScale(dbisOne);
		}

		new AppClass();
	}

}

AppClass::AppClass() : QObject()
, _uploader(0) {
	AppObject = this;

	Fonts::start();

	ThirdParty::start();
	Global::start();
	Local::start();
	if (Local::oldSettingsVersion() < AppVersion) {
		psNewVersion();
	}

	if (cLaunchMode() == LaunchModeAutoStart && !cAutoStart()) {
		psAutoStart(false, true);
		application()->quit();
		return;
	}

	application()->installEventFilter(new EventFilterForKeys(this));

	if (cRetina()) {
		cSetConfigScale(dbisOne);
		cSetRealScale(dbisOne);
	}

	if (cLang() < languageTest) {
		cSetLang(Sandbox::LangSystem());
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

	application()->installTranslator(_translator = new Translator());

	style::startManager();
	anim::startManager();
	historyInit();

	DEBUG_LOG(("Application Info: inited.."));

	application()->installNativeEventFilter(psNativeEventFilter());

	Sandbox::connect(SIGNAL(applicationStateChanged(Qt::ApplicationState)), this, SLOT(onAppStateChanged(Qt::ApplicationState)));

	connect(&_mtpUnpauseTimer, SIGNAL(timeout()), this, SLOT(doMtpUnpause()));

	connect(&killDownloadSessionsTimer, SIGNAL(timeout()), this, SLOT(killDownloadSessions()));

	cChangeTimeFormat(QLocale::system().timeFormat(QLocale::ShortFormat));

	DEBUG_LOG(("Application Info: starting app.."));

	QMimeDatabase().mimeTypeForName(qsl("text/plain")); // create mime database

	_window.createWinId();
	_window.init();

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
		_window.setupPasscode(false);
	} else {
		if (MTP::authedId()) {
			_window.setupMain(false);
		} else {
			_window.setupIntro(false);
		}
	}
	_window.firstShow();

	if (cStartToSettings()) {
		_window.showSettings();
	}

	QNetworkProxyFactory::setUseSystemConfiguration(true);

	if (state != Local::ReadMapPassNeeded) {
		checkMapVersion();
	}

	_window.updateIsActive(cOnlineFocusTimeout());
}

void AppClass::regPhotoUpdate(const PeerId &peer, const FullMsgId &msgId) {
	photoUpdates.insert(msgId, peer);
}

void AppClass::clearPhotoUpdates() {
	photoUpdates.clear();
}

bool AppClass::isPhotoUpdating(const PeerId &peer) {
	for (QMap<FullMsgId, PeerId>::iterator i = photoUpdates.begin(), e = photoUpdates.end(); i != e; ++i) {
		if (i.value() == peer) {
			return true;
		}
	}
	return false;
}

void AppClass::cancelPhotoUpdate(const PeerId &peer) {
	for (QMap<FullMsgId, PeerId>::iterator i = photoUpdates.begin(), e = photoUpdates.end(); i != e;) {
		if (i.value() == peer) {
			i = photoUpdates.erase(i);
		} else {
			++i;
		}
	}
}

void AppClass::mtpPause() {
	MTP::pause();
	_mtpUnpauseTimer.start(st::slideDuration * 2);
}

void AppClass::mtpUnpause() {
	_mtpUnpauseTimer.start(1);
}

void AppClass::doMtpUnpause() {
	MTP::unpause();
}

void AppClass::selfPhotoCleared(const MTPUserProfilePhoto &result) {
	if (!App::self()) return;
	App::self()->setPhoto(result);
	emit peerPhotoDone(App::self()->id);
}

void AppClass::chatPhotoCleared(PeerId peer, const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	cancelPhotoUpdate(peer);
	emit peerPhotoDone(peer);
}

void AppClass::selfPhotoDone(const MTPphotos_Photo &result) {
	if (!App::self()) return;
	const MTPDphotos_photo &photo(result.c_photos_photo());
	App::feedPhoto(photo.vphoto);
	App::feedUsers(photo.vusers);
	cancelPhotoUpdate(App::self()->id);
	emit peerPhotoDone(App::self()->id);
}

void AppClass::chatPhotoDone(PeerId peer, const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	cancelPhotoUpdate(peer);
	emit peerPhotoDone(peer);
}

bool AppClass::peerPhotoFail(PeerId peer, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	LOG(("Application Error: update photo failed %1: %2").arg(error.type()).arg(error.description()));
	cancelPhotoUpdate(peer);
	emit peerPhotoFail(peer);
	return true;
}

void AppClass::peerClearPhoto(PeerId id) {
	if (MTP::authedId() && peerToUser(id) == MTP::authedId()) {
		MTP::send(MTPphotos_UpdateProfilePhoto(MTP_inputPhotoEmpty(), MTP_inputPhotoCropAuto()), rpcDone(&AppClass::selfPhotoCleared), rpcFail(&AppClass::peerPhotoFail, id));
	} else if (peerIsChat(id)) {
		MTP::send(MTPmessages_EditChatPhoto(peerToBareMTPInt(id), MTP_inputChatPhotoEmpty()), rpcDone(&AppClass::chatPhotoCleared, id), rpcFail(&AppClass::peerPhotoFail, id));
	} else if (peerIsChannel(id)) {
		if (ChannelData *channel = App::channelLoaded(id)) {
			MTP::send(MTPchannels_EditPhoto(channel->inputChannel, MTP_inputChatPhotoEmpty()), rpcDone(&AppClass::chatPhotoCleared, id), rpcFail(&AppClass::peerPhotoFail, id));
		}
	}
}

void AppClass::killDownloadSessionsStart(int32 dc) {
	if (killDownloadSessionTimes.constFind(dc) == killDownloadSessionTimes.cend()) {
		killDownloadSessionTimes.insert(dc, getms() + MTPAckSendWaiting + MTPKillFileSessionTimeout);
	}
	if (!killDownloadSessionsTimer.isActive()) {
		killDownloadSessionsTimer.start(MTPAckSendWaiting + MTPKillFileSessionTimeout + 5);
	}
}

void AppClass::killDownloadSessionsStop(int32 dc) {
	killDownloadSessionTimes.remove(dc);
	if (killDownloadSessionTimes.isEmpty() && killDownloadSessionsTimer.isActive()) {
		killDownloadSessionsTimer.stop();
	}
}

void AppClass::checkLocalTime() {
	if (App::main()) App::main()->checkLastUpdate(checkms());
}

void AppClass::onAppStateChanged(Qt::ApplicationState state) {
	checkLocalTime();
	_window.updateIsActive((state == Qt::ApplicationActive) ? cOnlineFocusTimeout() : cOfflineBlurTimeout());
}

void AppClass::killDownloadSessions() {
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

void AppClass::photoUpdated(const FullMsgId &msgId, const MTPInputFile &file) {
	if (!App::self()) return;

	QMap<FullMsgId, PeerId>::iterator i = photoUpdates.find(msgId);
	if (i != photoUpdates.end()) {
		PeerId id = i.value();
		if (MTP::authedId() && peerToUser(id) == MTP::authedId()) {
			MTP::send(MTPphotos_UploadProfilePhoto(file, MTP_string(""), MTP_inputGeoPointEmpty(), MTP_inputPhotoCrop(MTP_double(0), MTP_double(0), MTP_double(100))), rpcDone(&AppClass::selfPhotoDone), rpcFail(&AppClass::peerPhotoFail, id));
		} else if (peerIsChat(id)) {
			History *hist = App::history(id);
			hist->sendRequestId = MTP::send(MTPmessages_EditChatPhoto(hist->peer->asChat()->inputChat, MTP_inputChatUploadedPhoto(file, MTP_inputPhotoCrop(MTP_double(0), MTP_double(0), MTP_double(100)))), rpcDone(&AppClass::chatPhotoDone, id), rpcFail(&AppClass::peerPhotoFail, id), 0, 0, hist->sendRequestId);
		} else if (peerIsChannel(id)) {
			History *hist = App::history(id);
			hist->sendRequestId = MTP::send(MTPchannels_EditPhoto(hist->peer->asChannel()->inputChannel, MTP_inputChatUploadedPhoto(file, MTP_inputPhotoCrop(MTP_double(0), MTP_double(0), MTP_double(100)))), rpcDone(&AppClass::chatPhotoDone, id), rpcFail(&AppClass::peerPhotoFail, id), 0, 0, hist->sendRequestId);
		}
	}
}

void AppClass::onSwitchDebugMode() {
	if (cDebug()) {
		QFile(cWorkingDir() + qsl("tdata/withdebug")).remove();
		cSetDebug(false);
		cSetRestarting(true);
		cSetRestartingToSettings(true);
		App::quit();
	} else {
		cSetDebug(true);
		DEBUG_LOG(("Debug logs started."));
		QFile f(cWorkingDir() + qsl("tdata/withdebug"));
		if (f.open(QIODevice::WriteOnly)) {
			f.write("1");
			f.close();
		}
		Ui::hideLayer();
	}
}

void AppClass::onSwitchTestMode() {
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

FileUploader *AppClass::uploader() {
	if (!_uploader && !App::quiting()) _uploader = new FileUploader();
	return _uploader;
}

void AppClass::uploadProfilePhoto(const QImage &tosend, const PeerId &peerId) {
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

	ReadyLocalMedia ready(PreparePhoto, file, filename, filesize, data, id, id, qsl("jpg"), peerId, photo, MTP_audioEmpty(MTP_long(0)), photoThumbs, MTP_documentEmpty(MTP_long(0)), jpeg, false, false, 0);

	connect(App::uploader(), SIGNAL(photoReady(const FullMsgId&, const MTPInputFile&)), App::app(), SLOT(photoUpdated(const FullMsgId&, const MTPInputFile&)), Qt::UniqueConnection);

	FullMsgId newId(peerToChannel(peerId), clientMsgId());
	App::app()->regPhotoUpdate(peerId, newId);
	App::uploader()->uploadMedia(newId, ready);
}

void AppClass::checkMapVersion() {
    if (Local::oldMapVersion() < AppVersion) {
		if (Local::oldMapVersion()) {
			QString versionFeatures;
			if (cDevVersion() && Local::oldMapVersion() < 9020) {
				if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
					versionFeatures = QString::fromUtf8("\xe2\x80\x94 Testing new crash reporting system\n\xe2\x80\x94 Conversation history is centered in wide windows\n\xe2\x80\x94 New cute link and timestamp tooltips design\n\xe2\x80\x94 Bug fixes and other minor improvements");// .replace('@', qsl("@") + QChar(0x200D));
				} else {
					versionFeatures = QString::fromUtf8("\xe2\x80\x94 Testing new crash reporting system\n\xe2\x80\x94 Conversation history is centered in wide windows\n\xe2\x80\x94 New cute link and timestamp tooltips design\n\xe2\x80\x94 Ctrl+W or Ctrl+F4 closes Telegram window\n\xe2\x80\x94 Bug fixes and other minor improvements");// .replace('@', qsl("@") + QChar(0x200D));
				}
			} else if (Local::oldMapVersion() < 9016) {
				versionFeatures = lng_new_version_text(lt_gifs_link, qsl("https://telegram.org/blog/gif-revolution"), lt_bots_link, qsl("https://telegram.org/blog/inline-bots")).trimmed();
			} else {
				versionFeatures = lang(lng_new_version_minor).trimmed();
			}
			if (!versionFeatures.isEmpty()) {
				versionFeatures = lng_new_version_wrap(lt_version, QString::fromStdWString(AppVersionStr), lt_changes, versionFeatures, lt_link, qsl("https://desktop.telegram.org/#changelog"));
				_window.serviceNotification(versionFeatures);
			}
		}
	}
	if (cNeedConfigResave()) {
		Local::writeUserSettings();
	}
}

AppClass::~AppClass() {
	_window.setParent(0);
	anim::stopManager();

	stopWebLoadManager();
	App::deinitMedia();
	deinitImageLinkManager();

	AppObject = 0;
	deleteAndMark(_uploader);
	deleteAndMark(_translator);

	delete cChatBackground();
	cSetChatBackground(0);

	delete cChatDogImage();
	cSetChatDogImage(0);

	style::stopManager();

	Local::finish();
	Global::finish();
	ThirdParty::finish();
}

AppClass *AppClass::app() {
	return AppObject;
}

Window *AppClass::wnd() {
	return AppObject ? &AppObject->_window : 0;
}

MainWidget *AppClass::main() {
	return AppObject ? AppObject->_window.mainWidget() : 0;
}
