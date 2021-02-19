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

class WaylandIntegration::Private {
};

WaylandIntegration::WaylandIntegration() {
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

void WaylandIntegration::waitForInterfaceAnnounce() {
}

bool WaylandIntegration::supportsXdgDecoration() {
	return false;
}

} // namespace internal
} // namespace Platform
