/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/update_install_linux.h"

#include "base/platform/base_platform_info.h"
#include "base/basic_types.h"
#include "core/update_channel.h"
#include "core/update_keys.h"
#include "core/update_unpack.h"
#include "core/update_verify.h"

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
#include "mtproto/dedicated_file_loader.h"
#include <glib/glib.hpp>
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSet>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include <utility>
#include <vector>

namespace Platform {
namespace {

[[nodiscard]] int ReportError(const QString &error) {
	fprintf(stderr, "Update install: %s\n", error.toUtf8().constData());
	fflush(stderr);
	return 1;
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

constexpr auto kDirectoryFlags
	= O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
constexpr auto kUpdaterPathLimit = 1024;
constexpr auto kCopyBufferSize = 64 * 1024;
constexpr auto kMaxDiagnosticBytes = 1024 * 1024;
constexpr auto kMaxDiagnosticFiles = 64;

struct DirectoryCloser {
	void operator()(DIR *stream) const {
		closedir(stream);
	}
};

using DirectoryStream = std::unique_ptr<DIR, DirectoryCloser>;

class FileDescriptor final {
public:
	FileDescriptor() = default;
	explicit FileDescriptor(int value);
	FileDescriptor(FileDescriptor &&other) noexcept;
	FileDescriptor &operator=(FileDescriptor &&other) noexcept;
	~FileDescriptor();

	[[nodiscard]] bool close();
	[[nodiscard]] int get() const;
	[[nodiscard]] explicit operator bool() const;

private:
	int _value = -1;

};

class PrivateDirectory final {
public:
	PrivateDirectory() = default;
	PrivateDirectory(const PrivateDirectory &) = delete;
	PrivateDirectory &operator=(const PrivateDirectory &) = delete;
	~PrivateDirectory();

	[[nodiscard]] bool create(QString &error);
	[[nodiscard]] bool cleanup();
	[[nodiscard]] const QString &path() const;

private:
	QString _path;

};

struct Installation {
	QByteArray directoryPath;
	QByteArray executableName;
	FileDescriptor directory;
	FileDescriptor executable;
};

struct ExpectedFile {
	QByteArray path;
	QByteArray digest;
	off_t size = 0;
	mode_t mode = 0;
};

FileDescriptor::FileDescriptor(int value) : _value(value) {
}

FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept
: _value(std::exchange(other._value, -1)) {
}

FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept {
	if (this != &other) {
		if (_value >= 0) {
			::close(_value);
		}
		_value = std::exchange(other._value, -1);
	}
	return *this;
}

FileDescriptor::~FileDescriptor() {
	if (_value >= 0) {
		::close(_value);
	}
}

bool FileDescriptor::close() {
	return _value < 0 || ::close(std::exchange(_value, -1)) == 0;
}

int FileDescriptor::get() const {
	return _value;
}

FileDescriptor::operator bool() const {
	return _value >= 0;
}

[[nodiscard]] bool Fail(QString &error, const char *message) {
	error = QString::fromLatin1(message);
	return false;
}

void ReportProgress(const char *message) {
	fprintf(stdout, "Update install: %s\n", message);
	fflush(stdout);
}

[[nodiscard]] bool TrustedDirectory(const struct stat &info) {
	return S_ISDIR(info.st_mode)
		&& info.st_uid == 0
		&& !(info.st_mode & (S_IWGRP | S_IWOTH));
}

[[nodiscard]] bool TrustedExecutable(const struct stat &info) {
	return S_ISREG(info.st_mode)
		&& info.st_uid == 0
		&& info.st_nlink > 0
		&& (info.st_mode & S_IXUSR)
		&& !(info.st_mode & (S_IWGRP | S_IWOTH));
}

[[nodiscard]] FileDescriptor OpenTrustedDirectory(
		int parent,
		const QByteArray &name,
		QString &error) {
	auto result = FileDescriptor(openat(
		parent,
		name.constData(),
		kDirectoryFlags));
	struct stat info = {};
	if (!result
		|| fstat(result.get(), &info) != 0
		|| !TrustedDirectory(info)) {
		error = u"Installation ancestors must be root-owned, unwritable "
			u"by other users, and free of symlinks."_q;
		return FileDescriptor();
	}
	return result;
}

[[nodiscard]] bool CurrentExecutableMatches(
		const Installation &installation,
		QString &error) {
	struct stat running = {};
	struct stat entry = {};
	if (fstat(installation.executable.get(), &running) != 0
		|| fstatat(
			installation.directory.get(),
			installation.executableName.constData(),
			&entry,
			AT_SYMLINK_NOFOLLOW) != 0
		|| !TrustedExecutable(running)
		|| !TrustedExecutable(entry)
		|| running.st_dev != entry.st_dev
		|| running.st_ino != entry.st_ino) {
		return Fail(error, "The installed executable is unsafe or was replaced.");
	}
	return true;
}

[[nodiscard]] bool SafePathComponent(const QByteArray &name) {
	if (name.isEmpty() || name == "." || name == "..") {
		return false;
	}
	for (const auto ch : name) {
		if (uchar(ch) < 32 || uchar(ch) == 127 || ch == '\\') {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QByteArray AsciiLower(QByteArray value) {
	for (auto &ch : value) {
		if (ch >= 'A' && ch <= 'Z') {
			ch += 'a' - 'A';
		}
	}
	return value;
}

[[nodiscard]] bool SafeExecutableName(const QByteArray &name) {
	const auto lower = AsciiLower(name);
	const auto reserved = { "updater", "ready", "tdata" };
	for (const auto word : reserved) {
		if (lower == word) {
			return false;
		}
	}
	return SafePathComponent(name);
}

[[nodiscard]] std::optional<Installation> OpenInstallation(QString &error) {
	auto pathBuffer = std::array<char, kUpdaterPathLimit>();
	const auto length = readlink(
		"/proc/self/exe",
		pathBuffer.data(),
		pathBuffer.size());
	if (length <= 0 || size_t(length) >= pathBuffer.size()) {
		error = u"Could not obtain a complete /proc/self/exe path."_q;
		return std::nullopt;
	}
	const auto path = QByteArray(pathBuffer.data(), length);
	if (!path.startsWith('/') || path.endsWith(" (deleted)")) {
		error = u"The executable path is unavailable or deleted."_q;
		return std::nullopt;
	}
	const auto slash = path.lastIndexOf('/');
	auto result = Installation{
		.directoryPath = path.left(slash + 1),
		.executableName = path.mid(slash + 1),
	};
	if (!SafeExecutableName(result.executableName)
		|| result.directoryPath.size() + sizeof("Updater") > kUpdaterPathLimit) {
		error = u"The installation path is unsafe for the update helper."_q;
		return std::nullopt;
	}
	result.executable = FileDescriptor(open(
		"/proc/self/exe",
		O_RDONLY | O_CLOEXEC | O_NONBLOCK));
	struct stat executable = {};
	if (!result.executable
		|| fstat(result.executable.get(), &executable) != 0
		|| !TrustedExecutable(executable)) {
		error = u"The running executable must be root-owned and protected."_q;
		return std::nullopt;
	}
	result.directory = OpenTrustedDirectory(AT_FDCWD, "/", error);
	if (!result.directory) {
		return std::nullopt;
	}
	if (slash > 0) {
		const auto components = path.mid(1, slash - 1).split('/');
		for (const auto &part : components) {
			if (!SafePathComponent(part)) {
				error = u"The installation path contains an unsafe component."_q;
				return std::nullopt;
			}
			result.directory = OpenTrustedDirectory(
				result.directory.get(),
				part,
				error);
			if (!result.directory) {
				return std::nullopt;
			}
		}
	}
	if (flock(result.directory.get(), LOCK_EX | LOCK_NB) != 0) {
		error = u"Could not lock the installation; another update may be running."_q;
		return std::nullopt;
	}
	if (!CurrentExecutableMatches(result, error)) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<QByteArray> ReadPackage(
		const QString &path,
		QString &error) {
	const auto nativePath = QFile::encodeName(path);
	if (QFile::decodeName(nativePath) != path || nativePath.contains('\0')) {
		error = u"The package path cannot be represented without loss."_q;
		return std::nullopt;
	}
	const auto file = FileDescriptor(open(
		nativePath.constData(),
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
}

[[nodiscard]] std::optional<Core::Updates::VerifiedUpdate> VerifyPackage(
		const QByteArray &bytes,
		bool beta,
		QString &error) {
	const auto root = Core::Updates::RootPublicKeyPem();
	const auto held = Core::Updates::ParseVerifiedManifest(
		Core::Updates::EmbeddedManifest(),
		Core::Updates::EmbeddedManifestSignature(),
		root,
		&error);
	if (!held) {
		return std::nullopt;
	}
	const auto target = Core::Updates::TargetFromPlatformKey(
		AutoUpdateKey().toLatin1());
	if (!target) {
		error = u"This platform has no supported update target."_q;
		return std::nullopt;
	}
	return Core::Updates::VerifyUpdate(
		bytes,
		Core::BuildUpdateChannel,
		AppBetaVersion || beta,
		*target,
		Core::RunningUpdateVersion(),
		held,
		root,
		QDateTime::currentSecsSinceEpoch(),
		&error);
}

PrivateDirectory::~PrivateDirectory() {
	if (!cleanup()) {
		fprintf(
			stderr,
			"Update install: could not remove private staging at %s.\n",
			_path.toUtf8().constData());
		fflush(stderr);
	}
}

bool PrivateDirectory::create(QString &error) {
	const auto root = OpenTrustedDirectory(AT_FDCWD, "/", error);
	if (!root) {
		return false;
	}
	const auto parent = FileDescriptor(openat(root.get(), "tmp", kDirectoryFlags));
	struct stat info = {};
	if (!parent
		|| fstat(parent.get(), &info) != 0
		|| !S_ISDIR(info.st_mode)
		|| info.st_uid != 0
		|| ((info.st_mode & (S_IWGRP | S_IWOTH))
			&& !(info.st_mode & S_ISVTX))) {
		return Fail(error, "The fixed /tmp parent is not safely root-owned.");
	}
	char pattern[] = "/tmp/telegram-update-XXXXXX";
	const auto created = mkdtemp(pattern);
	if (!created) {
		return Fail(error, "Could not create private update staging.");
	}
	_path = QString::fromLatin1(created);
	const auto directory = FileDescriptor(open(created, kDirectoryFlags));
	if (!directory
		|| fstat(directory.get(), &info) != 0
		|| !S_ISDIR(info.st_mode)
		|| info.st_uid != 0
		|| (info.st_mode & 07777) != 0700) {
		return Fail(error, "Private update staging is not root-owned mode 0700.");
	}
	const auto rootDir = QDir(_path);
	if (!rootDir.mkdir(u"tupdates"_q)
		|| !rootDir.mkdir(u"tupdates/temp"_q)
		|| !rootDir.mkdir(u"DebugLogs"_q)) {
		return Fail(error, "Could not create private update directories.");
	}
	return true;
}

bool PrivateDirectory::cleanup() {
	if (_path.isEmpty()) {
		return true;
	}
	if (!QDir(_path).removeRecursively()) {
		return false;
	}
	struct stat info = {};
	if (lstat(QFile::encodeName(_path).constData(), &info) == 0
		|| errno != ENOENT) {
		return false;
	}
	_path.clear();
	return true;
}

const QString &PrivateDirectory::path() const {
	return _path;
}

[[nodiscard]] bool WriteReady(const QString &directory, QString &error) {
	auto file = QFile(directory + u"/ready"_q);
	if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
		return Fail(error, "Could not create the reserved update ready marker.");
	}
	if (file.write("1", 1) != 1 || !file.flush()) {
		return Fail(error, "Could not write the private update ready marker.");
	}
	file.close();
	if (file.error() != QFileDevice::NoError) {
		return Fail(error, "Could not close the private update ready marker.");
	}
	return true;
}

[[nodiscard]] DirectoryStream OpenDirectoryStream(int directory, QString &error) {
	const auto copy = openat(directory, ".", kDirectoryFlags);
	auto stream = static_cast<DIR*>(nullptr);
	const auto guard = gsl::finally([&] {
		if (copy >= 0 && !stream) {
			close(copy);
		}
	});
	if (copy >= 0) {
		stream = fdopendir(copy);
	}
	if (!stream) {
		error = u"Could not enumerate a protected update directory."_q;
	}
	return DirectoryStream(stream);
}

[[nodiscard]] bool TrustedFile(const struct stat &info) {
	return S_ISREG(info.st_mode)
		&& info.st_uid == 0
		&& !(info.st_mode & (S_IWGRP | S_IWOTH));
}

[[nodiscard]] QByteArray FileDigest(int file, off_t size, QString &error) {
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	auto buffer = std::array<char, kCopyBufferSize>();
	while (size > 0) {
		const auto count = read(
			file,
			buffer.data(),
			std::min<off_t>(size, buffer.size()));
		if (count < 0 && errno == EINTR) {
			continue;
		} else if (count <= 0) {
			error = u"Could not read a complete installed or staged file."_q;
			return QByteArray();
		}
		hash.addData(QByteArrayView(buffer.data(), count));
		size -= count;
	}
	auto extra = char(0);
	auto count = ssize_t(0);
	do {
		count = read(file, &extra, 1);
	} while (count < 0 && errno == EINTR);
	if (count != 0) {
		error = u"An installed or staged file changed while being read."_q;
		return QByteArray();
	}
	return hash.result();
}

[[nodiscard]] bool InventoryDirectory(
		int directory,
		const QByteArray &prefix,
		const QByteArray &executableName,
		std::vector<ExpectedFile> &files,
		QSet<QByteArray> &destinations,
		QString &error) {
	const auto stream = OpenDirectoryStream(directory, error);
	if (!stream) {
		return false;
	}
	while (true) {
		errno = 0;
		const auto entry = readdir(stream.get());
		if (!entry) {
			return !errno || Fail(error, "Could not finish staged inventory.");
		}
		const auto name = QByteArray(entry->d_name);
		if (name == "." || name == "..") {
			continue;
		} else if (!SafePathComponent(name)) {
			return Fail(error, "Unsafe staged filename.");
		}
		const auto relative = prefix + name;
		if (prefix.isEmpty()) {
			const auto reserved = { "Telegram", "Updater", "ready", "tdata" };
			for (const auto word : reserved) {
				if (AsciiLower(name) == AsciiLower(word) && name != word) {
					return Fail(error, "A staged name aliases a reserved filename.");
				}
			}
		}
		struct stat info = {};
		if (fstatat(directory, name.constData(), &info, AT_SYMLINK_NOFOLLOW)) {
			return Fail(error, "Could not inspect a staged entry.");
		}
		const auto skipped = relative == "ready"
			|| relative == "tdata"
			|| relative.startsWith("tdata/");
		const auto target = (relative == "Telegram") ? executableName : relative;
		if (!skipped) {
			const auto key = AsciiLower(target);
			if (destinations.contains(key)) {
				return Fail(error, "Staged entries map to conflicting destinations.");
			}
			destinations.insert(key);
		}
		if (S_ISDIR(info.st_mode)) {
			if (relative == "Telegram" || relative == "Updater"
				|| relative == "ready") {
				return Fail(error, "A required staged file is a directory.");
			}
			const auto child = OpenTrustedDirectory(directory, name, error);
			if (!child || !InventoryDirectory(
					child.get(),
					relative + '/',
					executableName,
					files,
					destinations,
					error)) {
				return false;
			}
			continue;
		}
		const auto mode = info.st_mode & 07777;
		if (!TrustedFile(info)
			|| info.st_size < 0
			|| (mode != 0644 && mode != 0755)
			|| ((relative == "Telegram" || relative == "Updater")
				&& mode != 0755)) {
			return Fail(error, "A staged file has an unsafe type, owner or mode.");
		} else if (skipped) {
			continue;
		}
		const auto file = FileDescriptor(openat(
			directory,
			name.constData(),
			O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
		struct stat opened = {};
		if (!file || fstat(file.get(), &opened)
			|| opened.st_dev != info.st_dev || opened.st_ino != info.st_ino) {
			return Fail(error, "Could not open the inspected staged file.");
		}
		auto digest = FileDigest(file.get(), info.st_size, error);
		if (digest.isEmpty()) {
			return false;
		}
		files.push_back({ target, std::move(digest), info.st_size, mode });
	}
}

[[nodiscard]] bool PreflightDestination(
		int installation,
		const QByteArray &path,
		QString &error) {
	auto parent = OpenTrustedDirectory(installation, ".", error);
	if (!parent) {
		return false;
	}
	const auto parts = path.split('/');
	for (auto i = 0; i != parts.size(); ++i) {
		struct stat info = {};
		if (fstatat(
				parent.get(),
				parts[i].constData(),
				&info,
				AT_SYMLINK_NOFOLLOW)) {
			return errno == ENOENT
				|| Fail(error, "Could not inspect an installation destination.");
		} else if (i + 1 == parts.size()) {
			return TrustedFile(info)
				|| Fail(error, "An installation target is not a protected file.");
		}
		parent = OpenTrustedDirectory(parent.get(), parts[i], error);
		if (!parent) {
			return false;
		}
	}
	return false;
}

[[nodiscard]] bool FileMatches(
		int file,
		const ExpectedFile &expected,
		QString &error) {
	struct stat info = {};
	if (fstat(file, &info)
		|| !TrustedFile(info)
		|| info.st_size != expected.size
		|| (info.st_mode & 07777) != expected.mode) {
		return Fail(error, "An update file has the wrong size, owner or mode.");
	}
	const auto digest = FileDigest(file, expected.size, error);
	if (digest.isEmpty()) {
		return false;
	} else if (digest != expected.digest) {
		return Fail(error, "An update file does not match the authenticated bytes.");
	}
	return true;
}

[[nodiscard]] bool InstalledFileMatches(
		int installation,
		const ExpectedFile &expected,
		QString &error) {
	auto parent = OpenTrustedDirectory(installation, ".", error);
	const auto parts = expected.path.split('/');
	for (auto i = 0; parent && i + 1 < parts.size(); ++i) {
		parent = OpenTrustedDirectory(parent.get(), parts[i], error);
	}
	if (!parent) {
		return false;
	}
	const auto file = FileDescriptor(openat(
		parent.get(),
		parts.back().constData(),
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
	if (!file) {
		return Fail(error, "Could not open an expected installed file safely.");
	}
	return FileMatches(file.get(), expected, error);
}

[[nodiscard]] bool CopyFile(int source, int destination, off_t size) {
	auto buffer = std::array<char, kCopyBufferSize>();
	while (size > 0) {
		const auto count = read(
			source,
			buffer.data(),
			std::min<off_t>(size, buffer.size()));
		if (count < 0 && errno == EINTR) {
			continue;
		} else if (count <= 0) {
			return false;
		}
		for (auto offset = ssize_t(0); offset != count;) {
			const auto written = write(
				destination,
				buffer.data() + offset,
				count - offset);
			if (written < 0 && errno == EINTR) {
				continue;
			} else if (written <= 0) {
				return false;
			}
			offset += written;
		}
		size -= count;
	}
	return true;
}

[[nodiscard]] bool InstallUpdater(
		const Installation &installation,
		int staged,
		const ExpectedFile &expected,
		bool &changed,
		QString &error) {
	auto source = FileDescriptor(openat(
		staged,
		"Updater",
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
	if (!source) {
		return Fail(error, "Could not open the authenticated update helper.");
	} else if (!FileMatches(source.get(), expected, error)) {
		return false;
	} else if (lseek(source.get(), 0, SEEK_SET) != 0) {
		return Fail(error, "Could not rewind the authenticated update helper.");
	}
	const auto directory = installation.directory.get();
	if (!PreflightDestination(directory, "Updater", error)
		|| !CurrentExecutableMatches(installation, error)) {
		return false;
	}
	if (unlinkat(directory, "Updater", 0) == 0) {
		changed = true;
	} else if (errno != ENOENT) {
		return Fail(error, "Could not remove the old installed update helper.");
	}
	auto destination = FileDescriptor(openat(
		directory,
		"Updater",
		O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
		0600));
	if (!destination) {
		return Fail(error, "Could not create the installed update helper.");
	}
	changed = true;
	if (!CopyFile(source.get(), destination.get(), expected.size)
		|| fchmod(destination.get(), expected.mode)
		|| fsync(destination.get())) {
		return Fail(error, "Could not completely write the installed update helper.");
	}
	const auto sourceClosed = source.close();
	const auto destinationClosed = destination.close();
	if (!sourceClosed || !destinationClosed) {
		return Fail(error, "Could not close the copied update helper.");
	} else if (!InstalledFileMatches(directory, expected, error)) {
		return false;
	} else if (unlinkat(staged, "Updater", 0)) {
		return Fail(error, "Could not remove the staged update helper.");
	}
	return true;
}

[[nodiscard]] bool RelayUpdaterLogs(const QString &staging, QString &error) {
	const auto path = QFile::encodeName(staging + u"/DebugLogs"_q);
	const auto directory = OpenTrustedDirectory(AT_FDCWD, path, error);
	if (!directory) {
		return false;
	}
	const auto stream = OpenDirectoryStream(directory.get(), error);
	if (!stream) {
		return false;
	}
	auto remaining = off_t(kMaxDiagnosticBytes);
	auto files = 0;
	auto buffer = std::array<char, kCopyBufferSize>();
	while (true) {
		errno = 0;
		const auto entry = readdir(stream.get());
		if (!entry) {
			return !errno || Fail(error, "Could not finish reading Updater logs.");
		}
		const auto name = QByteArray(entry->d_name);
		if (name == "." || name == "..") {
			continue;
		} else if (!remaining || files++ == kMaxDiagnosticFiles) {
			ReportProgress("Updater diagnostics truncated at the output limit.");
			return true;
		}
		const auto file = FileDescriptor(openat(
			directory.get(),
			name.constData(),
			O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
		struct stat info = {};
		if (!file || fstat(file.get(), &info) || !TrustedFile(info)
			|| info.st_size < 0) {
			return Fail(error, "Could not read a protected regular Updater log.");
		}
		auto left = std::min(remaining, info.st_size);
		const auto truncated = left < info.st_size;
		while (left > 0) {
			const auto count = read(
				file.get(),
				buffer.data(),
				std::min<off_t>(left, buffer.size()));
			if (count < 0 && errno == EINTR) {
				continue;
			} else if (count <= 0) {
				return Fail(error, "Could not read the complete Updater log.");
			} else if (fwrite(buffer.data(), 1, count, stdout) != size_t(count)) {
				return Fail(error, "Could not forward Updater diagnostics.");
			}
			left -= count;
			remaining -= count;
		}
		if (fflush(stdout)) {
			return Fail(error, "Could not flush Updater diagnostics.");
		} else if (truncated) {
			ReportProgress("Updater diagnostics truncated at the output limit.");
			return true;
		}
	}
}

[[nodiscard]] bool RunUpdater(
		const Installation &installation,
		const QString &staging,
		QString &error) {
	namespace GLib = gi::repository::GLib;
	const auto arguments = std::vector<std::string>{
		(installation.directoryPath + "Updater").toStdString(),
		"-debug",
		"-justupdate",
		"-writeprotected",
		"-workpath",
		QFile::encodeName(staging + '/').toStdString(),
		"-exepath",
		installation.directoryPath.toStdString(),
		"-exename",
		installation.executableName.toStdString(),
	};
	const auto environment = std::vector<std::string>{
		"LANG=C",
		"LC_ALL=C",
		"TZ=UTC0",
	};
	auto status = 0;
	const auto spawned = GLib::spawn_sync(
		installation.directoryPath.toStdString(),
		arguments,
		environment,
		GLib::SpawnFlags::DEFAULT_,
		nullptr,
		nullptr,
		nullptr,
		&status,
		nullptr);
	const auto relayed = RelayUpdaterLogs(staging, error);
	if (!spawned) {
		return Fail(error, "Could not execute the installed update helper.");
	} else if (WIFSIGNALED(status)) {
		error = u"The update helper was terminated by signal %1."_q
			.arg(WTERMSIG(status));
		return false;
	} else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		error = u"The update helper failed (wait status %1)."_q.arg(status);
		return false;
	}
	return relayed;
}

[[nodiscard]] int InstallUpdate(const QString &packagePath, bool beta) {
	auto error = QString();
	const auto installation = OpenInstallation(error);
	if (!installation) {
		return ReportError(error);
	}
	ReportProgress("Reading the update package.");
	auto bytes = ReadPackage(packagePath, error);
	if (!bytes) {
		return ReportError(error);
	}
	ReportProgress("Verifying the update package.");
	auto verified = VerifyPackage(*bytes, beta, error);
	bytes.reset();
	if (!verified) {
		return ReportError(error);
	}
	const auto previousMask = umask(022);
	const auto restoreMask = gsl::finally([=] { umask(previousMask); });
	auto staging = PrivateDirectory();
	if (!staging.create(error)) {
		return ReportError(error);
	}
	ReportProgress("Extracting the authenticated package into private staging.");
	const auto version = verified->envelope.version;
	auto uncompressed = Core::Updates::DecompressUpdatePayload(*verified, &error);
	verified.reset();
	if (!uncompressed) {
		return ReportError(error);
	}
	const auto temp = staging.path() + u"/tupdates/temp"_q;
	if (!Core::Updates::ExtractUpdateFiles(*uncompressed, version, temp, &error)
		|| !WriteReady(temp, error)) {
		return ReportError(error);
	}
	uncompressed.reset();
	const auto staged = OpenTrustedDirectory(
		AT_FDCWD,
		QFile::encodeName(temp),
		error);
	if (!staged) {
		return ReportError(error);
	}
	for (const auto required : { "Telegram", "Updater" }) {
		struct stat info = {};
		if (fstatat(staged.get(), required, &info, AT_SYMLINK_NOFOLLOW)
			|| !TrustedExecutable(info)) {
			return ReportError(u"The package requires exact top-level Telegram "
				u"and Updater executables."_q);
		}
	}
	ReportProgress("Checking authenticated files and installation destinations.");
	auto files = std::vector<ExpectedFile>();
	auto destinations = QSet<QByteArray>();
	if (!InventoryDirectory(
			staged.get(),
			QByteArray(),
			installation->executableName,
			files,
			destinations,
			error)) {
		return ReportError(error);
	}
	for (const auto &file : files) {
		if (!PreflightDestination(installation->directory.get(), file.path, error)) {
			return ReportError(error);
		}
	}
	destinations.clear();
	const auto updater = std::ranges::find(
		files,
		QByteArray("Updater"),
		&ExpectedFile::path);
	if (updater == files.end()) {
		return ReportError(u"The authenticated update helper is missing."_q);
	}
	auto changed = false;
	const auto failed = [&] {
		if (changed) {
			error += u" Installation may be incomplete."_q;
		}
		return ReportError(error);
	};
	ReportProgress("Installing the authenticated update helper.");
	if (!InstallUpdater(*installation, staged.get(), *updater, changed, error)) {
		return failed();
	}
	ReportProgress("Running the installed update helper.");
	if (!RunUpdater(*installation, staging.path(), error)) {
		return failed();
	}
	ReportProgress("Verifying all installed files against the package.");
	for (const auto &file : files) {
		if (!InstalledFileMatches(installation->directory.get(), file, error)) {
			error = u"Installed file %1: %2"_q
				.arg(QFile::decodeName(file.path), error);
			return failed();
		}
	}
	if (!staging.cleanup()) {
		return ReportError(u"Files installed, but private staging cleanup failed."_q);
	}
	ReportProgress("Update installed and private staging removed.");
	return 0;
}

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

} // namespace

std::optional<int> InstallUpdateIfRequested(const QStringList &arguments) {
	auto requested = false;
	for (auto i = 1; i < arguments.size(); ++i) {
		if (arguments[i] == u"-installupdate"_q) {
			requested = true;
			break;
		}
	}
	if (!requested) {
		return std::nullopt;
	}
	if ((arguments.size() != 3 && arguments.size() != 4)
		|| arguments[1] != u"-installupdate"_q
		|| !arguments[2].startsWith('/')
		|| arguments[2].contains(QChar(0))
		|| (arguments.size() == 4 && arguments[3] != u"-beta"_q)) {
		return ReportError(u"Usage: -installupdate <absolute-package-path> "
			u"[-beta]. No other arguments are accepted."_q);
	}
#ifdef TDESKTOP_DISABLE_AUTOUPDATE
	return ReportError(u"Automatic updates are disabled in this build."_q);
#else // TDESKTOP_DISABLE_AUTOUPDATE
	if (getuid() != 0 || geteuid() != 0) {
		return ReportError(u"Update installation requires root."_q);
	} else if (Core::BuildUpdateChannel == Core::Updates::Channel::CanaryPrivate) {
		return ReportError(u"Private-canary builds do not support this mode."_q);
	}
	return InstallUpdate(arguments[2], arguments.size() == 4);
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

} // namespace Platform
