/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_overlay_widget.h"

#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/rp_window.h"
#include "styles/style_calls.h"

namespace Platform {

void OverlayWidgetHelper::minimize(not_null<Ui::RpWindow*> window) {
	window->setWindowState(window->windowState() | Qt::WindowMinimized);
}

DefaultOverlayWidgetHelper::DefaultOverlayWidgetHelper(
	not_null<Ui::RpWindow*> window,
	Fn<void(bool)> maximize)
: _controls(Ui::Platform::SetupSeparateTitleControls(
	window,
	st::callTitle,
	std::move(maximize))) {
}

DefaultOverlayWidgetHelper::~DefaultOverlayWidgetHelper() = default;

void DefaultOverlayWidgetHelper::orderWidgets() {
	_controls->wrap.raise();
}

bool DefaultOverlayWidgetHelper::skipTitleHitTest(QPoint position) {
	return _controls->controls.geometry().contains(position);
}

} // namespace Platform
