
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/notifications_manager_linux.h"

#include "base/platform/base_platform_info.h"

namespace Platform {
namespace Notifications {

bool SkipAudio() {
	return false;
}

bool SkipToast() {
	return false;
}

bool SkipFlashBounce() {
	return false;
}

bool Supported() {
	return false;
}

bool Enforced() {
	// Wayland doesn't support positioning
	// and custom notifications don't work here
	return IsWayland();
}

void Create(Window::Notifications::System *system) {
	if (Enforced()) {
		using DummyManager = Window::Notifications::DummyManager;
		system->setManager(std::make_unique<DummyManager>(system));
	} else {
		system->setManager(nullptr);
	}
}

} // namespace Notifications
} // namespace Platform
