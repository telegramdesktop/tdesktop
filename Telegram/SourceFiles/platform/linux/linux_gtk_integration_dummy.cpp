/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gtk_integration.h"

namespace Platform {
namespace internal {

QString GtkIntegration::AllowedBackends() {
	return {};
}

void GtkIntegration::Start(Type type) {
}

void GtkIntegration::Autorestart(Type type) {
}

} // namespace internal
} // namespace Platform
