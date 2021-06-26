/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gtk_integration.h"

namespace Platform {
namespace internal {

GtkIntegration::GtkIntegration() {
}

GtkIntegration *GtkIntegration::Instance() {
	return nullptr;
}

void GtkIntegration::load() {
}

std::optional<int> GtkIntegration::scaleFactor() const {
	return std::nullopt;
}

bool GtkIntegration::showOpenWithDialog(const QString &filepath) const {
	return false;
}

QImage GtkIntegration::getImageFromClipboard() const {
	return {};
}

} // namespace internal
} // namespace Platform
