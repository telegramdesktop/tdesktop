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
public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	int getCustomTitleHeight() const {
		return _customTitleHeight;
	}

	~MainWindow();

	void updateWindowIcon() override;

	rpl::producer<QPoint> globalForceClicks() override {
		return _forceClicks.events();
	}

	class Private;

protected:
	bool eventFilter(QObject *obj, QEvent *evt) override;

	void stateChangedHook(Qt::WindowState state) override;
	void initHook() override;
	void unreadCounterChangedHook() override;

	void updateGlobalMenuHook() override;

	void closeWithoutDestroy() override;
	void createGlobalMenu() override;

private:
	friend class Private;

	bool nativeEvent(
		const QByteArray &eventType,
		void *message,
		qintptr *result) override;

	void hideAndDeactivate();
	void updateDockCounter();

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
	QAction *psBlockquote = nullptr;
	QAction *psMonospace = nullptr;
	QAction *psClearFormat = nullptr;

	rpl::event_stream<QPoint> _forceClicks;
	int _customTitleHeight = 0;
	int _lastPressureStage = 0;

};

[[nodiscard]] inline int32 ScreenNameChecksum(const QString &name) {
	return Window::DefaultScreenNameChecksum(name);
}

} // namespace Platform
