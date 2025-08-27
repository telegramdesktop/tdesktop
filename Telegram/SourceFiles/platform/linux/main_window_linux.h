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

namespace Platform {

class MainWindow : public Window::MainWindow {
public:
	explicit MainWindow(not_null<Window::Controller*> controller);
	~MainWindow();

	void updateWindowIcon() override;

protected:
	bool eventFilter(QObject *obj, QEvent *evt) override;

	void unreadCounterChangedHook() override;
	void updateGlobalMenuHook() override;

	void workmodeUpdated(Core::Settings::WorkMode mode) override;
	void createGlobalMenu() override;

private:
	void updateUnityCounter();

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
	QAction *psBlockquote = nullptr;
	QAction *psMonospace = nullptr;
	QAction *psClearFormat = nullptr;

	bool _exposed = false;

};

[[nodiscard]] inline int32 ScreenNameChecksum(const QString &name) {
	return Window::DefaultScreenNameChecksum(name);
}

} // namespace Platform
