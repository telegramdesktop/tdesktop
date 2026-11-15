/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif // Q_OS_UNIX

namespace Core {

[[nodiscard]] inline bool IsOwnTempDirectory(const QString &path) {
	if (path.isEmpty()) {
		return false;
	}
#ifdef Q_OS_UNIX
	struct stat data = {};
	return lstat(QFile::encodeName(path).constData(), &data) == 0
		&& S_ISDIR(data.st_mode)
		&& data.st_uid == geteuid();
#else
	return QFileInfo(path).isDir();
#endif // Q_OS_UNIX
}

[[nodiscard]] inline QString PreparePrivateTempDirectory(const QString &path) {
	const auto usable = [](const QString &dir) {
		return IsOwnTempDirectory(dir)
			&& QFile::setPermissions(
				dir,
				QFileDevice::ReadUser
					| QFileDevice::WriteUser
					| QFileDevice::ExeUser);
	};
#ifdef Q_OS_UNIX
	// Creation is best-effort; the no-follow owner check below is authoritative.
	::mkdir(QFile::encodeName(path).constData(), 0700);
#else
	QDir().mkpath(path);
#endif // Q_OS_UNIX
	if (usable(path)) {
		return path;
	}
	// Never destroyed: a late producer or upload reader may outlive exit.
	static const auto fallback = new QTemporaryDir();
	return (fallback->isValid() && usable(fallback->path()))
		? fallback->path()
		: QString();
}

} // namespace Core
