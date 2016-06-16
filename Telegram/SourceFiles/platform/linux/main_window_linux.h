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

class NotifyWindow;

namespace Platform {

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
	void psNotifySettingGot();

	void psUpdateWorkmode();

	void psRefreshTaskbarIcon();

	bool psPosInited() const {
		return posInited;
	}

	void psActivateNotify(NotifyWindow *w);
	void psClearNotifies(PeerId peerId = 0);
	void psNotifyShown(NotifyWindow *w);
	void psPlatformNotify(HistoryItem *item, int32 fwdCount);

	void psUpdateCounter();

	bool psHasNativeNotifications() {
		return false;
	}

	virtual QImage iconWithCounter(int size, int count, style::color bg, bool smallIcon) = 0;

	static void LibsLoaded();

	~MainWindow();

public slots:

	void psUpdateDelegate();
	void psSavePosition(Qt::WindowState state = Qt::WindowActive);
	void psShowTrayMenu();

	void psStatusIconCheck();
	void psUpdateIndicator();

protected:

	bool psHasTrayIcon() const;

	bool posInited = false;
	QSystemTrayIcon *trayIcon = nullptr;
	QMenu *trayIconMenu = nullptr;
	QImage icon256, iconbig256;
	QIcon wndIcon;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();

	QTimer psUpdatedPositionTimer;

private:
	void psCreateTrayIcon();

	QTimer _psCheckStatusIconTimer;
	int _psCheckStatusIconLeft = 100;

	QTimer _psUpdateIndicatorTimer;
	uint64 _psLastIndicatorUpdate = 0;
};

} // namespace Platform
