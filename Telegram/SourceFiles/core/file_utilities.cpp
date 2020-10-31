/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/file_utilities.h"

#include "boxes/abstract_box.h"
#include "storage/localstorage.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/base_platform_file_utilities.h"
#include "platform/platform_file_utilities.h"
#include "core/application.h"
#include "base/unixtime.h"
#include "ui/delayed_activation.h"
#include "ui/chat/attach/attach_extensions.h"
#include "main/main_session.h"
#include "mainwindow.h"

#include <QtWidgets/QFileDialog>
#include <QtCore/QCoreApplication>
#include <QtCore/QStandardPaths>
#include <QtGui/QDesktopServices>

bool filedialogGetSaveFile(
		QPointer<QWidget> parent,
		QString &file,
		const QString &caption,
		const QString &filter,
		const QString &initialPath) {
	QStringList files;
	QByteArray remoteContent;
	Ui::PreventDelayedActivation();
	bool result = Platform::FileDialog::Get(
		parent,
		files,
		remoteContent,
		caption,
		filter,
		FileDialog::internal::Type::WriteFile,
		initialPath);
	file = files.isEmpty() ? QString() : files.at(0);
	return result;
}

bool filedialogGetSaveFile(
		QString &file,
		const QString &caption,
		const QString &filter,
		const QString &initialPath) {
	return filedialogGetSaveFile(
		Core::App().getFileDialogParent(),
		file,
		caption,
		filter,
		initialPath);
}

QString filedialogDefaultName(
		const QString &prefix,
		const QString &extension,
		const QString &path,
		bool skipExistance,
		TimeId fileTime) {
	auto directoryPath = path;
	if (directoryPath.isEmpty()) {
		if (cDialogLastPath().isEmpty()) {
			Platform::FileDialog::InitLastPath();
		}
		directoryPath = cDialogLastPath();
	}

	QString base;
	if (fileTime) {
		const auto date = base::unixtime::parse(fileTime);
		base = prefix + date.toString("_yyyy-MM-dd_HH-mm-ss");
	} else {
		struct tm tm;
		time_t t = time(NULL);
		mylocaltime(&tm, &t);

		QChar zero('0');
		base = prefix + qsl("_%1-%2-%3_%4-%5-%6").arg(tm.tm_year + 1900).arg(tm.tm_mon + 1, 2, 10, zero).arg(tm.tm_mday, 2, 10, zero).arg(tm.tm_hour, 2, 10, zero).arg(tm.tm_min, 2, 10, zero).arg(tm.tm_sec, 2, 10, zero);
	}

	QString name;
	if (skipExistance) {
		name = base + extension;
	} else {
		QDir directory(directoryPath);
		const auto dir = directory.absolutePath();
		const auto nameBase = (dir.endsWith('/') ? dir : (dir + '/'))
			+ base;
		name = nameBase + extension;
		for (int i = 0; QFileInfo(name).exists(); ++i) {
			name = nameBase + qsl(" (%1)").arg(i + 2) + extension;
		}
	}
	return name;
}

QString filedialogNextFilename(
		const QString &name,
		const QString &cur,
		const QString &path) {
	QDir directory(path.isEmpty() ? cDialogLastPath() : path);
	int32 extIndex = name.lastIndexOf('.');
	QString prefix = name, extension;
	if (extIndex >= 0) {
		extension = name.mid(extIndex);
		prefix = name.mid(0, extIndex);
	}
	const auto dir = directory.absolutePath();
	const auto nameBase = (dir.endsWith('/') ? dir : (dir + '/')) + prefix;
	auto result = nameBase + extension;
	for (int i = 0; result.toLower() != cur.toLower() && QFileInfo(result).exists(); ++i) {
		result = nameBase + qsl(" (%1)").arg(i + 2) + extension;
	}
	return result;
}

namespace File {

void OpenUrl(const QString &url) {
	crl::on_main([=] {
		Ui::PreventDelayedActivation();
		Platform::File::UnsafeOpenUrl(url);
	});
}

void OpenEmailLink(const QString &email) {
	crl::on_main([=] {
		Ui::PreventDelayedActivation();
		Platform::File::UnsafeOpenEmailLink(email);
	});
}

void OpenWith(const QString &filepath, QPoint menuPosition) {
	InvokeQueued(QCoreApplication::instance(), [=] {
		if (!Platform::File::UnsafeShowOpenWithDropdown(filepath, menuPosition)) {
			Ui::PreventDelayedActivation();
			if (!Platform::File::UnsafeShowOpenWith(filepath)) {
				Platform::File::UnsafeLaunch(filepath);
			}
		}
	});
}

void Launch(const QString &filepath) {
	crl::on_main([=] {
		Ui::PreventDelayedActivation();
		Platform::File::UnsafeLaunch(filepath);
	});
}

void ShowInFolder(const QString &filepath) {
	crl::on_main([=] {
		Ui::PreventDelayedActivation();
		if (Platform::IsLinux()) {
			// Hide mediaview to make other apps visible.
			Ui::hideLayer(anim::type::instant);
		}
		base::Platform::ShowInFolder(filepath);
	});
}

QString DefaultDownloadPathFolder(not_null<Main::Session*> session) {
	return session->supportMode() ? u"Tsupport Desktop"_q : AppName.utf16();
}

QString DefaultDownloadPath(not_null<Main::Session*> session) {
	return QStandardPaths::writableLocation(
		QStandardPaths::DownloadLocation)
		+ '/'
		+ DefaultDownloadPathFolder(session)
		+ '/';
}

namespace internal {

void UnsafeOpenUrlDefault(const QString &url) {
	QDesktopServices::openUrl(url);
}

void UnsafeOpenEmailLinkDefault(const QString &email) {
	auto url = QUrl(qstr("mailto:") + email);
	QDesktopServices::openUrl(url);
}

void UnsafeLaunchDefault(const QString &filepath) {
	QDesktopServices::openUrl(QUrl::fromLocalFile(filepath));
}

} // namespace internal
} // namespace File

namespace FileDialog {

void GetOpenPath(
		QPointer<QWidget> parent,
		const QString &caption,
		const QString &filter,
		Fn<void(OpenResult &&result)> callback,
		Fn<void()> failed) {
	InvokeQueued(QCoreApplication::instance(), [=] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
		Ui::PreventDelayedActivation();
		const auto success = Platform::FileDialog::Get(
			parent,
			files,
			remoteContent,
			caption,
			filter,
			FileDialog::internal::Type::ReadFile);
		if (success
			&& ((!files.isEmpty() && !files[0].isEmpty())
				|| !remoteContent.isEmpty())) {
			if (callback) {
				auto result = OpenResult();
				if (!files.isEmpty() && !files[0].isEmpty()) {
					result.paths.push_back(files[0]);
				}
				result.remoteContent = remoteContent;
				callback(std::move(result));
			}
		} else if (failed) {
			failed();
		}
	});
}

void GetOpenPaths(
		QPointer<QWidget> parent,
		const QString &caption,
		const QString &filter,
		Fn<void(OpenResult &&result)> callback,
		Fn<void()> failed) {
	InvokeQueued(QCoreApplication::instance(), [=] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
		Ui::PreventDelayedActivation();
		const auto success = Platform::FileDialog::Get(
			parent,
			files,
			remoteContent,
			caption,
			filter,
			FileDialog::internal::Type::ReadFiles);
		if (success && (!files.isEmpty() || !remoteContent.isEmpty())) {
			if (callback) {
				auto result = OpenResult();
				result.paths = files;
				result.remoteContent = remoteContent;
				callback(std::move(result));
			}
		} else if (failed) {
			failed();
		}
	});
}

void GetWritePath(
		QPointer<QWidget> parent,
		const QString &caption,
		const QString &filter,
		const QString &initialPath,
		Fn<void(QString &&result)> callback,
		Fn<void()> failed) {
	InvokeQueued(QCoreApplication::instance(), [=] {
		auto file = QString();
		if (filedialogGetSaveFile(parent, file, caption, filter, initialPath)) {
			if (callback) {
				callback(std::move(file));
			}
		} else if (failed) {
			failed();
		}
	});
}

void GetFolder(
		QPointer<QWidget> parent,
		const QString &caption,
		const QString &initialPath,
		Fn<void(QString &&result)> callback,
		Fn<void()> failed) {
	InvokeQueued(QCoreApplication::instance(), [=] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
		Ui::PreventDelayedActivation();
		const auto success = Platform::FileDialog::Get(
			parent,
			files,
			remoteContent,
			caption,
			QString(),
			FileDialog::internal::Type::ReadFolder,
			initialPath);
		if (success && !files.isEmpty() && !files[0].isEmpty()) {
			if (callback) {
				callback(std::move(files[0]));
			}
		} else if (failed) {
			failed();
		}
	});
}

QString AllFilesFilter() {
#ifdef Q_OS_WIN
	return qsl("All files (*.*)");
#else // Q_OS_WIN
	return qsl("All files (*)");
#endif // Q_OS_WIN
}

QString ImagesFilter() {
	return u"Image files (*"_q + Ui::ImageExtensions().join(u" *"_q) + u")"_q;
}

QString AllOrImagesFilter() {
	return AllFilesFilter() + u";;"_q + ImagesFilter();
}

QString ImagesOrAllFilter() {
	return ImagesFilter() + u";;"_q + AllFilesFilter();
}

QString PhotoVideoFilesFilter() {
	return u"Image and Video Files (*.png *.jpg *.jpeg *.mp4 *.mov);;"_q
		+ AllFilesFilter();
}

namespace internal {

void InitLastPathDefault() {
	cSetDialogLastPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
}

bool GetDefault(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		FileDialog::internal::Type type,
		QString startFile = QString()) {
	if (cDialogLastPath().isEmpty()) {
		Platform::FileDialog::InitLastPath();
	}

	remoteContent = QByteArray();
	if (startFile.isEmpty() || startFile.at(0) != '/') {
		startFile = cDialogLastPath() + '/' + startFile;
	}
	QString file;

	const auto resolvedParent = (parent && parent->window()->isVisible())
		? parent->window()
		: Core::App().getFileDialogParent();
	Core::App().notifyFileDialogShown(true);
	if (type == Type::ReadFiles) {
		files = QFileDialog::getOpenFileNames(resolvedParent, caption, startFile, filter);
		QString path = files.isEmpty() ? QString() : QFileInfo(files.back()).absoluteDir().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeSettings();
		}
		return !files.isEmpty();
	} else if (type == Type::ReadFolder) {
		file = QFileDialog::getExistingDirectory(resolvedParent, caption, startFile);
	} else if (type == Type::WriteFile) {
		file = QFileDialog::getSaveFileName(resolvedParent, caption, startFile, filter);
	} else {
		file = QFileDialog::getOpenFileName(resolvedParent, caption, startFile, filter);
	}
	Core::App().notifyFileDialogShown(false);

	if (file.isEmpty()) {
		files = QStringList();
		return false;
	}
	if (type != Type::ReadFolder) {
		// Save last used directory for all queries except directory choosing.
		auto path = QFileInfo(file).absoluteDir().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeSettings();
		}
	}
	files = QStringList(file);
	return true;
}

} // namespace internal
} // namespace FileDialog
