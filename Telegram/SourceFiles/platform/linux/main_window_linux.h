/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"

#include "ui/widgets/popup_menu.h"

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
#include "statusnotifieritem.h"
#include <QtCore/QTemporaryFile>
#endif

namespace Platform {

class MainWindow : public Window::MainWindow {
	Q_OBJECT

public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	virtual QImage iconWithCounter(
		int size,
		int count,
		style::color bg,
		style::color fg,
		bool smallIcon) = 0;

	static void LibsLoaded();

	~MainWindow();

public slots:
	void psShowTrayMenu();

protected:
	void unreadCounterChangedHook() override;

	void initTrayMenuHook() override;
	bool hasTrayIcon() const override;

	void workmodeUpdated(DBIWorkMode mode) override;

	QSystemTrayIcon *trayIcon = nullptr;
	QMenu *trayIconMenu = nullptr;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();

	virtual void placeSmallCounter(
		QImage &img,
		int size,
		int count,
		style::color bg,
		const QPoint &shift,
		style::color color) = 0;

private:
	Ui::PopupMenu *_trayIconMenuXEmbed = nullptr;

	void updateIconCounters();

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	StatusNotifierItem *_sniTrayIcon = nullptr;
	std::unique_ptr<QTemporaryFile> _trayIconFile = nullptr;
	std::unique_ptr<QTemporaryFile> _trayToolTipIconFile = nullptr;

	void setSNITrayIcon(const QIcon &icon);
	void attachToSNITrayIcon();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

};

} // namespace Platform
