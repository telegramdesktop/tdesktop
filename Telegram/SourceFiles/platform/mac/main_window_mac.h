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

#include "window/main_window.h"
#include "pspecific_mac_p.h"

class NotifyWindow;

namespace Platform {

class MacPrivate : public PsMacWindowPrivate {
public:

	void activeSpaceChanged();
	void darkModeChanged();
	void notifyClicked(unsigned long long peer, int msgid);
	void notifyReplied(unsigned long long peer, int msgid, const char *str);

};

class MainWindow : public Window::MainWindow {
	Q_OBJECT

public:
	MainWindow();

	int32 psResizeRowWidth() const {
		return 0;//st::wndResizeAreaWidth;
	}

	void psInitFrameless();
	void psInitSize();

	void psFirstShow();
	void psInitSysMenu();
	void psUpdateSysMenu(Qt::WindowState state);
	void psUpdateMargins();
	void psUpdatedPosition();

	bool psHandleTitle();

	void psFlash();

	void psUpdateWorkmode();

	void psRefreshTaskbarIcon();

	bool psPosInited() const {
		return posInited;
	}

	bool psFilterNativeEvent(void *event);

	void psActivateNotify(NotifyWindow *w);
	void psClearNotifies(PeerId peerId = 0);
	void psNotifyShown(NotifyWindow *w);
	void psPlatformNotify(HistoryItem *item, int32 fwdCount);

	bool eventFilter(QObject *obj, QEvent *evt) override;

	void psUpdateCounter();

	bool psHasNativeNotifications() {
		return !(QSysInfo::macVersion() < QSysInfo::MV_10_8);
	}

	virtual QImage iconWithCounter(int size, int count, style::color bg, bool smallIcon) = 0;

	void closeWithoutDestroy() override;

	~MainWindow();

public slots:
	void psUpdateDelegate();
	void psSavePosition(Qt::WindowState state = Qt::WindowActive);
	void psShowTrayMenu();

	void psMacUndo();
	void psMacRedo();
	void psMacCut();
	void psMacCopy();
	void psMacPaste();
	void psMacDelete();
	void psMacSelectAll();

private slots:
	void onHideAfterFullScreen();

protected:
	void stateChangedHook(Qt::WindowState state) override;

	QImage psTrayIcon(bool selected = false) const;
	bool psHasTrayIcon() const {
		return trayIcon;
	}

	void psMacUpdateMenu();

	bool posInited;
	QSystemTrayIcon *trayIcon = nullptr;
	QMenu *trayIconMenu = nullptr;
	QImage icon256, iconbig256;
	QIcon wndIcon;

	QImage trayImg, trayImgSel;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();
	virtual void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) = 0;

	QTimer psUpdatedPositionTimer;

private:
	MacPrivate _private;

	mutable bool psIdle;
	mutable QTimer psIdleTimer;

	QTimer _hideAfterFullScreenTimer;

	QMenuBar psMainMenu;
	QAction *psLogout = nullptr;
	QAction *psUndo = nullptr;
	QAction *psRedo = nullptr;
	QAction *psCut = nullptr;
	QAction *psCopy = nullptr;
	QAction *psPaste = nullptr;
	QAction *psDelete = nullptr;
	QAction *psSelectAll = nullptr;
	QAction *psContacts = nullptr;
	QAction *psAddContact = nullptr;
	QAction *psNewGroup = nullptr;
	QAction *psNewChannel = nullptr;
	QAction *psShowTelegram = nullptr;

};

} // namespace Platform
