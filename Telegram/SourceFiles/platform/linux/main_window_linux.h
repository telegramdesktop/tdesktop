/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"

#include "ui/widgets/popup_menu.h"

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "statusnotifieritem.h"
#include <QtCore/QTemporaryFile>
#include <QtDBus/QDBusObjectPath>
#include <dbusmenuexporter.h>
#endif

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
typedef struct _GtkClipboard GtkClipboard;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION


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

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	void onSNIOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner);

	void onAppMenuOwnerChanged(
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

	void onVisibleChanged(bool visible);

#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	GtkClipboard *gtkClipboard() {
		return _gtkClipboard;
	}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

protected:
	void initHook() override;
	void unreadCounterChangedHook() override;
	void updateGlobalMenuHook() override;

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
	Ui::PopupMenu *_trayIconMenuXEmbed = nullptr;

	void updateIconCounters();
	void updateWaylandDecorationColors();

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	StatusNotifierItem *_sniTrayIcon = nullptr;
	std::unique_ptr<QTemporaryFile> _trayIconFile = nullptr;

	DBusMenuExporter *_mainMenuExporter = nullptr;
	QDBusObjectPath _mainMenuPath;

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
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	GtkClipboard *_gtkClipboard = nullptr;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

};

} // namespace Platform
