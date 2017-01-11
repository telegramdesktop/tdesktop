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
#pragma once

#include "core/observer.h"

void filedialogInit();
bool filedialogGetOpenFiles(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter);
bool filedialogGetOpenFile(QString &file, QByteArray &remoteContent, const QString &caption, const QString &filter);
bool filedialogGetSaveFile(QString &file, const QString &caption, const QString &filter, const QString &startName);
bool filedialogGetDir(QString &dir, const QString &caption);

QString filedialogDefaultName(const QString &prefix, const QString &extension, const QString &path = QString(), bool skipExistance = false);
QString filedialogNextFilename(const QString &name, const QString &cur, const QString &path = QString());

QString filedialogAllFilesFilter();

namespace FileDialog {
namespace internal {

enum class Type {
	ReadFile,
	ReadFiles,
	ReadFolder,
	WriteFile,
};

} // namespace internal

using QueryId = uint64;
struct QueryUpdate {
	QueryUpdate(QueryId id) : queryId(id) {
	}
	QueryId queryId;
	QStringList filePaths;
	QByteArray remoteContent;
};

QueryId queryReadFile(const QString &caption, const QString &filter);
QueryId queryReadFiles(const QString &caption, const QString &filter);
QueryId queryWriteFile(const QString &caption, const QString &filter, const QString &filePath);
QueryId queryReadFolder(const QString &caption);

// Returns false if no need to call it anymore right now.
// NB! This function enters an event loop.
bool processQuery();

base::Observable<QueryUpdate> &QueryDone();

} // namespace FileDialog
