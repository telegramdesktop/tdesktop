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

#include <windows.h>

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
	HWND psHwnd() const;
	HMENU psMenu() const;

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

	bool psHasNativeNotifications();
	void psCleanNotifyPhotosIn(int32 dt);

	virtual QImage iconWithCounter(int size, int count, style::color bg, bool smallIcon) = 0;

	static UINT TaskbarCreatedMsgId() {
		return _taskbarCreatedMsgId;
	}
	static void TaskbarCreated();

	// Custom shadows.
	enum class ShadowsChange {
		Moved    = 0x01,
		Resized  = 0x02,
		Shown    = 0x04,
		Hidden   = 0x08,
		Activate = 0x10,
	};
	Q_DECLARE_FLAGS(ShadowsChanges, ShadowsChange);

	bool shadowsWorking() const {
		return _shadowsWorking;
	}
	void shadowsActivate();
	void shadowsDeactivate();
	void shadowsUpdate(ShadowsChanges changes, WINDOWPOS *position = nullptr);

	int deltaLeft() const {
		return _deltaLeft;
	}
	int deltaTop() const {
		return _deltaTop;
	}

	~MainWindow();

public slots:

	void psUpdateDelegate();
	void psSavePosition(Qt::WindowState state = Qt::WindowActive);
	void psShowTrayMenu();

	void psCleanNotifyPhotos();

protected:

	bool psHasTrayIcon() const {
		return trayIcon;
	}

	bool posInited = false;
	QSystemTrayIcon *trayIcon = nullptr;
	PopupMenu *trayIconMenu = nullptr;
	QImage icon256, iconbig256;
	QIcon wndIcon;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();

	QTimer psUpdatedPositionTimer;

private:
	void psDestroyIcons();

	static UINT _taskbarCreatedMsgId;

	bool _shadowsWorking = false;
	bool _themeInited = false;

	HWND ps_hWnd = nullptr;
	HWND ps_tbHider_hWnd = nullptr;
	HMENU ps_menu = nullptr;
	HICON ps_iconBig = nullptr;
	HICON ps_iconSmall = nullptr;
	HICON ps_iconOverlay = nullptr;

	SingleTimer ps_cleanNotifyPhotosTimer;

	int _deltaLeft = 0;
	int _deltaTop = 0;

};

Q_DECLARE_OPERATORS_FOR_FLAGS(MainWindow::ShadowsChanges);

} // namespace Platform
