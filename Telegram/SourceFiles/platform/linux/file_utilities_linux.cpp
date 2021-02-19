/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/file_utilities_linux.h"

#include "platform/linux/linux_gtk_integration.h"
#include "platform/linux/specific_linux.h"

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "platform/linux/linux_xdp_file_dialog.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#include <QtGui/QDesktopServices>

extern "C" {
#undef signals
#include <gio/gio.h>
#define signals public
} // extern "C"

using Platform::internal::GtkIntegration;

namespace Platform {
namespace File {

void UnsafeOpenUrl(const QString &url) {
	if (!g_app_info_launch_default_for_uri(
		url.toUtf8().constData(),
		nullptr,
		nullptr)) {
		QDesktopServices::openUrl(url);
	}
}

void UnsafeOpenEmailLink(const QString &email) {
	UnsafeOpenUrl(qstr("mailto:") + email);
}

bool UnsafeShowOpenWith(const QString &filepath) {
	if (InFlatpak() || InSnap()) {
		return false;
	}

	if (const auto integration = GtkIntegration::Instance()) {
		const auto absolutePath = QFileInfo(filepath).absoluteFilePath();
		return integration->showOpenWithDialog(absolutePath);
	}

	return false;
}

void UnsafeLaunch(const QString &filepath) {
	const auto absolutePath = QFileInfo(filepath).absoluteFilePath();

	if (!g_app_info_launch_default_for_uri(
		g_filename_to_uri(
			absolutePath.toUtf8().constData(),
			nullptr,
			nullptr),
		nullptr,
		nullptr)) {
		if (!UnsafeShowOpenWith(filepath)) {
			QDesktopServices::openUrl(QUrl::fromLocalFile(filepath));
		}
	}
}

} // namespace File

namespace FileDialog {

bool Get(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		::FileDialog::internal::Type type,
		QString startFile) {
	if (parent) {
		parent = parent->window();
	}
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (XDP::Use(type)) {
		return XDP::Get(
			parent,
			files,
			remoteContent,
			caption,
			filter,
			type,
			startFile);
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (const auto integration = GtkIntegration::Instance()) {
		if (integration->fileDialogSupported()
			&& integration->useFileDialog(type)) {
			return integration->getFileDialog(
				parent,
				files,
				remoteContent,
				caption,
				filter,
				type,
				startFile);
		}
	}
	return ::FileDialog::internal::GetDefault(
		parent,
		files,
		remoteContent,
		caption,
		filter,
		type,
		startFile);
}

} // namespace FileDialog
} // namespace Platform
