/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"

namespace Platform {
namespace internal {

WaylandIntegration::WaylandIntegration() {
}

WaylandIntegration *WaylandIntegration::Instance() {
	if (!IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

bool WaylandIntegration::startMove(QWindow *window) {
	return false;
}

bool WaylandIntegration::startResize(QWindow *window, Qt::Edges edges) {
	return false;
}

bool WaylandIntegration::showWindowMenu(QWindow *window) {
	return false;
}

} // namespace internal
} // namespace Platform
