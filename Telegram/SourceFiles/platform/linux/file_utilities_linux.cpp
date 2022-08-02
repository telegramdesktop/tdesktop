/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/file_utilities_linux.h"

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "base/platform/linux/base_linux_app_launch_context.h"
#include "platform/linux/linux_xdp_open_with_dialog.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#include <QtGui/QDesktopServices>

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include <glibmm.h>
#include <giomm.h>
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

namespace Platform {
namespace File {

void UnsafeOpenUrl(const QString &url) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	try {
		if (Gio::AppInfo::launch_default_for_uri(
			url.toStdString(),
			base::Platform::AppLaunchContext())) {
			return;
		}
	} catch (const Glib::Error &e) {
		LOG(("App Error: %1").arg(QString::fromStdString(e.what())));
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	QDesktopServices::openUrl(url);
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

	return false;
}

void UnsafeLaunch(const QString &filepath) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	try {
		if (Gio::AppInfo::launch_default_for_uri(
			Glib::filename_to_uri(filepath.toStdString()),
			base::Platform::AppLaunchContext())) {
			return;
		}
	} catch (const Glib::Error &e) {
		LOG(("App Error: %1").arg(QString::fromStdString(e.what())));
	}

	if (UnsafeShowOpenWith(filepath)) {
		return;
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	QDesktopServices::openUrl(QUrl::fromLocalFile(filepath));
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
	// Workaround for sandboxed paths
	static const auto docRegExp = QRegularExpression("^/run/user/\\d+/doc");
	if (cDialogLastPath().contains(docRegExp)) {
		InitLastPath();
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
