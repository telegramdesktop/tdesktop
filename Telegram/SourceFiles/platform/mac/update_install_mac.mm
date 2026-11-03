/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/update_install_mac.h"

#include "base/basic_types.h"

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
#include "mtproto/dedicated_file_loader.h"
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

#include <QtCore/QDir>
#include <QtCore/QFile>

#include <sys/stat.h>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace Platform {
namespace {

class FileHandle final {
public:
	explicit FileHandle(int fd);
	~FileHandle();

	FileHandle(const FileHandle &) = delete;
	FileHandle &operator=(const FileHandle &) = delete;

	[[nodiscard]] int get() const;
	[[nodiscard]] explicit operator bool() const;

private:
	int _fd = -1;

};

FileHandle::FileHandle(int fd) : _fd(fd) {
}

FileHandle::~FileHandle() {
	if (_fd >= 0) {
		::close(_fd);
	}
}

int FileHandle::get() const {
	return _fd;
}

FileHandle::operator bool() const {
	return _fd >= 0;
}

[[nodiscard]] bool WritableDirectory(const QByteArray &native) {
	struct stat info = {};
	return !stat(native.constData(), &info)
		&& S_ISDIR(info.st_mode)
		&& !access(native.constData(), W_OK | X_OK);
}

} // namespace

UpdateInstallMode GetUpdateInstallMode(
		const QString &executablePath,
		QString &error) {
#ifdef TDESKTOP_DISABLE_AUTOUPDATE
	error = u"Automatic updates are disabled in this build."_q;
	return UpdateInstallMode::Refused;
#else // TDESKTOP_DISABLE_AUTOUPDATE
	const auto contents = QFile::encodeName(executablePath + u"/Contents"_q);
	const auto frameworks = contents + "/Frameworks";
	struct stat info = {};
	const auto target = lstat(frameworks.constData(), &info)
		? contents
		: frameworks;
	if (WritableDirectory(target)) {
		return UpdateInstallMode::Writable;
	}
	error = u"The application bundle is not writable for updating."_q;
	return UpdateInstallMode::Refused;
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

QString ReadyUpdatePackagePath(const QString &workingDirectory) {
	return QDir(workingDirectory).absoluteFilePath(u"tupdates/ready-package"_q);
}

bool ReadyUpdatePackagePresent(const QString &workingDirectory) {
	struct stat info = {};
	return !lstat(
		QFile::encodeName(ReadyUpdatePackagePath(workingDirectory)).constData(),
		&info) || errno != ENOENT;
}

std::optional<QByteArray> ReadUpdatePackage(
		const QString &path,
		QString &error) {
#ifdef TDESKTOP_DISABLE_AUTOUPDATE
	error = u"Automatic updates are disabled in this build."_q;
	return std::nullopt;
#else // TDESKTOP_DISABLE_AUTOUPDATE
	const auto file = FileHandle(open(
		QFile::encodeName(path).constData(),
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
	struct stat info = {};
	if (!file
		|| fstat(file.get(), &info) != 0
		|| !S_ISREG(info.st_mode)
		|| info.st_size <= 0
		|| info.st_size > MTP::AbstractDedicatedLoader::kMaxFileSize) {
		error = u"The package must be a nonempty regular file within the "
			u"update size limit, without a final symlink."_q;
		return std::nullopt;
	}
	auto result = QByteArray(info.st_size, Qt::Uninitialized);
	auto position = qsizetype(0);
	while (position < result.size()) {
		const auto count = read(
			file.get(),
			result.data() + position,
			result.size() - position);
		if (count < 0 && errno == EINTR) {
			continue;
		} else if (count <= 0) {
			error = u"The package could not be read completely."_q;
			return std::nullopt;
		}
		position += count;
	}
	auto extra = char(0);
	auto count = ssize_t(0);
	do {
		count = read(file.get(), &extra, 1);
	} while (count < 0 && errno == EINTR);
	if (count != 0) {
		error = u"The package grew or could not be read to its end."_q;
		return std::nullopt;
	}
	return result;
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

} // namespace Platform
