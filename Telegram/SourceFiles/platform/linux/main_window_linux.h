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

namespace Platform {

class MainWindow : public Window::MainWindow {
	Q_OBJECT

public:
	MainWindow();

	void psFirstShow();
	void psInitSysMenu();
	void psUpdateMargins();

	void psRefreshTaskbarIcon() {
	}

	virtual QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) = 0;

	static void LibsLoaded();

	~MainWindow();

public slots:
	void psShowTrayMenu();

	void psStatusIconCheck();
	void psUpdateIndicator();

protected:
	void unreadCounterChangedHook() override;

	bool hasTrayIcon() const override;

	void workmodeUpdated(DBIWorkMode mode) override;

	QSystemTrayIcon *trayIcon = nullptr;
	QMenu *trayIconMenu = nullptr;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();

	virtual void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) = 0;

private:
	void updateIconCounters();
	void psCreateTrayIcon();

	QTimer _psCheckStatusIconTimer;
	int _psCheckStatusIconLeft = 100;

	QTimer _psUpdateIndicatorTimer;
	TimeMs _psLastIndicatorUpdate = 0;

};

} // namespace Platform
