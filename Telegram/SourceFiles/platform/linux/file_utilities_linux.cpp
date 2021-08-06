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
#include "platform/linux/linux_xdp_open_with_dialog.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#include <QtCore/QProcess>
#include <QtGui/QDesktopServices>

#include <glibmm.h>
#include <giomm.h>

using Platform::internal::GtkIntegration;

namespace Platform {
namespace File {

void UnsafeOpenUrl(const QString &url) {
	try {
		if (Gio::AppInfo::launch_default_for_uri(url.toStdString())) {
			return;
		}
	} catch (const Glib::Error &e) {
		LOG(("App Error: %1").arg(QString::fromStdString(e.what())));
	}

	if (QDesktopServices::openUrl(url)) {
		return;
	}

	QProcess::startDetached(qsl("xdg-open"), { url });
}

void UnsafeOpenEmailLink(const QString &email) {
	UnsafeOpenUrl(qstr("mailto:") + email);
}

bool UnsafeShowOpenWith(const QString &filepath) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (internal::ShowXDPOpenWithDialog(filepath)) {
		return true;
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	if (InFlatpak() || InSnap()) {
		return false;
	}

	if (const auto integration = GtkIntegration::Instance()) {
		return integration->showOpenWithDialog(filepath);
	}

	return false;
}

void UnsafeLaunch(const QString &filepath) {
	try {
		if (Gio::AppInfo::launch_default_for_uri(
			Glib::filename_to_uri(filepath.toStdString()))) {
			return;
		}
	} catch (const Glib::Error &e) {
		LOG(("App Error: %1").arg(QString::fromStdString(e.what())));
	}

	if (UnsafeShowOpenWith(filepath)) {
		return;
	}

	const auto qUrlPath = QUrl::fromLocalFile(filepath);
	if (QDesktopServices::openUrl(qUrlPath)) {
		return;
	}

	QProcess::startDetached(qsl("xdg-open"), { qUrlPath.toEncoded() });
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
	{
		const auto result = XDP::Get(
			parent,
			files,
			remoteContent,
			caption,
			filter,
			type,
			startFile);

		if (result.has_value()) {
			return *result;
		}
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
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
