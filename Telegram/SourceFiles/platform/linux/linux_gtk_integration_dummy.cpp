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

bool GtkIntegration::useFileDialog(FileDialogType type) const {
	return false;
}

bool GtkIntegration::getFileDialog(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		FileDialogType type,
		QString startFile) const {
	return false;
}

bool GtkIntegration::showOpenWithDialog(const QString &filepath) const {
	return false;
}

QImage GtkIntegration::getImageFromClipboard() const {
	return {};
}

} // namespace internal
} // namespace Platform
