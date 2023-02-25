/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWindow;
} // namespace Ui

namespace Ui::Platform {
struct SeparateTitleControls;
} // namespace Ui::Platform

namespace Platform {

class OverlayWidgetHelper {
public:
	virtual ~OverlayWidgetHelper() = default;

	virtual void orderWidgets() {
	}
	[[nodiscard]] virtual bool skipTitleHitTest(QPoint position) {
		return false;
	}
	virtual void beforeShow(bool fullscreen) {
	}
	virtual void afterShow(bool fullscreen) {
	}
	virtual void notifyFileDialogShown(bool shown) {
	}
	virtual void minimize(not_null<Ui::RpWindow*> window);
};

[[nodiscard]] std::unique_ptr<OverlayWidgetHelper> CreateOverlayWidgetHelper(
	not_null<Ui::RpWindow*> window,
	Fn<void(bool)> maximize);

class DefaultOverlayWidgetHelper final : public OverlayWidgetHelper {
public:
	DefaultOverlayWidgetHelper(
		not_null<Ui::RpWindow*> window,
		Fn<void(bool)> maximize);
	~DefaultOverlayWidgetHelper();

	void orderWidgets() override;
	bool skipTitleHitTest(QPoint position) override;

private:
	const std::unique_ptr<Ui::Platform::SeparateTitleControls> _controls;

};

} // namespace Platform

// Platform dependent implementations.

#ifdef Q_OS_MAC
#include "platform/mac/overlay_widget_mac.h"
#elif defined Q_OS_UNIX // Q_OS_MAC
#include "platform/linux/overlay_widget_linux.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_UNIX
#include "platform/win/overlay_widget_win.h"
#endif // Q_OS_MAC || Q_OS_UNIX || Q_OS_WIN
