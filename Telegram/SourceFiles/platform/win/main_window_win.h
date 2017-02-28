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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "platform/platform_main_window.h"
#include <windows.h>

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Platform {

class MainWindow : public Window::MainWindow {
	Q_OBJECT

public:
	MainWindow();

	HWND psHwnd() const;
	HMENU psMenu() const;

	void psFirstShow();
	void psInitSysMenu();
	void psUpdateSysMenu(Qt::WindowState state);
	void psUpdateMargins();

	void psFlash();
	void psNotifySettingGot();

	void psUpdateWorkmode();

	void psRefreshTaskbarIcon();

	bool psHasNativeNotifications();

	virtual QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) = 0;

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
	void psShowTrayMenu();

protected:
	void initHook() override;
	int32 screenNameChecksum(const QString &name) const override;
	void unreadCounterChangedHook() override;

	bool hasTrayIcon() const override {
		return trayIcon;
	}

	QSystemTrayIcon *trayIcon = nullptr;
	Ui::PopupMenu *trayIconMenu = nullptr;
	QImage icon256, iconbig256;
	QIcon wndIcon;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();
	virtual void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) = 0;

	void showTrayTooltip() override;

	QTimer psUpdatedPositionTimer;

private:
	void updateIconCounters();

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

	int _deltaLeft = 0;
	int _deltaTop = 0;

};

Q_DECLARE_OPERATORS_FOR_FLAGS(MainWindow::ShadowsChanges);

} // namespace Platform
