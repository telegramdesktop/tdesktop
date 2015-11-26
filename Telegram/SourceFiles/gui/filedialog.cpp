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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "gui/filedialog.h"

#include "application.h"
#include "localstorage.h"

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

// multipleFiles: 1 - multi open, 0 - single open, -1 - single save, -2 - select dir
bool _filedialogGetFiles(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter, int multipleFiles, QString startFile = QString()) {

	filedialogInit();

#if defined Q_OS_LINUX || defined Q_OS_MAC // use native
    remoteContent = QByteArray();
	if (startFile.isEmpty() || startFile.at(0) != '/') {
		startFile = cDialogLastPath() + '/' + startFile;
	}
    QString file;
    if (multipleFiles >= 0) {
		files = QFileDialog::getOpenFileNames(App::wnd() ? App::wnd()->filedialogParent() : 0, caption, startFile, filter);
		QString path = files.isEmpty() ? QString() : QFileInfo(files.back()).absoluteDir().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeUserSettings();
		}
        return !files.isEmpty();
    } else if (multipleFiles < -1) {
		file = QFileDialog::getExistingDirectory(App::wnd() ? App::wnd()->filedialogParent() : 0, caption, startFile);
    } else if (multipleFiles < 0) {
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
	if (multipleFiles >= 0) { // open file or files
		dialog.setFileMode(multipleFiles ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
	} else if (multipleFiles < -1) { // save dir
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
		dialog.setFileMode(QFileDialog::Directory);
		dialog.setOption(QFileDialog::ShowDirsOnly);
	} else { // save file
		dialog.setFileMode(QFileDialog::AnyFile);
		dialog.setAcceptMode(QFileDialog::AcceptSave);
	}
	dialog.show();

	if (!cDialogLastPath().isEmpty()) dialog.setDirectory(cDialogLastPath());
	if (multipleFiles == -1) {
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
		if (multipleFiles > 0) {
			files = dialog.selectedFiles();
		} else {
			files = dialog.selectedFiles().mid(0, 1);
		}
		if (multipleFiles >= 0) {
#ifdef Q_OS_WIN
			remoteContent = dialog.selectedRemoteContent();
#endif
		}
		return true;
	}

	files = QStringList();
	remoteContent = QByteArray();
	return false;
}

bool filedialogGetOpenFiles(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter) {
	return _filedialogGetFiles(files, remoteContent, caption, filter, 1);
}

bool filedialogGetOpenFile(QString &file, QByteArray &remoteContent, const QString &caption, const QString &filter) {
	QStringList files;
	bool result = _filedialogGetFiles(files, remoteContent, caption, filter, 0);
	file = files.isEmpty() ? QString() : files.at(0);
	return result;
}

bool filedialogGetSaveFile(QString &file, const QString &caption, const QString &filter, const QString &startName) {
	QStringList files;
	QByteArray remoteContent;
	bool result = _filedialogGetFiles(files, remoteContent, caption, filter, -1, startName);
	file = files.isEmpty() ? QString() : files.at(0);
	return result;
}

bool filedialogGetDir(QString &dir, const QString &caption) {
	QStringList files;
	QByteArray remoteContent;
	bool result = _filedialogGetFiles(files, remoteContent, caption, QString(), -2);
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
