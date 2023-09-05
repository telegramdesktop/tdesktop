/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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

bool WaylandIntegration::skipTaskbarSupported() {
	return false;
}

void WaylandIntegration::skipTaskbar(QWindow *window, bool skip) {
}

} // namespace internal
} // namespace Platform
