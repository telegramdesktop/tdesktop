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
#include "stdafx.h"
#include "pspecific.h"

#include "lang.h"
#include "application.h"
#include "mainwidget.h"
#include "historywidget.h"

namespace {
	bool frameless = true;
	bool finished = true;

    class _PsEventFilter : public QAbstractNativeEventFilter {
	public:
		_PsEventFilter() {
		}

		bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) {
			Window *wnd = Application::wnd();
			if (!wnd) return false;

			return false;
		}
	};
    _PsEventFilter *_psEventFilter = 0;

};

void MacPrivate::activeSpaceChanged() {
    if (App::wnd()) {
        App::wnd()->notifyActivateAll();
    }
}

void MacPrivate::darkModeChanged() {
	if (App::wnd()) {
		App::wnd()->psUpdateCounter();
	}
}

void MacPrivate::notifyClicked(unsigned long long peer) {
    History *history = App::history(PeerId(peer));

    App::wnd()->showFromTray();
    App::wnd()->hideSettings();
    App::main()->showPeer(history->peer->id, false, true);
    App::wnd()->notifyClear(history);
}

void MacPrivate::notifyReplied(unsigned long long peer, const char *str) {
    History *history = App::history(PeerId(peer));
    
    App::main()->sendMessage(history, QString::fromUtf8(str));
}

PsMainWindow::PsMainWindow(QWidget *parent) : QMainWindow(parent),
posInited(false), trayIcon(0), trayIconMenu(0), icon256(qsl(":/gui/art/icon256.png")), iconbig256(qsl(":/gui/art/iconbig256.png")), wndIcon(QPixmap::fromImage(iconbig256)),
psLogout(0), psUndo(0), psRedo(0), psCut(0), psCopy(0), psPaste(0), psDelete(0), psSelectAll(0), psContacts(0), psAddContact(0), psNewGroup(0), psShowTelegram(0) {
	QImage tray(qsl(":/gui/art/osxtray.png"));
	trayImg = tray.copy(0, cRetina() ? 0 : tray.width() / 2, tray.width() / (cRetina() ? 2 : 4), tray.width() / (cRetina() ? 2 : 4));
	trayImgSel = tray.copy(tray.width() / (cRetina() ? 2 : 4), cRetina() ? 0 : tray.width() / 2, tray.width() / (cRetina() ? 2 : 4), tray.width() / (cRetina() ? 2 : 4));
    connect(&psIdleTimer, SIGNAL(timeout()), this, SLOT(psIdleTimeout()));
    psIdleTimer.setSingleShot(false);
}

void PsMainWindow::psNotIdle() const {
	psIdleTimer.stop();
	if (psIdle) {
		psIdle = false;
		if (App::main()) App::main()->setOnline();
		if (App::wnd()) App::wnd()->checkHistoryActivation();
	}
}

QImage PsMainWindow::psTrayIcon(bool selected) const {
	return selected ? trayImgSel : trayImg;
}

void PsMainWindow::psIdleTimeout() {
    int64 idleTime = objc_idleTime();
    if (idleTime >= 0) {
        if (idleTime <= IdleMsecs) {
			psNotIdle();
		}
    } else { // error
		psNotIdle();
	}
}

void PsMainWindow::psShowTrayMenu() {
}

bool PsMainWindow::psIsOnline(int state) const {
	if (state < 0) state = this->windowState();
	if (state & Qt::WindowMinimized) {
		return false;
	} else if (!isVisible()) {
		return false;
	}
    int64 idleTime = objc_idleTime();
    LOG(("App Info: idle time %1").arg(idleTime));
    if (idleTime >= 0) {
        if (idleTime > IdleMsecs) {
			if (!psIdle) {
				psIdle = true;
				psIdleTimer.start(900);
			}
			return false;
		} else {
			psNotIdle();
		}
    } else { // error
		psNotIdle();
	}
	return true;
}

bool PsMainWindow::psIsActive(int state) const {
	if (state < 0) state = this->windowState();
    return isActiveWindow() && isVisible() && !(state & Qt::WindowMinimized) && !psIdle;
}

void PsMainWindow::psRefreshTaskbarIcon() {
}

void PsMainWindow::psUpdateWorkmode() {
	setupTrayIcon();
	if (cWorkMode() == dbiwmWindowOnly) {
		if (trayIcon) {
			trayIcon->setContextMenu(0);
			trayIcon->deleteLater();
		}
		trayIcon = 0;
	}
	setWindowIcon(wndIcon);
}

void _placeCounter(QImage &img, int size, int count, style::color bg, style::color color) {
	if (!count) return;

	QPainter p(&img);
	QString cnt = (count < 100) ? QString("%1").arg(count) : QString("..%1").arg(count % 100, 2, 10, QChar('0'));
	int32 cntSize = cnt.size();

	p.setBrush(bg->b);
	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::Antialiasing);
	int32 fontSize, skip;
	if (size == 22) {
		skip = 1;
		fontSize = 8;
	} else {
		skip = 2;
		fontSize = 16;
	}
	style::font f(fontSize);
	int32 w = f->m.width(cnt), d, r;
	if (size == 22) {
		d = (cntSize < 2) ? 3 : 2;
		r = (cntSize < 2) ? 6 : 5;
	} else {
		d = (cntSize < 2) ? 6 : 5;
		r = (cntSize < 2) ? 9 : 11;
	}
	p.drawRoundedRect(QRect(size - w - d * 2 - skip, size - f->height - skip, w + d * 2, f->height), r, r);

	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.setFont(f->f);
	p.setPen(color->p);
	p.drawText(size - w - d - skip, size - f->height + f->ascent - skip, cnt);
}

void PsMainWindow::psUpdateCounter() {
	int32 counter = App::histories().unreadFull;

    setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));
	setWindowIcon(wndIcon);

    QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
    _private.setWindowBadge(counter ? cnt : QString());

	if (trayIcon) {
		bool dm = objc_darkMode(), important = (App::histories().unreadMuted < counter);
		style::color bg = important ? st::counterBG : st::counterMuteBG;
		QIcon icon;
		QImage img(psTrayIcon(dm)), imgsel(psTrayIcon(true));
		img.detach();
		imgsel.detach();
		int32 size = cRetina() ? 44 : 22;
		_placeCounter(img, size, counter, bg, (dm && !important) ? st::counterMacInvColor : st::counterColor);
		_placeCounter(imgsel, size, counter, st::white, st::counterMacInvColor);
		icon.addPixmap(QPixmap::fromImage(img));
		icon.addPixmap(QPixmap::fromImage(imgsel), QIcon::Selected);
		trayIcon->setIcon(icon);
	}
}

void PsMainWindow::psUpdateDelegate() {
	_private.updateDelegate();
}

void PsMainWindow::psInitSize() {
	setMinimumWidth(st::wndMinWidth);
	setMinimumHeight(st::wndMinHeight);

	TWindowPos pos(cWindowPos());
	QRect avail(QDesktopWidget().availableGeometry());
	bool maximized = false;
	QRect geom(avail.x() + (avail.width() - st::wndDefWidth) / 2, avail.y() + (avail.height() - st::wndDefHeight) / 2, st::wndDefWidth, st::wndDefHeight);
	if (pos.w && pos.h) {
		QList<QScreen*> screens = App::app()->screens();
		for (QList<QScreen*>::const_iterator i = screens.cbegin(), e = screens.cend(); i != e; ++i) {
			QByteArray name = (*i)->name().toUtf8();
			if (pos.moncrc == hashCrc32(name.constData(), name.size())) {
				QRect screen((*i)->geometry());
				int32 w = screen.width(), h = screen.height();
				if (w >= st::wndMinWidth && h >= st::wndMinHeight) {
					if (pos.w > w) pos.w = w;
					if (pos.h > h) pos.h = h;
					pos.x += screen.x();
					pos.y += screen.y();
					if (pos.x < screen.x() + screen.width() - 10 && pos.y < screen.y() + screen.height() - 10) {
						geom = QRect(pos.x, pos.y, pos.w, pos.h);
					}
				}
				break;
			}
		}

		if (pos.y < 0) pos.y = 0;
		maximized = pos.maximized;
	}
	setGeometry(geom);
}

void PsMainWindow::psInitFrameless() {
    psUpdatedPositionTimer.setSingleShot(true);
	connect(&psUpdatedPositionTimer, SIGNAL(timeout()), this, SLOT(psSavePosition()));

	if (frameless) {
		//setWindowFlags(Qt::FramelessWindowHint);
	}

    connect(windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(psStateChanged(Qt::WindowState)));
}

void PsMainWindow::psSavePosition(Qt::WindowState state) {
    if (state == Qt::WindowActive) state = windowHandle()->windowState();
	if (state == Qt::WindowMinimized || !posInited) return;

	TWindowPos pos(cWindowPos()), curPos = pos;

	if (state == Qt::WindowMaximized) {
		curPos.maximized = 1;
	} else {
		QRect r(geometry());
		curPos.x = r.x();
		curPos.y = r.y();
		curPos.w = r.width();
		curPos.h = r.height();
		curPos.maximized = 0;
	}

	int px = curPos.x + curPos.w / 2, py = curPos.y + curPos.h / 2, d = 0;
	QScreen *chosen = 0;
	QList<QScreen*> screens = App::app()->screens();
	for (QList<QScreen*>::const_iterator i = screens.cbegin(), e = screens.cend(); i != e; ++i) {
		int dx = (*i)->geometry().x() + (*i)->geometry().width() / 2 - px; if (dx < 0) dx = -dx;
		int dy = (*i)->geometry().y() + (*i)->geometry().height() / 2 - py; if (dy < 0) dy = -dy;
		if (!chosen || dx + dy < d) {
			d = dx + dy;
			chosen = *i;
		}
	}
	if (chosen) {
		curPos.x -= chosen->geometry().x();
		curPos.y -= chosen->geometry().y();
		QByteArray name = chosen->name().toUtf8();
		curPos.moncrc = hashCrc32(name.constData(), name.size());
	}

	if (curPos.w >= st::wndMinWidth && curPos.h >= st::wndMinHeight) {
		if (curPos.x != pos.x || curPos.y != pos.y || curPos.w != pos.w || curPos.h != pos.h || curPos.moncrc != pos.moncrc || curPos.maximized != pos.maximized) {
			cSetWindowPos(curPos);
			App::writeConfig();
		}
    }
}

void PsMainWindow::psUpdatedPosition() {
    psUpdatedPositionTimer.start(SaveWindowPositionTimeout);
}

void PsMainWindow::psStateChanged(Qt::WindowState state) {
	psUpdateSysMenu(state);
	psUpdateMargins();
//    if (state == Qt::WindowMinimized && GetWindowLong(ps_hWnd, GWL_HWNDPARENT)) {
//		App::wnd()->minimizeToTray();
//    }
    psSavePosition(state);
}

void PsMainWindow::psFirstShow() {
	finished = false;

    psUpdateMargins();

	bool showShadows = true;

	show();
    _private.enableShadow(winId());
	if (cWindowPos().maximized) {
		setWindowState(Qt::WindowMaximized);
	}

	if (cFromAutoStart()) {
		if (cStartMinimized()) {
			setWindowState(Qt::WindowMinimized);
			if (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray) {
				hide();
			} else {
				show();
			}
			showShadows = false;
		} else {
			show();
		}
	} else {
		show();
	}
	posInited = true;

	// init global menu
	QMenu *main = psMainMenu.addMenu(qsl("Telegram"));
	main->addAction(lang(lng_mac_menu_about), App::wnd()->getTitle(), SLOT(onAbout()))->setMenuRole(QAction::AboutQtRole);
	main->addSeparator();
	QAction *prefs = main->addAction(lang(lng_mac_menu_preferences), App::wnd(), SLOT(showSettings()));
	prefs->setMenuRole(QAction::PreferencesRole);

	QMenu *file = psMainMenu.addMenu(lang(lng_mac_menu_file));
	psLogout = file->addAction(lang(lng_mac_menu_logout), App::wnd(), SLOT(onLogout()));

	QMenu *edit = psMainMenu.addMenu(lang(lng_mac_menu_edit));
	psUndo = edit->addAction(lang(lng_mac_menu_undo), this, SLOT(psMacUndo()), QKeySequence::Undo);
	psRedo = edit->addAction(lang(lng_mac_menu_redo), this, SLOT(psMacRedo()), QKeySequence::Redo);
	edit->addSeparator();
	psCut = edit->addAction(lang(lng_mac_menu_cut), this, SLOT(psMacCut()), QKeySequence::Cut);
	psCopy = edit->addAction(lang(lng_mac_menu_copy), this, SLOT(psMacCopy()), QKeySequence::Copy);
	psPaste = edit->addAction(lang(lng_mac_menu_paste), this, SLOT(psMacPaste()), QKeySequence::Paste);
	psDelete = edit->addAction(lang(lng_mac_menu_delete), this, SLOT(psMacDelete()), QKeySequence(Qt::ControlModifier | Qt::Key_Backspace));
	edit->addSeparator();
	psSelectAll = edit->addAction(lang(lng_mac_menu_select_all), this, SLOT(psMacSelectAll()), QKeySequence::SelectAll);

	QMenu *window = psMainMenu.addMenu(lang(lng_mac_menu_window));
	psContacts = window->addAction(lang(lng_mac_menu_contacts), App::wnd()->getTitle(), SLOT(onContacts()));
	psAddContact = window->addAction(lang(lng_mac_menu_add_contact), App::wnd(), SLOT(onShowAddContact()));
	window->addSeparator();
	psNewGroup = window->addAction(lang(lng_mac_menu_new_group), App::wnd(), SLOT(onShowNewGroup()));
	window->addSeparator();
	psShowTelegram = window->addAction(lang(lng_mac_menu_show), App::wnd(), SLOT(showFromTray()));

	psMacUpdateMenu();
}

namespace {
	void _sendKeySequence(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
		QWidget *focused = QApplication::focusWidget();
		if (qobject_cast<QLineEdit*>(focused) || qobject_cast<FlatTextarea*>(focused) || qobject_cast<HistoryList*>(focused)) {
			QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyPress, key, modifiers));
			QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyRelease, key, modifiers));
		}
	}
	void _forceDisabled(QAction *action, bool disabled) {
		if (action->isEnabled()) {
			if (disabled) action->setDisabled(true);
		} else if (!disabled) {
			action->setDisabled(false);
		}
	}
}

void PsMainWindow::psMacUndo() {
	_sendKeySequence(Qt::Key_Z, Qt::ControlModifier);
}

void PsMainWindow::psMacRedo() {
	_sendKeySequence(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
}

void PsMainWindow::psMacCut() {
	_sendKeySequence(Qt::Key_X, Qt::ControlModifier);
}

void PsMainWindow::psMacCopy() {
	_sendKeySequence(Qt::Key_C, Qt::ControlModifier);
}

void PsMainWindow::psMacPaste() {
	_sendKeySequence(Qt::Key_V, Qt::ControlModifier);
}

void PsMainWindow::psMacDelete() {
	_sendKeySequence(Qt::Key_Delete);
}

void PsMainWindow::psMacSelectAll() {
	_sendKeySequence(Qt::Key_A, Qt::ControlModifier);
}

bool PsMainWindow::psHandleTitle() {
    return false;
}

void PsMainWindow::psInitSysMenu() {
}

void PsMainWindow::psUpdateSysMenu(Qt::WindowState state) {
}

void PsMainWindow::psUpdateMargins() {
}

void PsMainWindow::psMacUpdateMenu() {
	if (!posInited) return;

	QWidget *focused = QApplication::focusWidget();
	bool isLogged = !!App::self(), canUndo = false, canRedo = false, canCut = false, canCopy = false, canPaste = false, canDelete = false, canSelectAll = false;
	if (QLineEdit *edit = qobject_cast<QLineEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->hasSelectedText();
		canSelectAll = !edit->text().isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = !App::app()->clipboard()->text().isEmpty();
	} else if (FlatTextarea *edit = qobject_cast<FlatTextarea*>(focused)) {
		canCut = canCopy = canDelete = edit->textCursor().hasSelection();
		canSelectAll = edit->hasText();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = !App::app()->clipboard()->text().isEmpty();
	} else if (HistoryList *list = qobject_cast<HistoryList*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}
	_forceDisabled(psLogout, !isLogged);
	_forceDisabled(psUndo, !canUndo);
	_forceDisabled(psRedo, !canRedo);
	_forceDisabled(psCut, !canCut);
	_forceDisabled(psCopy, !canCopy);
	_forceDisabled(psPaste, !canPaste);
	_forceDisabled(psDelete, !canDelete);
	_forceDisabled(psSelectAll, !canSelectAll);
	_forceDisabled(psContacts, !isLogged);
	_forceDisabled(psAddContact, !isLogged);
	_forceDisabled(psNewGroup, !isLogged);
	_forceDisabled(psShowTelegram, psIsActive());
}

void PsMainWindow::psFlash() {
    _private.startBounce();
}

PsMainWindow::~PsMainWindow() {
	finished = true;
}

void PsMainWindow::psClearNotifies(PeerId peerId) {
	_private.clearNotifies(peerId);
}

void PsMainWindow::psActivateNotify(NotifyWindow *w) {
	objc_activateWnd(w->winId());
}

namespace {
	QRect _monitorRect;
	uint64 _monitorLastGot = 0;
}

QRect psDesktopRect() {
	uint64 tnow = getms(true);
	if (tnow > _monitorLastGot + 1000 || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
	}
	return _monitorRect;
}

void psShowOverAll(QWidget *w, bool canFocus) {
	objc_showOverAll(w->winId(), canFocus);
}

void psBringToBack(QWidget *w) {
	objc_bringToBack(w->winId());
}

void PsMainWindow::psNotifyShown(NotifyWindow *w) {
	w->hide();
	objc_holdOnTop(w->winId());
	w->show();
	psShowOverAll(w, false);
}

void PsMainWindow::psPlatformNotify(HistoryItem *item) {
	QString title = (cNotifyView() <= dbinvShowName) ? item->history()->peer->name : lang(lng_notification_title);
	QString subtitle = (cNotifyView() <= dbinvShowName) ? item->notificationHeader() : QString();
	QString msg = (cNotifyView() <= dbinvShowPreview) ? item->notificationText() : lang(lng_notification_preview);
	_private.showNotify(item->history()->peer->id, title, subtitle, msg, (cNotifyView() <= dbinvShowPreview));
}

bool PsMainWindow::eventFilter(QObject *obj, QEvent *evt) {
	QEvent::Type t = evt->type();
	if (t == QEvent::FocusIn || t == QEvent::FocusOut) {
		if (qobject_cast<QLineEdit*>(obj) || qobject_cast<FlatTextarea*>(obj) || qobject_cast<HistoryList*>(obj)) {
			psMacUpdateMenu();
		}
	}
	return QMainWindow::eventFilter(obj, evt);
}

PsApplication::PsApplication(int &argc, char **argv) : QApplication(argc, argv) {
}

void PsApplication::psInstallEventFilter() {
    delete _psEventFilter;
	_psEventFilter = new _PsEventFilter();
    installNativeEventFilter(_psEventFilter);
}

PsApplication::~PsApplication() {
    delete _psEventFilter;
    _psEventFilter = 0;
}

PsUpdateDownloader::PsUpdateDownloader(QThread *thread, const MTPDhelp_appUpdate &update) : reply(0), already(0), full(0) {
	updateUrl = qs(update.vurl);
	moveToThread(thread);
	manager.moveToThread(thread);
	App::setProxySettings(manager);

	connect(thread, SIGNAL(started()), this, SLOT(start()));
	initOutput();
}

PsUpdateDownloader::PsUpdateDownloader(QThread *thread, const QString &url) : reply(0), already(0), full(0) {
	updateUrl = url;
	moveToThread(thread);
	manager.moveToThread(thread);
	App::setProxySettings(manager);

	connect(thread, SIGNAL(started()), this, SLOT(start()));
	initOutput();
}

void PsUpdateDownloader::initOutput() {
	QString fileName;
	QRegularExpressionMatch m = QRegularExpression(qsl("/([^/\\?]+)(\\?|$)")).match(updateUrl);
	if (m.hasMatch()) {
		fileName = m.captured(1).replace(QRegularExpression(qsl("[^a-zA-Z0-9_\\-]")), QString());
	}
	if (fileName.isEmpty()) {
		fileName = qsl("tupdate-%1").arg(rand());
	}
	QString dirStr = cWorkingDir() + qsl("tupdates/");
	fileName = dirStr + fileName;
	QFileInfo file(fileName);

	QDir dir(dirStr);
	if (dir.exists()) {
		QFileInfoList all = dir.entryInfoList(QDir::Files);
		for (QFileInfoList::iterator i = all.begin(), e = all.end(); i != e; ++i) {
			if (i->absoluteFilePath() != file.absoluteFilePath()) {
				QFile::remove(i->absoluteFilePath());
			}
		}
	} else {
		dir.mkdir(dir.absolutePath());
	}
	outputFile.setFileName(fileName);
	if (file.exists()) {
		uint64 fullSize = file.size();
		if (fullSize < INT_MAX) {
			int32 goodSize = (int32)fullSize;
			if (goodSize % UpdateChunk) {
				goodSize = goodSize - (goodSize % UpdateChunk);
				if (goodSize) {
					if (outputFile.open(QIODevice::ReadOnly)) {
						QByteArray goodData = outputFile.readAll().mid(0, goodSize);
						outputFile.close();
						if (outputFile.open(QIODevice::WriteOnly)) {
							outputFile.write(goodData);
							outputFile.close();
							
							QMutexLocker lock(&mutex);
							already = goodSize;
						}
					}
				}
			} else {
				QMutexLocker lock(&mutex);
				already = goodSize;
			}
		}
		if (!already) {
			QFile::remove(fileName);
		}
	}
}

void PsUpdateDownloader::start() {
	sendRequest();
}

void PsUpdateDownloader::sendRequest() {
	QNetworkRequest req(updateUrl);
	QByteArray rangeHeaderValue = "bytes=" + QByteArray::number(already) + "-";// + QByteArray::number(already + cUpdateChunk() - 1); 
	req.setRawHeader("Range", rangeHeaderValue);
	req.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);
	if (reply) reply->deleteLater();
	reply = manager.get(req);
	connect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(partFinished(qint64,qint64)));
	connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(partFailed(QNetworkReply::NetworkError)));
	connect(reply, SIGNAL(metaDataChanged()), this, SLOT(partMetaGot()));
}

void PsUpdateDownloader::partMetaGot() {
	typedef QList<QNetworkReply::RawHeaderPair> Pairs;
	Pairs pairs = reply->rawHeaderPairs();
	for (Pairs::iterator i = pairs.begin(), e = pairs.end(); i != e; ++i) {
		if (QString::fromUtf8(i->first).toLower() == "content-range") {
			QRegularExpressionMatch m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(i->second));
			if (m.hasMatch()) {
				{
					QMutexLocker lock(&mutex);
					full = m.captured(1).toInt();
				}
				emit App::app()->updateDownloading(already, full);
			}
		}
	}
}

int32 PsUpdateDownloader::ready() {
	QMutexLocker lock(&mutex);
	return already;
}

int32 PsUpdateDownloader::size() {
	QMutexLocker lock(&mutex);
	return full;
}

void PsUpdateDownloader::partFinished(qint64 got, qint64 total) {
	if (!reply) return;

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
	    int status = statusCode.toInt();
		if (status != 200 && status != 206 && status != 416) {
			LOG(("Update Error: Bad HTTP status received in partFinished(): %1").arg(status));
			return fatalFail();
		}
	}

	if (!already && !full) {
		QMutexLocker lock(&mutex);
		full = total;
	}
	DEBUG_LOG(("Update Info: part %1 of %2").arg(got).arg(total));

	if (!outputFile.isOpen()) {
		if (!outputFile.open(QIODevice::Append)) {
			LOG(("Update Error: Could not open output file '%1' for appending").arg(outputFile.fileName()));
			return fatalFail();
		}
	}
	QByteArray r = reply->readAll();
	if (!r.isEmpty()) {
		outputFile.write(r);

		QMutexLocker lock(&mutex);
		already += r.size();
	}
	if (got >= total) {
		reply->deleteLater();
		reply = 0;
		outputFile.close();
		unpackUpdate();
	} else {
		emit App::app()->updateDownloading(already, full);
	}
}

void PsUpdateDownloader::partFailed(QNetworkReply::NetworkError e) {
	if (!reply) return;

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	reply->deleteLater();
	reply = 0;
    if (statusCode.isValid()) {
	    int status = statusCode.toInt();
		if (status == 416) { // Requested range not satisfiable
			outputFile.close();
			unpackUpdate();
			return;
		}
	}
	LOG(("Update Error: failed to download part starting from %1, error %2").arg(already).arg(e));
	emit App::app()->updateFailed();
}

void PsUpdateDownloader::deleteDir(const QString &dir) {
	objc_deleteDir(dir);
}

void PsUpdateDownloader::fatalFail() {
	clearAll();
	emit App::app()->updateFailed();
}

void PsUpdateDownloader::clearAll() {
	deleteDir(cWorkingDir() + qsl("tupdates"));
}

#ifdef Q_OS_WIN
typedef DWORD VerInt;
typedef WCHAR VerChar;
#else
typedef int VerInt;
typedef wchar_t VerChar;
#endif

void PsUpdateDownloader::unpackUpdate() {
    QByteArray packed;
	if (!outputFile.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read updates file!"));
		return fatalFail();
	}
#ifdef Q_OS_WIN // use Lzma SDK for win
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header
#else
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = 0, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hOriginalSizeLen; // header
#endif
	QByteArray compressed = outputFile.readAll();
	int32 compressedLen = compressed.size() - hSize;
	if (compressedLen <= 0) {
		LOG(("Update Error: bad compressed size: %1").arg(compressed.size()));
		return fatalFail();
	}
	outputFile.close();

	QString tempDirPath = cWorkingDir() + qsl("tupdates/temp"), readyDirPath = cWorkingDir() + qsl("tupdates/ready");
	deleteDir(tempDirPath);
	deleteDir(readyDirPath);

	QDir tempDir(tempDirPath), readyDir(readyDirPath);
	if (tempDir.exists() || readyDir.exists()) {
		LOG(("Update Error: cant clear tupdates/temp or tupdates/ready dir!"));
		return fatalFail();
	}

	uchar sha1Buffer[20];
	bool goodSha1 = !memcmp(compressed.constData() + hSigLen, hashSha1(compressed.constData() + hSigLen + hShaLen, compressedLen + hPropsLen + hOriginalSizeLen, sha1Buffer), hShaLen);
	if (!goodSha1) {
		LOG(("Update Error: bad SHA1 hash of update file!"));
		return fatalFail();
	}

	RSA *pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(UpdatesPublicKey), -1), 0, 0, 0);
	if (!pbKey) {
		LOG(("Update Error: cant read public rsa key!"));
		return fatalFail();
	}
    if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
		RSA_free(pbKey);
		LOG(("Update Error: bad RSA signature of update file!"));
		return fatalFail();
    }
	RSA_free(pbKey);

	QByteArray uncompressed;

	int32 uncompressedLen;
	memcpy(&uncompressedLen, compressed.constData() + hSigLen + hShaLen + hPropsLen, hOriginalSizeLen);
	uncompressed.resize(uncompressedLen);

	size_t resultLen = uncompressed.size();
#ifdef Q_OS_WIN // use Lzma SDK for win
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)uncompressed.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData() + hSigLen + hShaLen), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		LOG(("Update Error: could not uncompress lzma, code: %1").arg(uncompressRes));
		return fatalFail();
	}
#else
	lzma_stream stream = LZMA_STREAM_INIT;

	lzma_ret ret = lzma_stream_decoder(&stream, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
			case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		LOG(("Error initializing the decoder: %1 (error code %2)").arg(msg).arg(ret));
		return fatalFail();
	}

	stream.avail_in = compressedLen;
	stream.next_in = (uint8_t*)(compressed.constData() + hSize);
	stream.avail_out = resultLen;
	stream.next_out = (uint8_t*)uncompressed.data();

	lzma_ret res = lzma_code(&stream, LZMA_FINISH);
	if (stream.avail_in) {
		LOG(("Error in decompression, %1 bytes left in _in of %2 whole.").arg(stream.avail_in).arg(compressedLen));
		return fatalFail();
	} else if (stream.avail_out) {
		LOG(("Error in decompression, %1 bytes free left in _out of %2 whole.").arg(stream.avail_out).arg(resultLen));
		return fatalFail();
	}
	lzma_end(&stream);
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_FORMAT_ERROR: msg = "The input data is not in the .xz format"; break;
			case LZMA_OPTIONS_ERROR: msg = "Unsupported compression options"; break;
			case LZMA_DATA_ERROR: msg = "Compressed file is corrupt"; break;
			case LZMA_BUF_ERROR: msg = "Compressed data is truncated or otherwise corrupt"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		LOG(("Error in decompression: %1 (error code %2)").arg(msg).arg(res));
		return fatalFail();
	}
#endif

	tempDir.mkdir(tempDir.absolutePath());

	quint32 version;
	{
		QBuffer buffer(&uncompressed);
		buffer.open(QIODevice::ReadOnly);
		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);

		stream >> version;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read version from downloaded stream, status: %1").arg(stream.status()));
			return fatalFail();
		}
		if (version <= AppVersion) {
			LOG(("Update Error: downloaded version %1 is not greater, than mine %2").arg(version).arg(AppVersion));
			return fatalFail();
		}

		quint32 filesCount;
		stream >> filesCount;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read files count from downloaded stream, status: %1").arg(stream.status()));
			return fatalFail();
		}
		if (!filesCount) {
			LOG(("Update Error: update is empty!"));
			return fatalFail();
		}
		for (uint32 i = 0; i < filesCount; ++i) {
			QString relativeName;
			quint32 fileSize;
			QByteArray fileInnerData;
			bool executable = false;

			stream >> relativeName >> fileSize >> fileInnerData;
#if defined Q_OS_MAC || defined Q_OS_LINUX
			stream >> executable;
#endif
			if (stream.status() != QDataStream::Ok) {
				LOG(("Update Error: cant read file from downloaded stream, status: %1").arg(stream.status()));
				return fatalFail();
			}
			if (fileSize != quint32(fileInnerData.size())) {
				LOG(("Update Error: bad file size %1 not matching data size %2").arg(fileSize).arg(fileInnerData.size()));
				return fatalFail();
			}

			QFile f(tempDirPath + '/' + relativeName);
			if (!QDir().mkpath(QFileInfo(f).absolutePath())) {
				LOG(("Update Error: cant mkpath for file '%1'").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			if (!f.open(QIODevice::WriteOnly)) {
				LOG(("Update Error: cant open file '%1' for writing").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			if (f.write(fileInnerData) != fileSize) {
				f.close();
				LOG(("Update Error: cant write file '%1'").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			f.close();
			if (executable) {
				QFileDevice::Permissions p = f.permissions();
				p |= QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther;
				f.setPermissions(p);
			}
		}

		// create tdata/version file
		tempDir.mkdir(QDir(tempDirPath + qsl("/tdata")).absolutePath());
		std::wstring versionString = ((version % 1000) ? QString("%1.%2.%3").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000)).arg(int(version % 1000)) : QString("%1.%2").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000))).toStdWString();

		VerInt versionNum = VerInt(version), versionLen = VerInt(versionString.size() * sizeof(VerChar));
		VerChar versionStr[32];
		memcpy(versionStr, versionString.c_str(), versionLen);

		QFile fVersion(tempDirPath + qsl("/tdata/version"));		
		if (!fVersion.open(QIODevice::WriteOnly)) {
			LOG(("Update Error: cant write version file '%1'").arg(tempDirPath + qsl("/version")));
			return fatalFail();
		}
		fVersion.write((const char*)&versionNum, sizeof(VerInt));
		fVersion.write((const char*)&versionLen, sizeof(VerInt));
		fVersion.write((const char*)&versionStr[0], versionLen);
		fVersion.close();
	}
	
	if (!tempDir.rename(tempDir.absolutePath(), readyDir.absolutePath())) {
		LOG(("Update Error: cant rename temp dir '%1' to ready dir '%2'").arg(tempDir.absolutePath()).arg(readyDir.absolutePath()));
		return fatalFail();
	}
	deleteDir(tempDirPath);
	outputFile.remove();

    emit App::app()->updateReady();
}

PsUpdateDownloader::~PsUpdateDownloader() {
	delete reply;
	reply = 0;
}

void psActivateProcess(uint64 pid) {
	objc_activateProgram();
}

QString psCurrentCountry() {
	QString country = objc_currentCountry();
	return country.isEmpty() ? QString::fromLatin1(DefaultCountry) : country;
}

QString psCurrentLanguage() {
	QString lng = objc_currentLang();
	return lng.isEmpty() ? QString::fromLatin1(DefaultLanguage) : lng;
}

QString psAppDataPath() {
	return objc_appDataPath();
}

QString psDownloadPath() {
	return objc_downloadPath();
}

QString psCurrentExeDirectory(int argc, char *argv[]) {
    QString first = argc ? QString::fromLocal8Bit(argv[0]) : QString();
    if (!first.isEmpty()) {
        QFileInfo info(first);
        if (info.exists()) {
            return QDir(info.absolutePath() + qsl("/../../..")).absolutePath() + '/';
        }
    }
	return QString();
}

QString psCurrentExeName(int argc, char *argv[]) {
	QString first = argc ? QString::fromLocal8Bit(argv[0]) : QString();
	if (!first.isEmpty()) {
		QFileInfo info(first);
		if (info.exists()) {
			return QDir(QDir(info.absolutePath() + qsl("/../..")).absolutePath()).dirName();
		}
	}
	return QString();
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
		psSendToMenu(false, true);
	} catch (...) {
	}
}

int psCleanup() {
	psDoCleanup();
	return 0;
}

void psDoFixPrevious() {
}

int psFixPrevious() {
	psDoFixPrevious();
	return 0;
}

bool psCheckReadyUpdate() {
    QString readyPath = cWorkingDir() + qsl("tupdates/ready");
	if (!QDir(readyPath).exists()) {
		return false;
	}

	// check ready version
	QString versionPath = readyPath + qsl("/tdata/version");
	{
		QFile fVersion(versionPath);
		if (!fVersion.open(QIODevice::ReadOnly)) {
			LOG(("Update Error: cant read version file '%1'").arg(versionPath));
			PsUpdateDownloader::clearAll();
			return false;
		}
		VerInt versionNum;
		if (fVersion.read((char*)&versionNum, sizeof(VerInt)) != sizeof(VerInt)) {
			LOG(("Update Error: cant read version from file '%1'").arg(versionPath));
			PsUpdateDownloader::clearAll();
			return false;
		}
		fVersion.close();
		if (versionNum <= AppVersion) {
			LOG(("Update Error: cant install version %1 having version %2").arg(versionNum).arg(AppVersion));
			PsUpdateDownloader::clearAll();
			return false;
		}
	}

#ifdef Q_OS_WIN
	QString curUpdater = (cExeDir() + qsl("Updater.exe"));
	QFileInfo updater(cWorkingDir() + qsl("tupdates/ready/Updater.exe"));
#elif defined Q_OS_MAC
	QString curUpdater = (cExeDir() + cExeName() + qsl("/Contents/Frameworks/Updater"));
	QFileInfo updater(cWorkingDir() + qsl("tupdates/ready/Telegram.app/Contents/Frameworks/Updater"));
#endif
	if (!updater.exists()) {
		QFileInfo current(curUpdater);
		if (!current.exists()) {
			PsUpdateDownloader::clearAll();
			return false;
		}
		if (!QFile(current.absoluteFilePath()).copy(updater.absoluteFilePath())) {
			PsUpdateDownloader::clearAll();
			return false;
		}
	}
#ifdef Q_OS_WIN
	if (CopyFile(updater.absoluteFilePath().toStdWString().c_str(), curUpdater.toStdWString().c_str(), FALSE) == FALSE) {
		PsUpdateDownloader::clearAll();
		return false;
	}
	if (DeleteFile(updater.absoluteFilePath().toStdWString().c_str()) == FALSE) {
		PsUpdateDownloader::clearAll();
		return false;
    }
#elif defined Q_OS_MAC
	QDir().mkpath(QFileInfo(curUpdater).absolutePath());
	DEBUG_LOG(("Update Info: moving %1 to %2..").arg(updater.absoluteFilePath()).arg(curUpdater));
	if (!objc_moveFile(updater.absoluteFilePath(), curUpdater)) {
		PsUpdateDownloader::clearAll();
		return false;
	}
#endif
    return true;
}

void psPostprocessFile(const QString &name) {
}

void psOpenFile(const QString &name, bool openWith) {
    objc_openFile(name, openWith);
}

void psShowInFolder(const QString &name) {
    objc_showInFinder(name, QFileInfo(name).absolutePath());
}

void psStart() {
	objc_start();
}

void psFinish() {
    objc_finish();
}

void psExecUpdater() {
	if (!objc_execUpdater()) {
		QString readyPath = cWorkingDir() + qsl("tupdates/ready");
		PsUpdateDownloader::deleteDir(readyPath);
	}
}

void psExecTelegram() {
	objc_execTelegram();
}

void psAutoStart(bool start, bool silent) {
}

void psSendToMenu(bool send, bool silent) {
}

void psUpdateOverlayed(QWidget *widget) {
}

QString psConvertFileUrl(const QString &url) {
	return objc_convertFileUrl(url);
}

QString strNotificationAboutThemeChange() {
	const uint32 letters[] = { 0xE9005541, 0x5600DC70, 0x88001570, 0xF500D86C, 0x8100E165, 0xEE005949, 0x2900526E, 0xAE00FB74, 0x96000865, 0x7000CD72, 0x3B001566, 0x5F007361, 0xAE00B663, 0x74009A65, 0x29003054, 0xC6002668, 0x98003865, 0xFA00336D, 0xA3007A65, 0x93001443, 0xBB007868, 0xE100E561, 0x3500366E, 0xC0007A67, 0x200CA65, 0xBE00DF64, 0xE300BB4E, 0x2900D26F, 0xD500D374, 0xE900E269, 0x86008F66, 0xC4006669, 0x1C00A863, 0xE600A761, 0x8E00EE74, 0xB300B169, 0xCF00B36F, 0xE600D36E };
	return strMakeFromLetters(letters, sizeof(letters) / sizeof(letters[0]));
}

QString strStyleOfInterface() {
	const uint32 letters[] = { 0xEF004041, 0x4C007F70, 0x1F007A70, 0x9E00A76C, 0x8500D165, 0x2E003749, 0x7B00526E, 0x3400E774, 0x3C00FA65, 0x6200B172, 0xF7001D66, 0xB002961, 0x71008C63, 0x86005465, 0xA3006F53, 0x11006174, 0xCD001779, 0x8200556C, 0x6C009B65 };
	return strMakeFromLetters(letters, sizeof(letters) / sizeof(letters[0]));
}

QString strNeedToReload() {
	const uint32 letters[] = { 0x82007746, 0xBB00C649, 0x7E00235F, 0x9A00FE54, 0x4C004542, 0x91001772, 0x8A00D76F, 0xC700B977, 0x7F005F73, 0x34003665, 0x2300D572, 0x72002E54, 0x18001461, 0x14004A62, 0x5100CC6C, 0x83002365, 0x5A002C56, 0xA5004369, 0x26004265, 0xD006577 };
	return strMakeFromLetters(letters, sizeof(letters) / sizeof(letters[0]));
}

QString strNeedToRefresh1() {
	const uint32 letters[] = { 0xEF006746, 0xF500CE49, 0x1500715F, 0x95001254, 0x3A00CB4C, 0x17009469, 0xB400DA73, 0xDE00C574, 0x9200EC56, 0x3C00A669, 0xFD00D865, 0x59000977 };
	return strMakeFromLetters(letters, sizeof(letters) / sizeof(letters[0]));
}

QString strNeedToRefresh2() {
	const uint32 letters[] = { 0x8F001546, 0xAF007A49, 0xB8002B5F, 0x1A000B54, 0xD003E49, 0xE0003663, 0x4900796F, 0x500836E, 0x9A00D156, 0x5E00FF69, 0x5900C765, 0x3D00D177 };
	return strMakeFromLetters(letters, sizeof(letters) / sizeof(letters[0]));
}
