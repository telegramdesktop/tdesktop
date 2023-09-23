/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/file_utilities_linux.h"

#include "base/platform/linux/base_linux_app_launch_context.h"
#include "platform/linux/linux_xdp_open_with_dialog.h"

#include <QtGui/QDesktopServices>

#include <gio/gio.hpp>

using namespace gi::repository;

namespace Platform {
namespace File {

void UnsafeOpenUrl(const QString &url) {
	{
		const auto result = Gio::AppInfo::launch_default_for_uri(
			url.toStdString(),
			base::Platform::AppLaunchContext());

		if (!result) {
			LOG(("App Error: %1").arg(result.error().what()));
		} else if (*result) {
			return;
		}
	}

	QDesktopServices::openUrl(url);
}

void UnsafeOpenEmailLink(const QString &email) {
	UnsafeOpenUrl(u"mailto:"_q + email);
}

bool UnsafeShowOpenWith(const QString &filepath) {
	if (internal::ShowXDPOpenWithDialog(filepath)) {
		return true;
	}

	return false;
}

void UnsafeLaunch(const QString &filepath) {
	if ([&] {
		const auto filename = GLib::filename_to_uri(filepath.toStdString());
		if (!filename) {
			LOG(("App Error: %1").arg(filename.error().what()));

			return false;
		}

		const auto result = Gio::AppInfo::launch_default_for_uri(
			*filename,
			base::Platform::AppLaunchContext());

		if (!result) {
			LOG(("App Error: %1").arg(result.error().what()));

			return false;
		}

		return *result;
	}()) {
		return;
	}

	if (UnsafeShowOpenWith(filepath)) {
		return;
	}

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
