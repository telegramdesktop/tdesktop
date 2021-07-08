/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gtk_integration.h"

namespace Platform {
namespace internal {

class GtkIntegration::Private {
};

GtkIntegration::GtkIntegration() {
}

GtkIntegration::~GtkIntegration() = default;

GtkIntegration *GtkIntegration::Instance() {
	return nullptr;
}

void GtkIntegration::load(const QString &allowedBackends) {
}

int GtkIntegration::exec(const QString &parentDBusName) {
	return 1;
}

bool GtkIntegration::showOpenWithDialog(const QString &filepath) const {
	return false;
}

QImage GtkIntegration::getImageFromClipboard() const {
	return {};
}

QString GtkIntegration::AllowedBackends() {
	return {};
}

int GtkIntegration::Exec(
		Type type,
		const QString &parentDBusName,
		const QString &serviceName) {
	return 1;
}

void GtkIntegration::Start(Type type) {
}

void GtkIntegration::Autorestart(Type type) {
}

} // namespace internal
} // namespace Platform
