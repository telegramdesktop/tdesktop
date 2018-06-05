/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/file_utilities.h"

#include "mainwindow.h"
#include "storage/localstorage.h"
#include "platform/platform_file_utilities.h"
#include "messenger.h"

bool filedialogGetSaveFile(
		QPointer<QWidget> parent,
		QString &file,
		const QString &caption,
		const QString &filter,
		const QString &initialPath) {
	QStringList files;
	QByteArray remoteContent;
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
		Messenger::Instance().getFileDialogParent(),
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
		const auto date = ParseDateTime(fileTime);
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
		QDir dir(directoryPath);
		QString nameBase = dir.absolutePath() + '/' + base;
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
	QDir dir(path.isEmpty() ? cDialogLastPath() : path);
	int32 extIndex = name.lastIndexOf('.');
	QString prefix = name, extension;
	if (extIndex >= 0) {
		extension = name.mid(extIndex);
		prefix = name.mid(0, extIndex);
	}
	QString nameBase = dir.absolutePath() + '/' + prefix, result = nameBase + extension;
	for (int i = 0; result.toLower() != cur.toLower() && QFileInfo(result).exists(); ++i) {
		result = nameBase + qsl(" (%1)").arg(i + 2) + extension;
	}
	return result;
}

namespace File {

void OpenEmailLink(const QString &email) {
	crl::on_main([=] {
		Platform::File::UnsafeOpenEmailLink(email);
	});
}

void OpenWith(const QString &filepath, QPoint menuPosition) {
	InvokeQueued(QApplication::instance(), [=] {
		if (!Platform::File::UnsafeShowOpenWithDropdown(filepath, menuPosition)) {
			if (!Platform::File::UnsafeShowOpenWith(filepath)) {
				Platform::File::UnsafeLaunch(filepath);
			}
		}
	});
}

void Launch(const QString &filepath) {
	crl::on_main([=] {
		Platform::File::UnsafeLaunch(filepath);
	});
}

void ShowInFolder(const QString &filepath) {
	crl::on_main([=] {
		Platform::File::UnsafeShowInFolder(filepath);
	});
}

namespace internal {

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
	InvokeQueued(QApplication::instance(), [=] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
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
	InvokeQueued(QApplication::instance(), [=] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
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
	InvokeQueued(QApplication::instance(), [=] {
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
	InvokeQueued(QApplication::instance(), [=] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
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
	if (type == Type::ReadFiles) {
		files = QFileDialog::getOpenFileNames(Messenger::Instance().getFileDialogParent(), caption, startFile, filter);
		QString path = files.isEmpty() ? QString() : QFileInfo(files.back()).absoluteDir().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeUserSettings();
		}
		return !files.isEmpty();
    } else if (type == Type::ReadFolder) {
		file = QFileDialog::getExistingDirectory(Messenger::Instance().getFileDialogParent(), caption, startFile);
    } else if (type == Type::WriteFile) {
		file = QFileDialog::getSaveFileName(Messenger::Instance().getFileDialogParent(), caption, startFile, filter);
    } else {
		file = QFileDialog::getOpenFileName(Messenger::Instance().getFileDialogParent(), caption, startFile, filter);
    }
    if (file.isEmpty()) {
        files = QStringList();
        return false;
    }
	if (type != Type::ReadFolder) {
		// Save last used directory for all queries except directory choosing.
		auto path = QFileInfo(file).absoluteDir().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeUserSettings();
		}
	}
	files = QStringList(file);
	return true;
}

} // namespace internal
} // namespace FileDialog
