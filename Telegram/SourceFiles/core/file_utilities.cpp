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
#include "stdafx.h"
#include "core/file_utilities.h"

#include "mainwindow.h"
#include "localstorage.h"
#include "platform/platform_file_utilities.h"
#include "core/task_queue.h"

namespace FileDialog {
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
		files = QFileDialog::getOpenFileNames(App::wnd() ? App::wnd()->filedialogParent() : 0, caption, startFile, filter);
		QString path = files.isEmpty() ? QString() : QFileInfo(files.back()).absoluteDir().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeUserSettings();
		}
		return !files.isEmpty();
    } else if (type == Type::ReadFolder) {
		file = QFileDialog::getExistingDirectory(App::wnd() ? App::wnd()->filedialogParent() : 0, caption, startFile);
    } else if (type == Type::WriteFile) {
		file = QFileDialog::getSaveFileName(App::wnd() ? App::wnd()->filedialogParent() : 0, caption, startFile, filter);
    } else {
		file = QFileDialog::getOpenFileName(App::wnd() ? App::wnd()->filedialogParent() : 0, caption, startFile, filter);
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

bool filedialogGetOpenFiles(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter) {
	return Platform::FileDialog::Get(files, remoteContent, caption, filter, FileDialog::internal::Type::ReadFiles);
}

bool filedialogGetOpenFile(QString &file, QByteArray &remoteContent, const QString &caption, const QString &filter) {
	QStringList files;
	bool result = Platform::FileDialog::Get(files, remoteContent, caption, filter, FileDialog::internal::Type::ReadFile);
	file = files.isEmpty() ? QString() : files.at(0);
	return result;
}

bool filedialogGetSaveFile(QString &file, const QString &caption, const QString &filter, const QString &initialPath) {
	QStringList files;
	QByteArray remoteContent;
	bool result = Platform::FileDialog::Get(files, remoteContent, caption, filter, FileDialog::internal::Type::WriteFile, initialPath);
	file = files.isEmpty() ? QString() : files.at(0);
	return result;
}

bool filedialogGetDir(QString &dir, const QString &caption, const QString &initialPath) {
	QStringList files;
	QByteArray remoteContent;
	bool result = Platform::FileDialog::Get(files, remoteContent, caption, QString(), FileDialog::internal::Type::ReadFolder, initialPath);
	dir = files.isEmpty() ? QString() : files.at(0);
	return result;
}

QString filedialogDefaultName(const QString &prefix, const QString &extension, const QString &path, bool skipExistance) {
	auto directoryPath = path;
	if (directoryPath.isEmpty()) {
		if (cDialogLastPath().isEmpty()) {
			Platform::FileDialog::InitLastPath();
		}
		directoryPath = cDialogLastPath();
	}

	time_t t = time(NULL);
	struct tm tm;
    mylocaltime(&tm, &t);

	QChar zero('0');

	QString name;
	QString base = prefix + qsl("_%1-%2-%3_%4-%5-%6").arg(tm.tm_year + 1900).arg(tm.tm_mon + 1, 2, 10, zero).arg(tm.tm_mday, 2, 10, zero).arg(tm.tm_hour, 2, 10, zero).arg(tm.tm_min, 2, 10, zero).arg(tm.tm_sec, 2, 10, zero);
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

QString filedialogAllFilesFilter() {
#ifdef Q_OS_WIN
	return qsl("All files (*.*)");
#else // Q_OS_WIN
	return qsl("All files (*)");
#endif // Q_OS_WIN
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
namespace {

base::Observable<QueryUpdate> QueryDoneObservable;

struct Query {
	enum class Type {
		ReadFile,
		ReadFiles,
		WriteFile,
		ReadFolder,
	};
	Query(Type type
		, const QString &caption = QString()
		, const QString &filter = QString()
		, const QString &filePath = QString()) : id(rand_value<QueryId>())
		, type(type)
		, caption(caption)
		, filter(filter)
		, filePath(filePath) {
	}
	QueryId id;
	Type type;
	QString caption, filter, filePath;
};

using QueryList = QList<Query>;
NeverFreedPointer<QueryList> Queries;

void StartCallback() {
	Queries.createIfNull();
}

} // namespace

QueryId queryReadFile(const QString &caption, const QString &filter) {
	Queries.createIfNull();

	Queries->push_back(Query(Query::Type::ReadFile, caption, filter));
	Global::RefHandleFileDialogQueue().call();
	return Queries->back().id;
}

QueryId queryReadFiles(const QString &caption, const QString &filter) {
	Queries.createIfNull();

	Queries->push_back(Query(Query::Type::ReadFiles, caption, filter));
	Global::RefHandleFileDialogQueue().call();
	return Queries->back().id;
}

QueryId queryWriteFile(const QString &caption, const QString &filter, const QString &filePath) {
	Queries.createIfNull();

	Queries->push_back(Query(Query::Type::WriteFile, caption, filter, filePath));
	Global::RefHandleFileDialogQueue().call();
	return Queries->back().id;
}

QueryId queryReadFolder(const QString &caption) {
	Queries.createIfNull();

	Queries->push_back(Query(Query::Type::ReadFolder, caption));
	Global::RefHandleFileDialogQueue().call();
	return Queries->back().id;
}

bool processQuery() {
	if (!Queries || !Global::started() || Queries->isEmpty()) return false;

	auto query = Queries->front();
	Queries->pop_front();

	QueryUpdate update(query.id);

	switch (query.type) {
	case Query::Type::ReadFile: {
		QString file;
		QByteArray remoteContent;
		if (filedialogGetOpenFile(file, remoteContent, query.caption, query.filter)) {
			if (!file.isEmpty()) {
				update.filePaths.push_back(file);
			}
			update.remoteContent = remoteContent;
		}
	} break;

	case Query::Type::ReadFiles: {
		QStringList files;
		QByteArray remoteContent;
		if (filedialogGetOpenFiles(files, remoteContent, query.caption, query.filter)) {
			update.filePaths = files;
			update.remoteContent = remoteContent;
		}
	} break;

	case Query::Type::WriteFile: {
		QString file;
		if (filedialogGetSaveFile(file, query.caption, query.filter, query.filePath)) {
			if (!file.isEmpty()) {
				update.filePaths.push_back(file);
			}
		}
	} break;

	case Query::Type::ReadFolder: {
		QString folder;
		if (filedialogGetDir(folder, query.caption, query.filePath)) {
			if (!folder.isEmpty()) {
				update.filePaths.push_back(folder);
			}
		}
	} break;
	}

	// No one knows what happened during filedialogGet*() call in the event loop.
	if (!Queries || !Global::started()) return false;

	QueryDone().notify(std::move(update));
	return true;
}

base::Observable<QueryUpdate> &QueryDone() {
	return QueryDoneObservable;
}

void GetOpenPath(const QString &caption, const QString &filter, base::lambda<void(const OpenResult &result)> callback, base::lambda<void()> failed) {
	base::TaskQueue::Main().Put([caption, filter, callback = std::move(callback), failed = std::move(failed)] {
		auto file = QString();
		auto remoteContent = QByteArray();
		if (filedialogGetOpenFile(file, remoteContent, caption, filter) && (!file.isEmpty() || !remoteContent.isEmpty())) {
			if (callback) {
				auto result = OpenResult();
				if (!file.isEmpty()) {
					result.paths.push_back(file);
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
		if (filedialogGetOpenFiles(files, remoteContent, caption, filter) && (!files.isEmpty() || !remoteContent.isEmpty())) {
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
		auto folder = QString();
		if (filedialogGetDir(folder, caption, initialPath) && !folder.isEmpty()) {
			if (callback) {
				callback(folder);
			}
		} else if (failed) {
			failed();
		}
	});
}

} // namespace FileDialog
