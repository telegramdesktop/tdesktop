/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"
#include "base/unique_qptr.h"

class QMenuBar;

namespace Ui {
class PopupMenu;
} // namespace Ui

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
class QTemporaryFile;
class StatusNotifierItem;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

namespace Platform {

class MainWindow : public Window::MainWindow {
public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	virtual QImage iconWithCounter(
		int size,
		int count,
		style::color bg,
		style::color fg,
		bool smallIcon) = 0;

	void psShowTrayMenu();

	bool trayAvailable() {
		return _sniAvailable || QSystemTrayIcon::isSystemTrayAvailable();
	}

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

	virtual void placeSmallCounter(
		QImage &img,
		int size,
		int count,
		style::color bg,
		const QPoint &shift,
		style::color color) = 0;

private:
	class Private;
	const std::unique_ptr<Private> _private;
	bool _sniAvailable = false;
	base::unique_qptr<Ui::PopupMenu> _trayIconMenuXEmbed;

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

	void psLinuxUndo();
	void psLinuxRedo();
	void psLinuxCut();
	void psLinuxCopy();
	void psLinuxPaste();
	void psLinuxDelete();
	void psLinuxSelectAll();

	void psLinuxBold();
	void psLinuxItalic();
	void psLinuxUnderline();
	void psLinuxStrikeOut();
	void psLinuxMonospace();
	void psLinuxClearFormat();

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	StatusNotifierItem *_sniTrayIcon = nullptr;
	uint _sniRegisteredSignalId = 0;
	uint _sniWatcherId = 0;
	std::unique_ptr<QTemporaryFile> _trayIconFile;

	void setSNITrayIcon(int counter, bool muted);
	void attachToSNITrayIcon();
	void handleSNIHostRegistered();

	void handleSNIOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner);
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

};

} // namespace Platform
