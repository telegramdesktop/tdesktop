/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"
#include "platform/mac/specific_mac_p.h"
#include "base/timer.h"

#include <QtWidgets/QMenuBar>
#include <QtCore/QTimer>

namespace Platform {

class MainWindow : public Window::MainWindow {
	// The Q_OBJECT meta info is used for qobject_cast!
	Q_OBJECT

public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	bool psFilterNativeEvent(void *event);

	virtual QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) = 0;

	int getCustomTitleHeight() const {
		return _customTitleHeight;
	}

	~MainWindow();

	void updateWindowIcon() override;

	class Private;

public slots:
	void psShowTrayMenu();

	void psMacUndo();
	void psMacRedo();
	void psMacCut();
	void psMacCopy();
	void psMacPaste();
	void psMacDelete();
	void psMacSelectAll();
	void psMacEmojiAndSymbols();

	void psMacBold();
	void psMacItalic();
	void psMacUnderline();
	void psMacStrikeOut();
	void psMacMonospace();
	void psMacClearFormat();

protected:
	bool eventFilter(QObject *obj, QEvent *evt) override;

	void handleActiveChangedHook() override;
	void stateChangedHook(Qt::WindowState state) override;
	void initHook() override;
	void titleVisibilityChangedHook() override;
	void unreadCounterChangedHook() override;

	QImage psTrayIcon(bool selected = false) const;
	bool hasTrayIcon() const override {
		return trayIcon;
	}

	void updateGlobalMenuHook() override;

	void workmodeUpdated(DBIWorkMode mode) override;

	QSystemTrayIcon *trayIcon = nullptr;
	QMenu *trayIconMenu = nullptr;

	QImage trayImg, trayImgSel;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();
	virtual void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) = 0;

	QTimer psUpdatedPositionTimer;

	void initShadows() override;
	void closeWithoutDestroy() override;
	void createGlobalMenu() override;

private:
	friend class Private;

	void initTouchBar();
	void hideAndDeactivate();
	void updateTitleCounter();
	void updateIconCounters();
	void destroyCurrentTouchBar();

	std::unique_ptr<Private> _private;

	mutable bool psIdle;
	mutable QTimer psIdleTimer;

	base::Timer _hideAfterFullScreenTimer;

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

	QAction *psBold = nullptr;
	QAction *psItalic = nullptr;
	QAction *psUnderline = nullptr;
	QAction *psStrikeOut = nullptr;
	QAction *psMonospace = nullptr;
	QAction *psClearFormat = nullptr;

	int _customTitleHeight = 0;

};

} // namespace Platform
