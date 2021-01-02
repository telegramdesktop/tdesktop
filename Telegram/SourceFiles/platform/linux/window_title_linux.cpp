/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/window_title_linux.h"

#include "platform/linux/linux_wayland_integration.h"
#include "base/platform/base_platform_info.h"

namespace Platform {
namespace {

bool SystemMoveResizeSupported() {
#if !defined DESKTOP_APP_DISABLE_WAYLAND_INTEGRATION || QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
	return true;
#else // !DESKTOP_APP_DISABLE_WAYLAND_INTEGRATION || Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
	return !IsWayland();
#endif // !DESKTOP_APP_DISABLE_WAYLAND_INTEGRATION || Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
}

} // namespace

bool AllowNativeWindowFrameToggle() {
	const auto waylandIntegration = internal::WaylandIntegration::Instance();
	return SystemMoveResizeSupported()
		&& (!waylandIntegration
			|| waylandIntegration->supportsXdgDecoration());
}

object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent) {
	return SystemMoveResizeSupported()
		? object_ptr<Window::TitleWidgetQt>(parent)
		: object_ptr<Window::TitleWidgetQt>{ nullptr };
}

} // namespace Platform
