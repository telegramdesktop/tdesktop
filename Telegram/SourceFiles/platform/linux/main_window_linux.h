/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
class QTemporaryFile;
class DBusMenuExporter;
class StatusNotifierItem;

typedef void* gpointer;
typedef char gchar;
typedef struct _GVariant GVariant;
typedef struct _GDBusProxy GDBusProxy;
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

	static void LibsLoaded();

	~MainWindow();

protected:
	void initHook() override;
	void unreadCounterChangedHook() override;
	void updateGlobalMenuHook() override;
	void handleVisibleChangedHook(bool visible) override;

	void initTrayMenuHook() override;
	bool hasTrayIcon() const override;

	void workmodeUpdated(DBIWorkMode mode) override;
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
	bool _sniAvailable = false;
	Ui::PopupMenu *_trayIconMenuXEmbed = nullptr;

	void updateIconCounters();
	void updateWaylandDecorationColors();

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	StatusNotifierItem *_sniTrayIcon = nullptr;
	GDBusProxy *_sniDBusProxy = nullptr;
	std::unique_ptr<QTemporaryFile> _trayIconFile;

	bool _appMenuSupported = false;
	DBusMenuExporter *_mainMenuExporter = nullptr;

	QMenu *psMainMenu = nullptr;
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

	void setSNITrayIcon(int counter, bool muted);
	void attachToSNITrayIcon();
	void handleSNIHostRegistered();

	void handleSNIOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner);

	void handleAppMenuOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner);

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

	static void sniSignalEmitted(
		GDBusProxy *proxy,
		gchar *sender_name,
		gchar *signal_name,
		GVariant *parameters,
		gpointer user_data);
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

};

} // namespace Platform
