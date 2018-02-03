/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/observer.h"

// legacy
bool filedialogGetSaveFile(
	QString &file,
	const QString &caption,
	const QString &filter,
	const QString &initialPath);

QString filedialogDefaultName(
	const QString &prefix,
	const QString &extension,
	const QString &path = QString(),
	bool skipExistance = false,
	TimeId fileTime = TimeId(0));
QString filedialogNextFilename(
	const QString &name,
	const QString &cur,
	const QString &path = QString());

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

struct OpenResult {
	QStringList paths;
	QByteArray remoteContent;
};
void GetOpenPath(
	const QString &caption,
	const QString &filter,
	base::lambda<void(OpenResult &&result)> callback,
	base::lambda<void()> failed = base::lambda<void()>());
void GetOpenPaths(
	const QString &caption,
	const QString &filter,
	base::lambda<void(OpenResult &&result)> callback,
	base::lambda<void()> failed = base::lambda<void()>());
void GetWritePath(
	const QString &caption,
	const QString &filter,
	const QString &initialPath,
	base::lambda<void(QString &&result)> callback,
	base::lambda<void()> failed = base::lambda<void()>());
void GetFolder(
	const QString &caption,
	const QString &initialPath,
	base::lambda<void(QString &&result)> callback,
	base::lambda<void()> failed = base::lambda<void()>());

QString AllFilesFilter();

namespace internal {

enum class Type {
	ReadFile,
	ReadFiles,
	ReadFolder,
	WriteFile,
};

void InitLastPathDefault();

bool GetDefault(
	QStringList &files,
	QByteArray &remoteContent,
	const QString &caption,
	const QString &filter,
	::FileDialog::internal::Type type,
	QString startFile);

} // namespace internal
} // namespace FileDialog
