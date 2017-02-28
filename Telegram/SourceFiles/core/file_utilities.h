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

// legacy
bool filedialogGetOpenFiles(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter);
bool filedialogGetOpenFile(QString &file, QByteArray &remoteContent, const QString &caption, const QString &filter);
bool filedialogGetSaveFile(QString &file, const QString &caption, const QString &filter, const QString &initialPath);
bool filedialogGetDir(QString &dir, const QString &caption, const QString &initialPath);

QString filedialogDefaultName(const QString &prefix, const QString &extension, const QString &path = QString(), bool skipExistance = false);
QString filedialogNextFilename(const QString &name, const QString &cur, const QString &path = QString());

QString filedialogAllFilesFilter();

namespace File {

// Those functions are async wrappers to Platform::File::Unsafe* calls.
void OpenEmailLink(const QString &email);
void OpenWith(const QString &filepath, QPoint menuPosition);
void Launch(const QString &filepath);
void ShowInFolder(const QString &filepath);

namespace internal {

inline QString UrlToLocalDefault(const QUrl &url) {
	return url.toLocalFile();
}

void UnsafeOpenEmailLinkDefault(const QString &email);
void UnsafeLaunchDefault(const QString &filepath);

} // namespace internal
} // namespace File

namespace FileDialog {
namespace internal {

enum class Type {
	ReadFile,
	ReadFiles,
	ReadFolder,
	WriteFile,
};

void InitLastPathDefault();

bool GetDefault(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter, ::FileDialog::internal::Type type, QString startFile);

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

struct OpenResult {
	QStringList paths;
	QByteArray remoteContent;
};
void GetOpenPath(const QString &caption, const QString &filter, base::lambda<void(const OpenResult &result)> callback, base::lambda<void()> failed = base::lambda<void()>());
void GetOpenPaths(const QString &caption, const QString &filter, base::lambda<void(const OpenResult &result)> callback, base::lambda<void()> failed = base::lambda<void()>());
void GetWritePath(const QString &caption, const QString &filter, const QString &initialPath, base::lambda<void(const QString &result)> callback, base::lambda<void()> failed = base::lambda<void()>());
void GetFolder(const QString &caption, const QString &initialPath, base::lambda<void(const QString &result)> callback, base::lambda<void()> failed = base::lambda<void()>());

} // namespace FileDialog
