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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "pspecific.h"

#include "lang.h"
#include "application.h"
#include "mainwidget.h"
#include "historywidget.h"

#include "localstorage.h"
#include "passcodewidget.h"

namespace {
    QStringList _initLogs;

    bool frameless = true;
	bool finished = true;

    class _PsEventFilter : public QAbstractNativeEventFilter {
	public:
		_PsEventFilter() {
		}

		bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) {
			Window *wnd = Application::wnd();
			if (!wnd) return false;

			return wnd->psFilterNativeEvent(message);
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
		App::wnd()->updateCounter();
	}
}

void MacPrivate::notifyClicked(unsigned long long peer, int msgid) {
    History *history = App::history(PeerId(peer));

    App::wnd()->showFromTray();
	if (App::passcoded()) {
		App::wnd()->setInnerFocus();
		App::wnd()->notifyClear();
	} else {
		App::wnd()->hideSettings();
		bool tomsg = !history->peer->isUser() && (msgid > 0);
		if (tomsg) {
			HistoryItem *item = App::histItemById(peerToChannel(PeerId(peer)), MsgId(msgid));
			if (!item || !item->mentionsMe()) {
				tomsg = false;
			}
		}
		Ui::showPeerHistory(history, tomsg ? msgid : ShowAtUnreadMsgId);
		App::wnd()->notifyClear(history);
	}
}

void MacPrivate::notifyReplied(unsigned long long peer, int msgid, const char *str) {
    History *history = App::history(PeerId(peer));

	App::main()->sendMessage(history, QString::fromUtf8(str), (msgid > 0 && !history->peer->isUser()) ? msgid : 0, false);
}

PsMainWindow::PsMainWindow(QWidget *parent) : QMainWindow(parent),
posInited(false), trayIcon(0), trayIconMenu(0), icon256(qsl(":/gui/art/icon256.png")), iconbig256(qsl(":/gui/art/iconbig256.png")), wndIcon(QPixmap::fromImage(iconbig256, Qt::ColorOnly)),
psLogout(0), psUndo(0), psRedo(0), psCut(0), psCopy(0), psPaste(0), psDelete(0), psSelectAll(0), psContacts(0), psAddContact(0), psNewGroup(0), psNewChannel(0), psShowTelegram(0) {
	QImage tray(qsl(":/gui/art/osxtray.png"));
	trayImg = tray.copy(0, cRetina() ? 0 : tray.width() / 2, tray.width() / (cRetina() ? 2 : 4), tray.width() / (cRetina() ? 2 : 4));
	trayImgSel = tray.copy(tray.width() / (cRetina() ? 2 : 4), cRetina() ? 0 : tray.width() / 2, tray.width() / (cRetina() ? 2 : 4), tray.width() / (cRetina() ? 2 : 4));
}

QImage PsMainWindow::psTrayIcon(bool selected) const {
	return selected ? trayImgSel : trayImg;
}

void PsMainWindow::psShowTrayMenu() {
}

void PsMainWindow::psRefreshTaskbarIcon() {
}

void PsMainWindow::psTrayMenuUpdated() {
}

void PsMainWindow::psSetupTrayIcon() {
    if (!trayIcon) {
        trayIcon = new QSystemTrayIcon(this);

        QIcon icon(QPixmap::fromImage(psTrayIcon(), Qt::ColorOnly));
        icon.addPixmap(QPixmap::fromImage(psTrayIcon(true), Qt::ColorOnly), QIcon::Selected);

        trayIcon->setIcon(icon);
        trayIcon->setToolTip(QString::fromStdWString(AppName));
        connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggleTray(QSystemTrayIcon::ActivationReason)), Qt::UniqueConnection);
        App::wnd()->updateTrayMenu();
    }
    psUpdateCounter();

    trayIcon->show();
}

void PsMainWindow::psUpdateWorkmode() {
    psSetupTrayIcon();
	if (cWorkMode() == dbiwmWindowOnly) {
		if (trayIcon) {
			trayIcon->setContextMenu(0);
			delete trayIcon;
		}
		trayIcon = 0;
	}
	psUpdateDelegate();
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
	int32 w = f->width(cnt), d, r;
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
	int32 counter = App::histories().unreadFull - (cIncludeMuted() ? 0 : App::histories().unreadMuted);

    setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));
	setWindowIcon(wndIcon);

    QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
    _private.setWindowBadge(counter ? cnt : QString());

	if (trayIcon) {
		bool muted = cIncludeMuted() ? (App::histories().unreadMuted >= counter) : false;
		bool dm = objc_darkMode();

		style::color bg = muted ? st::counterMuteBG : st::counterBG;
		QIcon icon;
		QImage img(psTrayIcon(dm)), imgsel(psTrayIcon(true));
		img.detach();
		imgsel.detach();
		int32 size = cRetina() ? 44 : 22;
		_placeCounter(img, size, counter, bg, (dm && muted) ? st::counterMacInvColor : st::counterColor);
		_placeCounter(imgsel, size, counter, st::white, st::counterMacInvColor);
		icon.addPixmap(QPixmap::fromImage(img, Qt::ColorOnly));
		icon.addPixmap(QPixmap::fromImage(imgsel, Qt::ColorOnly), QIcon::Selected);
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
			Local::writeSettings();
		}
    }
}

void PsMainWindow::psUpdatedPosition() {
    psUpdatedPositionTimer.start(SaveWindowPositionTimeout);
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

	if ((cFromAutoStart() && cStartMinimized()) || cStartInTray()) {
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

	posInited = true;

	// init global menu
	QMenu *main = psMainMenu.addMenu(qsl("Telegram"));
	main->addAction(lng_mac_menu_about_telegram(lt_telegram, qsl("Telegram")), App::wnd()->getTitle(), SLOT(onAbout()))->setMenuRole(QAction::AboutQtRole);
	main->addSeparator();
	QAction *prefs = main->addAction(lang(lng_mac_menu_preferences), App::wnd(), SLOT(showSettings()), QKeySequence(Qt::ControlModifier | Qt::Key_Comma));
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
	psNewChannel = window->addAction(lang(lng_mac_menu_new_channel), App::wnd(), SLOT(onShowNewChannel()));
	window->addSeparator();
	psShowTelegram = window->addAction(lang(lng_mac_menu_show), App::wnd(), SLOT(showFromTray()));

	psMacUpdateMenu();
}

namespace {
	void _sendKeySequence(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
		QWidget *focused = QApplication::focusWidget();
		if (qobject_cast<QLineEdit*>(focused) || qobject_cast<FlatTextarea*>(focused) || qobject_cast<HistoryInner*>(focused)) {
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
		canSelectAll = !edit->getLastText().isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = !App::app()->clipboard()->text().isEmpty();
	} else if (HistoryInner *list = qobject_cast<HistoryInner*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}
	_forceDisabled(psLogout, !isLogged && !App::passcoded());
	_forceDisabled(psUndo, !canUndo);
	_forceDisabled(psRedo, !canRedo);
	_forceDisabled(psCut, !canCut);
	_forceDisabled(psCopy, !canCopy);
	_forceDisabled(psPaste, !canPaste);
	_forceDisabled(psDelete, !canDelete);
	_forceDisabled(psSelectAll, !canSelectAll);
	_forceDisabled(psContacts, !isLogged || App::passcoded());
	_forceDisabled(psAddContact, !isLogged || App::passcoded());
	_forceDisabled(psNewGroup, !isLogged || App::passcoded());
	_forceDisabled(psNewChannel, !isLogged || App::passcoded());
	_forceDisabled(psShowTelegram, App::wnd()->isActive(false));
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

bool PsMainWindow::psFilterNativeEvent(void *event) {
	return _private.filterNativeEvent(event);
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

void PsMainWindow::psPlatformNotify(HistoryItem *item, int32 fwdCount) {
	QString title = (!App::passcoded() && cNotifyView() <= dbinvShowName) ? item->history()->peer->name : qsl("Telegram Desktop");
	QString subtitle = (!App::passcoded() && cNotifyView() <= dbinvShowName) ? item->notificationHeader() : QString();
	QPixmap pix = (!App::passcoded() && cNotifyView() <= dbinvShowName) ? item->history()->peer->photo->pix(st::notifyMacPhotoSize) : QPixmap();
	QString msg = (!App::passcoded() && cNotifyView() <= dbinvShowPreview) ? (fwdCount < 2 ? item->notificationText() : lng_forward_messages(lt_count, fwdCount)) : lang(lng_notification_preview);

	_private.showNotify(item->history()->peer->id, item->id, pix, title, subtitle, msg, !App::passcoded() && (cNotifyView() <= dbinvShowPreview));
}

bool PsMainWindow::eventFilter(QObject *obj, QEvent *evt) {
	QEvent::Type t = evt->type();
	if (t == QEvent::FocusIn || t == QEvent::FocusOut) {
		if (qobject_cast<QLineEdit*>(obj) || qobject_cast<FlatTextarea*>(obj) || qobject_cast<HistoryInner*>(obj)) {
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

void psDeleteDir(const QString &dir) {
	objc_deleteDir(dir);
}

namespace {
	uint64 _lastUserAction = 0;
}

void psUserActionDone() {
	_lastUserAction = getms(true);
}

bool psIdleSupported() {
	return objc_idleSupported();
}

uint64 psIdleTime() {
	int64 idleTime = 0;
	return objc_idleTime(idleTime) ? idleTime : (getms(true) - _lastUserAction);
}

bool psSkipAudioNotify() {
	return false;
}

bool psSkipDesktopNotify() {
	return false;
}

QStringList psInitLogs() {
    return _initLogs;
}

void psClearInitLogs() {
    _initLogs = QStringList();
}

void psActivateProcess(uint64 pid) {
	if (!pid) {
		objc_activateProgram(App::wnd() ? App::wnd()->winId() : 0);
	}
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
    QString first = argc ? fromUtf8Safe(argv[0]) : QString();
    if (!first.isEmpty()) {
        QFileInfo info(first);
        if (info.exists()) {
            return QDir(info.absolutePath() + qsl("/../../..")).absolutePath() + '/';
        }
    }
	return QString();
}

QString psCurrentExeName(int argc, char *argv[]) {
	QString first = argc ? fromUtf8Safe(argv[0]) : QString();
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

bool psShowOpenWithMenu(int x, int y, const QString &file) {
	return objc_showOpenWithMenu(x, y, file);
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

void psNewVersion() {
	objc_registerCustomScheme();
}

void psExecUpdater() {
	if (!objc_execUpdater()) {
		psDeleteDir(cWorkingDir() + qsl("tupdates/temp"));
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

void psDownloadPathEnableAccess() {
	objc_downloadPathEnableAccess(cDownloadPathBookmark());
}

QByteArray psDownloadPathBookmark(const QString &path) {
	return objc_downloadPathBookmark(path);
}

QByteArray psPathBookmark(const QString &path) {
	return objc_pathBookmark(path);
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
