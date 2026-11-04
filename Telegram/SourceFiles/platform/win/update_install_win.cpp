/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/update_install_win.h"

#include "base/platform/base_platform_info.h"
#include "base/platform/win/base_windows_safe_library.h"
#include "base/basic_types.h"
#include "core/update_channel.h"
#include "core/update_checker.h"
#include "core/update_keys.h"
#include "core/update_unpack.h"
#include "core/update_verify.h"

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
#include "mtproto/dedicated_file_loader.h"

extern "C" {
#include <openssl/conf.h>
} // extern "C"
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSet>

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace Platform {
namespace {

[[nodiscard]] std::wstring Native(const QString &path) {
	return QDir::toNativeSeparators(path).toStdWString();
}

[[nodiscard]] bool ProtectedModeSupported(QString &error) {
#ifdef TDESKTOP_DISABLE_AUTOUPDATE
	error = u"Automatic updates are disabled in this build."_q;
	return false;
#else // TDESKTOP_DISABLE_AUTOUPDATE
	if (Core::BuildUpdateChannel == Core::Updates::Channel::CanaryPrivate) {
		error = u"Private-canary builds do not support this mode."_q;
		return false;
	}
	return true;
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

constexpr auto kMaxDiagnosticBytes = 1024 * 1024;
constexpr auto kReadChunkSize = 1024 * 1024;
constexpr auto kWriteAccess = DWORD(FILE_WRITE_DATA
	| FILE_ADD_FILE
	| FILE_ADD_SUBDIRECTORY
	| FILE_WRITE_ATTRIBUTES
	| FILE_WRITE_EA
	| FILE_DELETE_CHILD
	| DELETE
	| WRITE_DAC
	| WRITE_OWNER);
constexpr auto kReplaceAccess = DWORD(FILE_DELETE_CHILD
	| DELETE
	| WRITE_DAC
	| WRITE_OWNER);
constexpr auto kTrustedInstallerSid = L"S-1-5-80-956008885-3418522649"
	L"-1831038044-1853292631-2271478464";

struct HandleCloser {
	void operator()(HANDLE handle) const;
};

struct LocalFreer {
	void operator()(void *memory) const;
};

using Handle = std::unique_ptr<void, HandleCloser>;
using LocalMemory = std::unique_ptr<void, LocalFreer>;

class InstallLog final {
public:
	explicit InstallLog(const QString &path);

	void write(const QString &line);
	void relay(const QString &path);

private:
	QFile _file;
	bool _open = false;

};

class TrustedPrincipals final {
public:
	TrustedPrincipals();

	[[nodiscard]] bool valid() const;
	[[nodiscard]] bool contains(PSID sid) const;
	[[nodiscard]] bool creatorOwner(PSID sid) const;

private:
	std::array<BYTE, SECURITY_MAX_SID_SIZE> _administrators = {};
	std::array<BYTE, SECURITY_MAX_SID_SIZE> _system = {};
	std::array<BYTE, SECURITY_MAX_SID_SIZE> _creator = {};
	LocalMemory _installer;
	bool _valid = false;

};

void HandleCloser::operator()(HANDLE handle) const {
	CloseHandle(handle);
}

void LocalFreer::operator()(void *memory) const {
	LocalFree(memory);
}

InstallLog::InstallLog(const QString &path) : _file(path) {
	DeleteFileW(Native(path).c_str());
	_open = _file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
}

void InstallLog::write(const QString &line) {
	if (_open) {
		_file.write(line.toUtf8() + '\n');
		_file.flush();
	}
}

void InstallLog::relay(const QString &path) {
	auto file = QFile(path);
	if (!_open || !file.open(QIODevice::ReadOnly)) {
		return;
	}
	auto content = file.read(kMaxDiagnosticBytes + 2);
	const auto truncated = content.size() > kMaxDiagnosticBytes;
	content.truncate(
		std::min<qsizetype>(content.size(), kMaxDiagnosticBytes) & ~qsizetype(1));
	_file.write(QString::fromUtf16(
		reinterpret_cast<const char16_t*>(content.constData()),
		content.size() / 2).toUtf8());
	if (truncated) {
		write(u"Updater diagnostics truncated at the output limit."_q);
	}
	_file.flush();
}

TrustedPrincipals::TrustedPrincipals() {
	auto administratorsSize = DWORD(_administrators.size());
	auto systemSize = DWORD(_system.size());
	auto creatorSize = DWORD(_creator.size());
	auto installerRaw = PSID(nullptr);
	_valid = CreateWellKnownSid(
			WinBuiltinAdministratorsSid,
			nullptr,
			_administrators.data(),
			&administratorsSize)
		&& CreateWellKnownSid(
			WinLocalSystemSid,
			nullptr,
			_system.data(),
			&systemSize)
		&& CreateWellKnownSid(
			WinCreatorOwnerSid,
			nullptr,
			_creator.data(),
			&creatorSize)
		&& ConvertStringSidToSidW(kTrustedInstallerSid, &installerRaw);
	_installer = LocalMemory(installerRaw);
}

bool TrustedPrincipals::valid() const {
	return _valid;
}

bool TrustedPrincipals::contains(PSID sid) const {
	return _valid
		&& sid
		&& (EqualSid(sid, PSID(_administrators.data()))
			|| EqualSid(sid, PSID(_system.data()))
			|| EqualSid(sid, _installer.get()));
}

bool TrustedPrincipals::creatorOwner(PSID sid) const {
	return _valid && sid && EqualSid(sid, PSID(_creator.data()));
}

[[nodiscard]] Handle OwnHandle(HANDLE handle) {
	return Handle(handle == INVALID_HANDLE_VALUE ? nullptr : handle);
}

[[nodiscard]] bool Fail(QString &error, const char *message) {
	error = QString::fromLatin1(message);
	return false;
}

[[nodiscard]] QString RunningExecutablePath() {
	auto buffer = std::array<WCHAR, MAX_PATH + 1>{ 0 };
	const auto count = GetModuleFileNameW(
		nullptr,
		buffer.data(),
		DWORD(buffer.size()));
	if (!count || count >= buffer.size()) {
		return QString();
	}
	return QDir::fromNativeSeparators(
		QString::fromWCharArray(buffer.data(), count));
}

[[nodiscard]] bool SafeExecutableName(const QString &name) {
	const auto lower = name.toLower();
	return !name.isEmpty()
		&& !name.contains('/')
		&& !name.contains('\\')
		&& lower != u"updater.exe"_q
		&& lower != u"ready"_q
		&& lower != u"tdata"_q;
}

[[nodiscard]] bool SamePath(const QString &a, const QString &b) {
	return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive)
		== 0;
}

[[nodiscard]] bool ProcessElevated() {
	auto raw = HANDLE(nullptr);
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
		return false;
	}
	const auto token = Handle(raw);
	auto elevation = TOKEN_ELEVATION();
	auto size = DWORD(0);
	return GetTokenInformation(
		token.get(),
		TokenElevation,
		&elevation,
		sizeof(elevation),
		&size) && elevation.TokenIsElevated;
}

[[nodiscard]] bool TrustedDirectory(const QString &path) {
	const auto attributes = GetFileAttributesW(Native(path).c_str());
	return (attributes != INVALID_FILE_ATTRIBUTES)
		&& (attributes & FILE_ATTRIBUTE_DIRECTORY)
		&& !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

[[nodiscard]] bool DirectoryProtected(
		const QString &path,
		DWORD forbidden,
		bool inheritable,
		const TrustedPrincipals &trusted,
		QString &error) {
	auto owner = PSID(nullptr);
	auto dacl = PACL(nullptr);
	auto descriptorRaw = PSECURITY_DESCRIPTOR(nullptr);
	if (!TrustedDirectory(path)) {
		return Fail(error, "An installation directory is missing or a link.");
	} else if (GetNamedSecurityInfoW(
			Native(path).c_str(),
			SE_FILE_OBJECT,
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			&owner,
			nullptr,
			&dacl,
			nullptr,
			&descriptorRaw) != ERROR_SUCCESS) {
		return Fail(error, "Could not read an installation directory security.");
	}
	const auto descriptor = LocalMemory(descriptorRaw);
	if (!trusted.contains(owner)) {
		return Fail(error, "An installation directory is not owned by the system.");
	} else if (!dacl) {
		return Fail(error, "An installation directory has no access list.");
	}
	auto mapping = GENERIC_MAPPING();
	mapping.GenericRead = FILE_GENERIC_READ;
	mapping.GenericWrite = FILE_GENERIC_WRITE;
	mapping.GenericExecute = FILE_GENERIC_EXECUTE;
	mapping.GenericAll = FILE_ALL_ACCESS;
	for (auto i = DWORD(0); i != dacl->AceCount; ++i) {
		auto aceRaw = LPVOID(nullptr);
		if (!GetAce(dacl, i, &aceRaw)) {
			return Fail(error, "Could not read an installation directory access entry.");
		}
		const auto header = static_cast<ACE_HEADER*>(aceRaw);
		const auto inheritOnly = (header->AceFlags & INHERIT_ONLY_ACE) != 0;
		if (inheritOnly && !inheritable) {
			continue;
		}
		switch (header->AceType) {
		case ACCESS_DENIED_ACE_TYPE:
		case ACCESS_DENIED_CALLBACK_ACE_TYPE:
			continue;
		case ACCESS_ALLOWED_ACE_TYPE:
		case ACCESS_ALLOWED_CALLBACK_ACE_TYPE: {
			const auto ace = static_cast<ACCESS_ALLOWED_ACE*>(aceRaw);
			const auto sid = PSID(&ace->SidStart);
			auto mask = ace->Mask;
			MapGenericMask(&mask, &mapping);
			if ((mask & forbidden)
				&& !trusted.contains(sid)
				&& !(inheritOnly && trusted.creatorOwner(sid))) {
				return Fail(error, "An installation directory is writable without privilege.");
			}
		} break;
		default:
			return Fail(error, "An installation directory has an unsupported access entry.");
		}
	}
	return true;
}

[[nodiscard]] bool InstallationProtected(
		const QString &directory,
		const TrustedPrincipals &trusted,
		QString &error) {
	if (!DirectoryProtected(directory, kWriteAccess, true, trusted, error)) {
		return false;
	}
	auto path = QDir::cleanPath(directory);
	while (true) {
		const auto parent = QFileInfo(path).path();
		if (parent.isEmpty() || parent == path) {
			return true;
		} else if (!DirectoryProtected(
				parent,
				kReplaceAccess,
				false,
				trusted,
				error)) {
			return false;
		}
		path = parent;
	}
}

[[nodiscard]] bool RemoveStaging(const QString &path);

[[nodiscard]] bool RemoveStagingContents(
		const QString &path,
		const std::wstring &native) {
	auto found = WIN32_FIND_DATAW();
	const auto search = FindFirstFileExW(
		(native + L"\\*").c_str(),
		FindExInfoBasic,
		&found,
		FindExSearchNameMatch,
		nullptr,
		0);
	if (search == INVALID_HANDLE_VALUE) {
		return false;
	}
	const auto guard = gsl::finally([&] { FindClose(search); });
	auto ok = true;
	do {
		const auto name = QString::fromWCharArray(found.cFileName);
		if (name != u"."_q && name != u".."_q) {
			ok = RemoveStaging(path + '/' + name) && ok;
		}
	} while (FindNextFileW(search, &found));
	return (GetLastError() == ERROR_NO_MORE_FILES) && ok;
}

bool RemoveStaging(const QString &path) {
	const auto native = Native(path);
	const auto attributes = GetFileAttributesW(native.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		return GetLastError() == ERROR_FILE_NOT_FOUND
			|| GetLastError() == ERROR_PATH_NOT_FOUND;
	} else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
		return DeleteFileW(native.c_str()) != FALSE;
	} else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		return RemoveDirectoryW(native.c_str()) != FALSE;
	}
	return RemoveStagingContents(path, native)
		&& RemoveDirectoryW(native.c_str());
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

[[nodiscard]] bool WriteReady(const QString &directory, QString &error) {
	auto file = QFile(directory + u"/ready"_q);
	if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
		return Fail(error, "Could not create the update ready marker.");
	} else if (file.write("1", 1) != 1 || !file.flush()) {
		return Fail(error, "Could not write the update ready marker.");
	}
	file.close();
	return (file.error() == QFileDevice::NoError)
		|| Fail(error, "Could not close the update ready marker.");
}

[[nodiscard]] QSet<QString> UpdaterLogs(const QString &directory) {
	auto result = QSet<QString>();
	const auto list = QDir(directory).entryInfoList(
		{ u"*_upd.txt"_q },
		QDir::Files);
	for (const auto &info : list) {
		result.insert(info.absoluteFilePath());
	}
	return result;
}

[[nodiscard]] bool RunUpdater(
		const QString &directory,
		const QString &executableName,
		QString &error) {
	const auto updater = Native(directory + u"Updater.exe"_q);
	auto command = L'"' + updater + L"\" -update -debug -justupdate"
		L" -writeprotected \"" + directory.toStdWString() + L"\""
		L" -exename \"" + executableName.toStdWString() + L'"';
	auto startup = STARTUPINFOW();
	startup.cb = sizeof(startup);
	auto information = PROCESS_INFORMATION();
	const auto working = Native(directory);
	const auto started = CreateProcessW(
		updater.c_str(),
		command.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		working.c_str(),
		&startup,
		&information);
	if (!started) {
		error = u"Could not run the installed update helper, error %1."_q
			.arg(GetLastError());
		return false;
	}
	const auto process = Handle(information.hProcess);
	const auto thread = Handle(information.hThread);
	auto code = DWORD(1);
	if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0
		|| !GetExitCodeProcess(process.get(), &code)) {
		error = u"Could not wait for the installed update helper."_q;
		return false;
	} else if (code != 0) {
		error = u"The update helper failed (exit code %1)."_q.arg(code);
		return false;
	}
	return true;
}

[[nodiscard]] int InstallUpdate(const QString &packagePath, bool beta) {
	OPENSSL_no_config(); // a planted OPENSSL_CONF must not load modules
	const auto running = RunningExecutablePath();
	const auto executableName = QFileInfo(running).fileName();
	const auto directory = QFileInfo(running).absolutePath() + '/';
	const auto trusted = TrustedPrincipals();
	auto error = QString();
	if (running.isEmpty()
		|| !SafeExecutableName(executableName)
		|| !trusted.valid()
		|| !InstallationProtected(directory, trusted, error)) {
		return 1;
	}
	const auto logs = directory + u"DebugLogs"_q;
	const auto staging = directory + u"tupdates"_q;
	if (!QDir().mkpath(logs)
		|| !DirectoryProtected(logs, kWriteAccess, true, trusted, error)
		|| (QFileInfo::exists(staging)
			&& !DirectoryProtected(
				staging,
				kWriteAccess,
				true,
				trusted,
				error))) {
		return 1;
	}
	const auto lockPath = Native(directory + u"update-install.lock"_q);
	DeleteFileW(lockPath.c_str());
	const auto lock = OwnHandle(CreateFileW(
		lockPath.c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_NEW,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
		nullptr));
	if (!lock) {
		return 2;
	}
	auto log = InstallLog(UpdateInstallLogPath(directory));
	const auto fail = [&](const QString &error) {
		log.write(u"Update install: %1"_q.arg(error));
		return 1;
	};
	log.write(u"Update install: reading the update package."_q);
	auto bytes = ReadUpdatePackage(packagePath, error);
	if (!bytes) {
		return fail(error);
	}
	log.write(u"Update install: verifying the update package."_q);
	auto verified = VerifyPackage(*bytes, beta, error);
	bytes.reset();
	if (!verified) {
		return fail(error);
	}
	const auto temp = staging + u"/temp"_q;
	if (!RemoveStaging(staging)) {
		return fail(u"Could not clear the previous update staging."_q);
	} else if (!QDir().mkpath(temp) || !TrustedDirectory(staging)) {
		return fail(u"Could not create the update staging."_q);
	}
	const auto cleanup = gsl::finally([&] {
		if (!RemoveStaging(staging)) {
			log.write(u"Update install: the staging could not be removed."_q);
		}
	});
	log.write(u"Update install: extracting the package into staging."_q);
	const auto version = verified->envelope.version;
	const auto canary
		= (verified->envelope.channel == Core::Updates::Channel::CanaryPublic)
		|| (verified->envelope.channel == Core::Updates::Channel::CanaryPrivate);
	auto uncompressed = Core::Updates::DecompressUpdatePayload(*verified, &error);
	verified.reset();
	if (!uncompressed) {
		return fail(error);
	} else if (!Core::Updates::ExtractUpdateFiles(
			*uncompressed,
			version,
			temp,
			&error)
		|| !WriteReady(temp, error)) {
		return fail(error);
	}
	uncompressed.reset();
	if (!Core::WriteUpdateVersionFile(
			temp,
			Core::Updates::UpdateVersionBase(version),
			canary ? version : 0)) {
		return fail(u"Could not write the update version file."_q);
	}
	log.write(u"Update install: installing the authenticated update helper."_q);
	const auto staged = temp + u"/Updater.exe"_q;
	const auto installed = directory + u"Updater.exe"_q;
	if (!QFileInfo(staged).isFile()) {
		return fail(u"The package carries no update helper."_q);
	} else if (!CopyFileW(
			Native(staged).c_str(),
			Native(installed).c_str(),
			FALSE)) {
		return fail(u"Could not install the update helper, error %1."_q
			.arg(GetLastError()));
	} else if (!QFile::remove(staged)) {
		return fail(u"Could not remove the staged update helper."_q);
	}
	log.write(u"Update install: running the installed update helper."_q);
	const auto before = UpdaterLogs(logs);
	const auto updated = RunUpdater(directory, executableName, error);
	for (const auto &path : UpdaterLogs(logs) - before) {
		log.write(u"Update install: update helper log %1:"_q.arg(path));
		log.relay(path);
		QFile::remove(path);
	}
	if (!updated) {
		return fail(error);
	} else if (!RemoveStaging(staging)) {
		return fail(u"Files installed, but the staging cleanup failed."_q);
	}
	log.write(u"Update install: update installed and staging removed."_q);
	return 0;
}

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

} // namespace

UpdateInstallMode GetUpdateInstallMode(
		const QString &executablePath,
		QString &error) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	const auto info = QFileInfo(executablePath);
	if (executablePath.isEmpty()
		|| !info.isAbsolute()
		|| !SafeExecutableName(info.fileName())) {
		error = u"The update destination path is invalid."_q;
		return UpdateInstallMode::Refused;
	}
	const auto directory = info.absolutePath() + '/';
	const auto probe = OwnHandle(CreateFileW(
		Native(directory
			+ u"tupdates-probe-"_q
			+ QString::number(GetCurrentProcessId())).c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_NEW,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
		nullptr));
	const auto code = probe ? ERROR_SUCCESS : GetLastError();
	if (code == ERROR_SUCCESS) {
		return UpdateInstallMode::Writable;
	} else if (code != ERROR_ACCESS_DENIED) {
		error = u"The update destination is unavailable, error %1."_q
			.arg(code);
		return UpdateInstallMode::Refused;
	} else if (!ProtectedModeSupported(error)) {
		return UpdateInstallMode::Refused;
	} else if (!SamePath(RunningExecutablePath(), executablePath)) {
		error = u"The update destination is not the running installed image."_q;
		return UpdateInstallMode::Refused;
	}
	return UpdateInstallMode::Protected;
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	error = u"Automatic updates are disabled in this build."_q;
	return UpdateInstallMode::Refused;
#endif // TDESKTOP_DISABLE_AUTOUPDATE
}

QString ReadyUpdatePackagePath(const QString &workingDirectory) {
	return QDir(workingDirectory).absoluteFilePath(u"tupdates/ready-package"_q);
}

bool ReadyUpdatePackagePresent(const QString &workingDirectory) {
	const auto native = Native(ReadyUpdatePackagePath(workingDirectory));
	return GetFileAttributesW(native.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::optional<QByteArray> ReadUpdatePackage(
		const QString &path,
		QString &error) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	const auto file = OwnHandle(CreateFileW(
		Native(path).c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT,
		nullptr));
	auto info = BY_HANDLE_FILE_INFORMATION();
	if (!file
		|| GetFileType(file.get()) != FILE_TYPE_DISK
		|| !GetFileInformationByHandle(file.get(), &info)
		|| (info.dwFileAttributes
			& (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
		error = u"The package must be a regular file without a final link."_q;
		return std::nullopt;
	}
	const auto size = (quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
	if (!size || size > MTP::AbstractDedicatedLoader::kMaxFileSize) {
		error = u"The package must be nonempty and within the update size limit."_q;
		return std::nullopt;
	}
	auto result = QByteArray(qsizetype(size), Qt::Uninitialized);
	auto position = qsizetype(0);
	while (position < result.size()) {
		auto got = DWORD(0);
		const auto chunk = DWORD(std::min<qsizetype>(
			result.size() - position,
			kReadChunkSize));
		if (!ReadFile(file.get(), result.data() + position, chunk, &got, nullptr)
			|| !got) {
			error = u"The package could not be read completely."_q;
			return std::nullopt;
		}
		position += got;
	}
	auto extra = char(0);
	auto got = DWORD(0);
	if (!ReadFile(file.get(), &extra, 1, &got, nullptr) || got != 0) {
		error = u"The package grew or could not be read to its end."_q;
		return std::nullopt;
	}
	return result;
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	error = u"Automatic updates are disabled in this build."_q;
	return std::nullopt;
#endif // TDESKTOP_DISABLE_AUTOUPDATE
}

bool ValidateProtectedUpdate(
		const QByteArray &bytes,
		bool beta,
		QString &error) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	return ProtectedModeSupported(error)
		&& VerifyPackage(bytes, beta, error).has_value();
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	error = u"Automatic updates are disabled in this build."_q;
	return false;
#endif // TDESKTOP_DISABLE_AUTOUPDATE
}

QString UpdateInstallLogPath(const QString &executableDirectory) {
	return QDir(executableDirectory).absoluteFilePath(
		u"DebugLogs/update-install.txt"_q);
}

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
	base::Platform::InitDynamicLibraries();
	if ((arguments.size() != 3 && arguments.size() != 4)
		|| arguments[1] != u"-installupdate"_q
		|| !QDir::isAbsolutePath(arguments[2])
		|| arguments[2].contains(QChar(0))
		|| (arguments.size() == 4 && arguments[3] != u"-beta"_q)) {
		return 1;
	}
	auto error = QString();
	if (!ProtectedModeSupported(error)) {
		return 1;
	}
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (!ProcessElevated()) {
		return 1;
	}
	return InstallUpdate(arguments[2], arguments.size() == 4);
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	return 1;
#endif // TDESKTOP_DISABLE_AUTOUPDATE
}

} // namespace Platform
