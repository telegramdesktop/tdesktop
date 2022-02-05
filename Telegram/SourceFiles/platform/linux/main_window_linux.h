/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"

class QMenuBar;

namespace Platform {

class MainWindow : public Window::MainWindow {
public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	void psShowTrayMenu();

	bool isActiveForTrayMenu() override;

	~MainWindow();

protected:
	void initHook() override;
	void unreadCounterChangedHook() override;
	void updateGlobalMenuHook() override;

	void initTrayMenuHook() override;
	bool hasTrayIcon() const override;

	void workmodeUpdated(Core::Settings::WorkMode mode) override;
	void createGlobalMenu() override;

	QSystemTrayIcon *trayIcon = nullptr;
	QMenu *trayIconMenu = nullptr;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();

private:
	class Private;
	friend class Private;
	const std::unique_ptr<Private> _private;

	QMenuBar *psMainMenu = nullptr;
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

	QAction *psBold = nullptr;
	QAction *psItalic = nullptr;
	QAction *psUnderline = nullptr;
	QAction *psStrikeOut = nullptr;
	QAction *psMonospace = nullptr;
	QAction *psClearFormat = nullptr;

	void updateIconCounters();
	void handleNativeSurfaceChanged(bool exist);

};

} // namespace Platform
