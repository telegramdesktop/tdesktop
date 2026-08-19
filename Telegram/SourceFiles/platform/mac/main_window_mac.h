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

	void closeWithoutDestroy() override;

private:
	friend class Private;

	bool nativeEvent(
		const QByteArray &eventType,
		void *message,
		qintptr *result) override;

	void hideAndDeactivate();
	void updateDockCounter();

	std::unique_ptr<Private> _private;

	mutable QTimer psIdleTimer;

	base::Timer _hideAfterFullScreenTimer;

	rpl::event_stream<QPoint> _forceClicks;
	int _customTitleHeight = 0;
	int _lastPressureStage = 0;

};

[[nodiscard]] int32 ScreenNameChecksum(const QString &name);
[[nodiscard]] int32 ScreenNameChecksum(const QScreen *screen);

[[nodiscard]] QString ScreenDisplayLabel(const QScreen *screen);

} // namespace Platform
