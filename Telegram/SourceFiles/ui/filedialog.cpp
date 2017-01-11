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
#include "ui/filedialog.h"

#include "application.h"
#include "localstorage.h"
#include "platform/platform_file_dialog.h"

void filedialogInit() {
	if (cDialogLastPath().isEmpty()) {
#ifdef Q_OS_WIN
		// hack to restore previous dir without hurting performance
		QSettings settings(QSettings::UserScope, qstr("QtProject"));
		settings.beginGroup(qstr("Qt"));
		QByteArray sd = settings.value(qstr("filedialog")).toByteArray();
		QDataStream stream(&sd, QIODevice::ReadOnly);
		if (!stream.atEnd()) {
			int version = 3, _QFileDialogMagic = 190;
			QByteArray splitterState;
			QByteArray headerData;
			QList<QUrl> bookmarks;
			QStringList history;
			QString currentDirectory;
			qint32 marker;
			qint32 v;
			qint32 viewMode;
			stream >> marker;
			stream >> v;
			if (marker == _QFileDialogMagic && v == version) {
				stream >> splitterState
						>> bookmarks
						>> history
						>> currentDirectory
						>> headerData
						>> viewMode;
				cSetDialogLastPath(currentDirectory);
			}
		}
		if (cDialogHelperPath().isEmpty()) {
			QDir temppath(cWorkingDir() + "tdata/tdummy/");
			if (!temppath.exists()) {
				temppath.mkpath(temppath.absolutePath());
			}
			if (temppath.exists()) {
				cSetDialogHelperPath(temppath.absolutePath());
			}
		}
#else
		cSetDialogLastPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
#endif
	}
}

namespace FileDialog {
namespace internal {

bool getFiles(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter, FileDialog::internal::Type type, QString startFile = QString()) {
	filedialogInit();

	if (Platform::FileDialog::Supported()) {
		return Platform::FileDialog::Get(files, remoteContent, caption, filter, type, startFile);
	}

#if defined Q_OS_LINUX || defined Q_OS_MAC // use native
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
    } else {
		QString path = QFileInfo(file).absoluteDir().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeUserSettings();
		}
        files = QStringList(file);
        return true;
    }
#endif

	// hack for fast non-native dialog create
	QFileDialog dialog(App::wnd() ? App::wnd()->filedialogParent() : 0, caption, cDialogHelperPathFinal(), filter);

	dialog.setModal(true);
	if (type == Type::ReadFile || type == Type::ReadFiles) {
		dialog.setFileMode((type == Type::ReadFiles) ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
	} else if (type == Type::ReadFolder) { // save dir
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
		dialog.setFileMode(QFileDialog::Directory);
		dialog.setOption(QFileDialog::ShowDirsOnly);
	} else { // save file
		dialog.setFileMode(QFileDialog::AnyFile);
		dialog.setAcceptMode(QFileDialog::AcceptSave);
	}
	dialog.show();

	if (!cDialogLastPath().isEmpty()) dialog.setDirectory(cDialogLastPath());
	if (type == Type::WriteFile) {
		QString toSelect(startFile);
#ifdef Q_OS_WIN
		int32 lastSlash = toSelect.lastIndexOf('/');
		if (lastSlash >= 0) {
			toSelect = toSelect.mid(lastSlash + 1);
		}
		int32 lastBackSlash = toSelect.lastIndexOf('\\');
		if (lastBackSlash >= 0) {
			toSelect = toSelect.mid(lastBackSlash + 1);
		}
#endif
		dialog.selectFile(toSelect);
	}

	int res = dialog.exec();

	QString path = dialog.directory().absolutePath();
	if (path != cDialogLastPath()) {
		cSetDialogLastPath(path);
		Local::writeUserSettings();
	}

	if (res == QDialog::Accepted) {
		if (type == Type::ReadFiles) {
			files = dialog.selectedFiles();
		} else {
			files = dialog.selectedFiles().mid(0, 1);
		}
		if (type == Type::ReadFile || type == Type::ReadFiles) {
#if defined Q_OS_WIN && !defined Q_OS_WINRT
			remoteContent = dialog.selectedRemoteContent();
#endif // Q_OS_WIN && !Q_OS_WINRT
		}
		return true;
	}

	files = QStringList();
	remoteContent = QByteArray();
	return false;
}

} // namespace internal
} // namespace FileDialog

bool filedialogGetOpenFiles(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter) {
	return FileDialog::internal::getFiles(files, remoteContent, caption, filter, FileDialog::internal::Type::ReadFiles);
}

bool filedialogGetOpenFile(QString &file, QByteArray &remoteContent, const QString &caption, const QString &filter) {
	QStringList files;
	bool result = FileDialog::internal::getFiles(files, remoteContent, caption, filter, FileDialog::internal::Type::ReadFile);
	file = files.isEmpty() ? QString() : files.at(0);
	return result;
}

bool filedialogGetSaveFile(QString &file, const QString &caption, const QString &filter, const QString &startName) {
	QStringList files;
	QByteArray remoteContent;
	bool result = FileDialog::internal::getFiles(files, remoteContent, caption, filter, FileDialog::internal::Type::WriteFile, startName);
	file = files.isEmpty() ? QString() : files.at(0);
	return result;
}

bool filedialogGetDir(QString &dir, const QString &caption) {
	QStringList files;
	QByteArray remoteContent;
	bool result = FileDialog::internal::getFiles(files, remoteContent, caption, QString(), FileDialog::internal::Type::ReadFolder);
	dir = files.isEmpty() ? QString() : files.at(0);
	return result;
}

QString filedialogDefaultName(const QString &prefix, const QString &extension, const QString &path, bool skipExistance) {
	filedialogInit();

	time_t t = time(NULL);
	struct tm tm;
    mylocaltime(&tm, &t);

	QChar zero('0');

	QString name;
	QString base = prefix + qsl("_%1-%2-%3_%4-%5-%6").arg(tm.tm_year + 1900).arg(tm.tm_mon + 1, 2, 10, zero).arg(tm.tm_mday, 2, 10, zero).arg(tm.tm_hour, 2, 10, zero).arg(tm.tm_min, 2, 10, zero).arg(tm.tm_sec, 2, 10, zero);
	if (skipExistance) {
		name = base + extension;
	} else {
		QDir dir(path.isEmpty() ? cDialogLastPath() : path);
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
		if (filedialogGetDir(folder, query.caption)) {
			if (!folder.isEmpty()) {
				update.filePaths.push_back(folder);
			}
		}
	} break;
	}

	// No one knows what happened during filedialogGet*() call in the event loop.
	if (!Queries || !Global::started()) return false;

	QueryDone().notify(std_::move(update));
	return true;
}

base::Observable<QueryUpdate> &QueryDone() {
	return QueryDoneObservable;
}

} // namespace FileDialog
