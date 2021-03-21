/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/file_utilities_linux.h"

#include "platform/linux/linux_gtk_integration.h"
#include "platform/linux/specific_linux.h"
#include "storage/localstorage.h"

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "platform/linux/linux_xdp_file_dialog.h"
#include "platform/linux/linux_xdp_open_with_dialog.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#include <QtCore/QProcess>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QFileDialog>

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
namespace {

using Type = ::FileDialog::internal::Type;

bool GetQt(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		Type type,
		QString startFile) {
	if (cDialogLastPath().isEmpty()) {
		InitLastPath();
	}

	QFileDialog dialog(parent, caption, QString(), filter);

	dialog.setOptions(QFileDialog::DontUseNativeDialog);
	dialog.setModal(true);
	if (type == Type::ReadFile || type == Type::ReadFiles) {
		dialog.setFileMode((type == Type::ReadFiles)
			? QFileDialog::ExistingFiles
			: QFileDialog::ExistingFile);
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
	} else if (type == Type::ReadFolder) {
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
		dialog.setFileMode(QFileDialog::Directory);
		dialog.setOption(QFileDialog::ShowDirsOnly);
	} else {
		dialog.setFileMode(QFileDialog::AnyFile);
		dialog.setAcceptMode(QFileDialog::AcceptSave);
	}
	if (startFile.isEmpty() || startFile.at(0) != '/') {
		startFile = cDialogLastPath() + '/' + startFile;
	}
	dialog.setDirectory(QFileInfo(startFile).absoluteDir().absolutePath());
	if (type == Type::WriteFile) {
		dialog.selectFile(startFile);
	}

	const auto res = dialog.exec();

	if (type != Type::ReadFolder) {
		// Save last used directory for all queries except directory choosing.
		const auto path = dialog.directory().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeSettings();
		}
	}

	if (res == QDialog::Accepted) {
		if (type == Type::ReadFiles) {
			files = dialog.selectedFiles();
		} else {
			files = dialog.selectedFiles().mid(0, 1);
		}
		return true;
	}

	files = QStringList();
	remoteContent = QByteArray();
	return false;
}

}

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
		if (integration->useFileDialog(type)) {
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
	// avoid situation when portals don't work
	// and Qt tries to use portals as well
	if (InFlatpak() || InSnap()) {
		return GetQt(
			parent,
			files,
			remoteContent,
			caption,
			filter,
			type,
			startFile);
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
