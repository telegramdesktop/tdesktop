/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "core/file_utilities.h"

#include "mainwindow.h"
#include "storage/localstorage.h"
#include "platform/platform_file_utilities.h"
#include "base/task_queue.h"
#include "messenger.h"

bool filedialogGetSaveFile(QString &file, const QString &caption, const QString &filter, const QString &initialPath) {
	QStringList files;
	QByteArray remoteContent;
	bool result = Platform::FileDialog::Get(files, remoteContent, caption, filter, FileDialog::internal::Type::WriteFile, initialPath);
	file = files.isEmpty() ? QString() : files.at(0);
	return result;
}

QString filedialogDefaultName(const QString &prefix, const QString &extension, const QString &path, bool skipExistance, int fileTime) {
	auto directoryPath = path;
	if (directoryPath.isEmpty()) {
		if (cDialogLastPath().isEmpty()) {
			Platform::FileDialog::InitLastPath();
		}
		directoryPath = cDialogLastPath();
	}

	QString base;
	if (fileTime) {
		base = prefix + ::date(fileTime).toString("_yyyy-MM-dd_HH-mm-ss");
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

QString filedialogNextFilename(const QString &name, const QString &cur, const QString &path) {
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
	base::TaskQueue::Main().Put([email] {
		Platform::File::UnsafeOpenEmailLink(email);
	});
}

void OpenWith(const QString &filepath, QPoint menuPosition) {
	base::TaskQueue::Main().Put([filepath, menuPosition] {
		if (!Platform::File::UnsafeShowOpenWithDropdown(filepath, menuPosition)) {
			if (!Platform::File::UnsafeShowOpenWith(filepath)) {
				Platform::File::UnsafeLaunch(filepath);
			}
		}
	});
}

void Launch(const QString &filepath) {
	base::TaskQueue::Main().Put([filepath] {
		Platform::File::UnsafeLaunch(filepath);
	});
}

void ShowInFolder(const QString &filepath) {
	base::TaskQueue::Main().Put([filepath] {
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

void GetOpenPath(const QString &caption, const QString &filter, base::lambda<void(const OpenResult &result)> callback, base::lambda<void()> failed) {
	base::TaskQueue::Main().Put([caption, filter, callback = std::move(callback), failed = std::move(failed)] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
		if (Platform::FileDialog::Get(files, remoteContent, caption, filter, FileDialog::internal::Type::ReadFile)
			&& ((!files.isEmpty() && !files[0].isEmpty()) || !remoteContent.isEmpty())) {
			if (callback) {
				auto result = OpenResult();
				if (!files.isEmpty() && !files[0].isEmpty()) {
					result.paths.push_back(files[0]);
				}
				result.remoteContent = remoteContent;
				callback(result);
			}
		} else if (failed) {
			failed();
		}
	});
}

void GetOpenPaths(const QString &caption, const QString &filter, base::lambda<void(const OpenResult &result)> callback, base::lambda<void()> failed) {
	base::TaskQueue::Main().Put([caption, filter, callback = std::move(callback), failed = std::move(failed)] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
		if (Platform::FileDialog::Get(files, remoteContent, caption, filter, FileDialog::internal::Type::ReadFiles)
			&& (!files.isEmpty() || !remoteContent.isEmpty())) {
			if (callback) {
				auto result = OpenResult();
				result.paths = files;
				result.remoteContent = remoteContent;
				callback(result);
			}
		} else if (failed) {
			failed();
		}
	});
}

void GetWritePath(const QString &caption, const QString &filter, const QString &initialPath, base::lambda<void(const QString &result)> callback, base::lambda<void()> failed) {
	base::TaskQueue::Main().Put([caption, filter, initialPath, callback = std::move(callback), failed = std::move(failed)] {
		auto file = QString();
		if (filedialogGetSaveFile(file, caption, filter, initialPath)) {
			if (callback) {
				callback(file);
			}
		} else if (failed) {
			failed();
		}
	});
}

void GetFolder(const QString &caption, const QString &initialPath, base::lambda<void(const QString &result)> callback, base::lambda<void()> failed) {
	base::TaskQueue::Main().Put([caption, initialPath, callback = std::move(callback), failed = std::move(failed)] {
		auto files = QStringList();
		auto remoteContent = QByteArray();
		if (Platform::FileDialog::Get(files, remoteContent, caption, QString(), FileDialog::internal::Type::ReadFolder, initialPath)
			&& !files.isEmpty() && !files[0].isEmpty()) {
			if (callback) {
				callback(files[0]);
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

bool GetDefault(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter, FileDialog::internal::Type type, QString startFile = QString()) {
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
