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

struct WaylandIntegration::Private {
};

WaylandIntegration::WaylandIntegration() {
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

QString WaylandIntegration::nativeHandle(QWindow *window) {
	return {};
}

bool WaylandIntegration::skipTaskbarSupported() {
	return false;
}

void WaylandIntegration::skipTaskbar(QWindow *window, bool skip) {
}

void WaylandIntegration::registerAppMenu(
		QWindow *window,
		const QString &serviceName,
		const QString &objectPath) {
}

} // namespace internal
} // namespace Platform
