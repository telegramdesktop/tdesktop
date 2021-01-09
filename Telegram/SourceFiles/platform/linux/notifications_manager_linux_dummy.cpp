
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/notifications_manager_linux.h"

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

std::unique_ptr<Window::Notifications::Manager> Create(
		Window::Notifications::System *system) {
	if (IsWayland()) {
		return std::make_unique<Window::Notifications::DummyManager>(system);
	}

	return nullptr;
}

} // namespace Notifications
} // namespace Platform
