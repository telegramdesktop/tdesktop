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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "platform/mac/main_window_mac.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "application.h"
#include "playerwidget.h"
#include "historywidget.h"
#include "localstorage.h"

#include "lang.h"

#include <Cocoa/Cocoa.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CFURL.h>

#include <IOKit/hidsystem/ev_keymap.h>

namespace Platform {

void MacPrivate::activeSpaceChanged() {
	if (App::wnd()) {
		App::wnd()->notifyActivateAll();
	}
}

void MacPrivate::darkModeChanged() {
	Notify::unreadCounterUpdated();
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

	MainWidget::MessageToSend message;
	message.history = history;
	message.textWithTags = { QString::fromUtf8(str), TextWithTags::Tags() };
	message.replyTo = (msgid > 0 && !history->peer->isUser()) ? msgid : 0;
	message.silent = false;
	message.clearDraft = false;
	App::main()->sendMessage(message);
}

MainWindow::MainWindow()
: posInited(false)
, icon256(qsl(":/gui/art/icon256.png"))
, iconbig256(qsl(":/gui/art/iconbig256.png"))
, wndIcon(QPixmap::fromImage(iconbig256, Qt::ColorOnly)) {
	QImage tray(qsl(":/gui/art/osxtray.png"));
	trayImg = tray.copy(0, cRetina() ? 0 : tray.width() / 2, tray.width() / (cRetina() ? 2 : 4), tray.width() / (cRetina() ? 2 : 4));
	trayImgSel = tray.copy(tray.width() / (cRetina() ? 2 : 4), cRetina() ? 0 : tray.width() / 2, tray.width() / (cRetina() ? 2 : 4), tray.width() / (cRetina() ? 2 : 4));

	_hideAfterFullScreenTimer.setSingleShot(true);
	connect(&_hideAfterFullScreenTimer, SIGNAL(timeout()), this, SLOT(onHideAfterFullScreen()));
}

void MainWindow::closeWithoutDestroy() {
	NSWindow *nsWindow = [reinterpret_cast<NSView*>(winId()) window];
	bool isFullScreen = (([nsWindow styleMask] & NSFullScreenWindowMask) == NSFullScreenWindowMask);
	if (isFullScreen) {
		_hideAfterFullScreenTimer.start(3000);
		[nsWindow toggleFullScreen:nsWindow];
	} else {
		hide();
	}
}

void MainWindow::stateChangedHook(Qt::WindowState state) {
	if (_hideAfterFullScreenTimer.isActive()) {
		_hideAfterFullScreenTimer.stop();
		QTimer::singleShot(0, this, SLOT(onHideAfterFullScreen()));
	}
}

void MainWindow::onHideAfterFullScreen() {
	hide();
}

QImage MainWindow::psTrayIcon(bool selected) const {
	return selected ? trayImgSel : trayImg;
}

void MainWindow::psShowTrayMenu() {
}

void MainWindow::psRefreshTaskbarIcon() {
}

void MainWindow::psTrayMenuUpdated() {
}

void MainWindow::psSetupTrayIcon() {
	if (!trayIcon) {
		trayIcon = new QSystemTrayIcon(this);

		QIcon icon(QPixmap::fromImage(psTrayIcon(), Qt::ColorOnly));
		icon.addPixmap(QPixmap::fromImage(psTrayIcon(true), Qt::ColorOnly), QIcon::Selected);

		trayIcon->setIcon(icon);
		trayIcon->setToolTip(str_const_toString(AppName));
		connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggleTray(QSystemTrayIcon::ActivationReason)), Qt::UniqueConnection);
		App::wnd()->updateTrayMenu();
	}
	psUpdateCounter();

	trayIcon->show();
}

void MainWindow::psUpdateWorkmode() {
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
	style::font f(fontSize, 0, 0);
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

void MainWindow::psUpdateCounter() {
	int32 counter = App::histories().unreadBadge();

	setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));
	setWindowIcon(wndIcon);

	QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
	_private.setWindowBadge(counter ? cnt : QString());

	if (trayIcon) {
		bool muted = App::histories().unreadOnlyMuted();
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

void MainWindow::psUpdateDelegate() {
	_private.updateDelegate();
}

void MainWindow::psInitSize() {
	setMinimumWidth(st::wndMinWidth);
	setMinimumHeight(st::wndMinHeight);

	TWindowPos pos(cWindowPos());
	QRect avail(QDesktopWidget().availableGeometry());
	bool maximized = false;
	QRect geom(avail.x() + (avail.width() - st::wndDefWidth) / 2, avail.y() + (avail.height() - st::wndDefHeight) / 2, st::wndDefWidth, st::wndDefHeight);
	if (pos.w && pos.h) {
		QList<QScreen*> screens = Application::screens();
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

void MainWindow::psInitFrameless() {
	psUpdatedPositionTimer.setSingleShot(true);
	connect(&psUpdatedPositionTimer, SIGNAL(timeout()), this, SLOT(psSavePosition()));
}

void MainWindow::psSavePosition(Qt::WindowState state) {
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
	QList<QScreen*> screens = Application::screens();
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

void MainWindow::psUpdatedPosition() {
	psUpdatedPositionTimer.start(SaveWindowPositionTimeout);
}

void MainWindow::psFirstShow() {
	psUpdateMargins();

	bool showShadows = true;

	show();
	_private.enableShadow(winId());
	if (cWindowPos().maximized) {
		setWindowState(Qt::WindowMaximized);
	}

	if ((cLaunchMode() == LaunchModeAutoStart && cStartMinimized()) || cStartInTray()) {
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

void MainWindow::psMacUndo() {
	_sendKeySequence(Qt::Key_Z, Qt::ControlModifier);
}

void MainWindow::psMacRedo() {
	_sendKeySequence(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psMacCut() {
	_sendKeySequence(Qt::Key_X, Qt::ControlModifier);
}

void MainWindow::psMacCopy() {
	_sendKeySequence(Qt::Key_C, Qt::ControlModifier);
}

void MainWindow::psMacPaste() {
	_sendKeySequence(Qt::Key_V, Qt::ControlModifier);
}

void MainWindow::psMacDelete() {
	_sendKeySequence(Qt::Key_Delete);
}

void MainWindow::psMacSelectAll() {
	_sendKeySequence(Qt::Key_A, Qt::ControlModifier);
}

bool MainWindow::psHandleTitle() {
	return false;
}

void MainWindow::psInitSysMenu() {
}

void MainWindow::psUpdateSysMenu(Qt::WindowState state) {
}

void MainWindow::psUpdateMargins() {
}

void MainWindow::psMacUpdateMenu() {
	if (!posInited) return;

	QWidget *focused = QApplication::focusWidget();
	bool isLogged = !!App::self(), canUndo = false, canRedo = false, canCut = false, canCopy = false, canPaste = false, canDelete = false, canSelectAll = false;
	if (QLineEdit *edit = qobject_cast<QLineEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->hasSelectedText();
		canSelectAll = !edit->text().isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = !Application::clipboard()->text().isEmpty();
	} else if (FlatTextarea *edit = qobject_cast<FlatTextarea*>(focused)) {
		canCut = canCopy = canDelete = edit->textCursor().hasSelection();
		canSelectAll = !edit->isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = !Application::clipboard()->text().isEmpty();
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

void MainWindow::psFlash() {
	_private.startBounce();
}

void MainWindow::psClearNotifies(PeerId peerId) {
	_private.clearNotifies(peerId);
}

void MainWindow::psActivateNotify(NotifyWindow *w) {
	objc_activateWnd(w->winId());
}

bool MainWindow::psFilterNativeEvent(void *event) {
	return _private.filterNativeEvent(event);
}

void MainWindow::psNotifyShown(NotifyWindow *w) {
	w->hide();
	objc_holdOnTop(w->winId());
	w->show();
	psShowOverAll(w, false);
}

void MainWindow::psPlatformNotify(HistoryItem *item, int32 fwdCount) {
	QString title = (!App::passcoded() && cNotifyView() <= dbinvShowName && !Global::ScreenIsLocked()) ? item->history()->peer->name : qsl("Telegram Desktop");
	QString subtitle = (!App::passcoded() && cNotifyView() <= dbinvShowName && !Global::ScreenIsLocked()) ? item->notificationHeader() : QString();
	QPixmap pix = (!App::passcoded() && cNotifyView() <= dbinvShowName && !Global::ScreenIsLocked()) ? item->history()->peer->genUserpic(st::notifyMacPhotoSize) : QPixmap();
	QString msg = (!App::passcoded() && cNotifyView() <= dbinvShowPreview && !Global::ScreenIsLocked()) ? (fwdCount < 2 ? item->notificationText() : lng_forward_messages(lt_count, fwdCount)) : lang(lng_notification_preview);

	bool withReply = !App::passcoded() && (cNotifyView() <= dbinvShowPreview && !Global::ScreenIsLocked()) && item->history()->peer->canWrite();

	_private.showNotify(item->history()->peer->id, item->id, pix, title, subtitle, msg, withReply);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *evt) {
	QEvent::Type t = evt->type();
	if (t == QEvent::FocusIn || t == QEvent::FocusOut) {
		if (qobject_cast<QLineEdit*>(obj) || qobject_cast<FlatTextarea*>(obj) || qobject_cast<HistoryInner*>(obj)) {
			psMacUpdateMenu();
		}
	}
	return Window::MainWindow::eventFilter(obj, evt);
}

MainWindow::~MainWindow() {
}

} // namespace
