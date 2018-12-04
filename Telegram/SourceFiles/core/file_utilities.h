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

QString DefaultDownloadPath();

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
	QPointer<QWidget> parent,
	const QString &caption,
	const QString &filter,
	Fn<void(OpenResult &&result)> callback,
	Fn<void()> failed = Fn<void()>());
void GetOpenPaths(
	QPointer<QWidget> parent,
	const QString &caption,
	const QString &filter,
	Fn<void(OpenResult &&result)> callback,
	Fn<void()> failed = Fn<void()>());
void GetWritePath(
	QPointer<QWidget> parent,
	const QString &caption,
	const QString &filter,
	const QString &initialPath,
	Fn<void(QString &&result)> callback,
	Fn<void()> failed = Fn<void()>());
void GetFolder(
	QPointer<QWidget> parent,
	const QString &caption,
	const QString &initialPath,
	Fn<void(QString &&result)> callback,
	Fn<void()> failed = Fn<void()>());

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
	QPointer<QWidget> parent,
	QStringList &files,
	QByteArray &remoteContent,
	const QString &caption,
	const QString &filter,
	::FileDialog::internal::Type type,
	QString startFile);

} // namespace internal
} // namespace FileDialog
