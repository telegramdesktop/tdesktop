/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/launcher_win.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "base/platform/base_platform_file_utilities.h"
#include "platform/win/update_install_win.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>

#include <windows.h>
#include <shellapi.h>
#include <VersionHelpers.h>

#include <crtdbg.h>
#include <cstdlib>

namespace Platform {
namespace {

constexpr auto kMaxUpdateLogBytes = 1024 * 1024;

void WriteUpdateLog(const QString &installLogPath, const QString &status) {
	auto content = QByteArray();
	auto installLog = QFile(installLogPath);
	if (!installLogPath.isEmpty() && installLog.open(QIODevice::ReadOnly)) {
		content = installLog.read(kMaxUpdateLogBytes + 1);
		if (content.size() > kMaxUpdateLogBytes) {
			content = content.left(kMaxUpdateLogBytes)
				+ "\nUpdate launcher: install log truncated.\n";
		}
	}
	content += u"\nUpdate launcher: %1\n"_q.arg(status).toUtf8();
	const auto directory = cWorkingDir() + u"DebugLogs"_q;
	if (!QDir().mkpath(directory)) {
		return;
	}
	const auto timestamp = QDateTime::currentDateTime().toString(
		u"yyyyMMdd_hhmmss"_q);
	for (auto attempt = 0; attempt != 100; ++attempt) {
		auto file = QFile(directory
			+ '/'
			+ timestamp
			+ (attempt ? '_' + QString::number(attempt) : QString())
			+ u"_upd.txt"_q);
		if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
			file.write(content);
			return;
		}
	}
}

// Turn a CRT/STL assertion (or pure-virtual / invalid-parameter) into a real,
// crash-reported failure instead of a modal Abort/Retry/Ignore dialog that
// would block a headless test agent forever.
[[noreturn]] void TestAgentReportCrash(
		const char *kind,
		const QString &message) {
	if (!message.isEmpty()) {
		CrashReports::SetAnnotation("CrtAssert", message);
	}
	// Be loud on stderr too (captured when the launch redirects it to a file).
	fprintf(stderr,
		"\n[testagent] %s: %s\n",
		kind,
		message.toLocal8Bit().constData());
	fflush(stderr);

	// Crashes with an access violation -> caught by the crash reporter, dumped
	// to tdata/working + tdata/dumps, and the process exits non-zero. No dialog.
	Unexpected("Test agent: CRT/STL assertion violation.");
}

#ifdef _DEBUG
int TestAgentReportHook(int reportType, char *message, int *returnValue) {
	if (returnValue) {
		*returnValue = 0; // Do not break into the (absent) debugger.
	}
	if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR) {
		TestAgentReportCrash(
			"assert",
			QString::fromLocal8Bit(message ? message : ""));
	} else if (message) {
		fputs(message, stderr);
		fflush(stderr);
	}
	return TRUE; // Handled -> suppress the default message box.
}

int TestAgentReportHookW(int reportType, wchar_t *message, int *returnValue) {
	if (returnValue) {
		*returnValue = 0;
	}
	if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR) {
		TestAgentReportCrash(
			"assert",
			QString::fromWCharArray(message ? message : L""));
	} else if (message) {
		fputws(message, stderr);
		fflush(stderr);
	}
	return TRUE;
}
#endif // _DEBUG

void InstallTestAgentCrashHandling() {
	// No Windows Error Reporting / general-protection / abort() message boxes —
	// each would block the test agent waiting on a button it cannot press.
	SetErrorMode(SEM_FAILCRITICALERRORS
		| SEM_NOGPFAULTERRORBOX
		| SEM_NOOPENFILEERRORBOX);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

	// Pure-virtual call / invalid CRT parameter -> reported crash, not a dialog.
	_set_purecall_handler([] {
		TestAgentReportCrash("purecall", u"Pure virtual function call"_q);
	});
	_set_invalid_parameter_handler([](
			const wchar_t *expression,
			const wchar_t *,
			const wchar_t *,
			unsigned int,
			uintptr_t) {
		TestAgentReportCrash(
			"invalid-parameter",
			QString::fromWCharArray(
				expression ? expression : L"CRT invalid parameter"));
	});

#ifdef _DEBUG
	// assert()/_ASSERTE and STL bounds/iterator checks (_STL_VERIFY) report via
	// _CrtDbgReport[W]; route them to stderr (never a window) and convert any
	// assertion into a reported crash through the hooks above.
	for (const auto type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) {
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
	_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, TestAgentReportHook);
	_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, TestAgentReportHookW);
#endif // _DEBUG
}

} // namespace

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv) {
}

int Launcher::exec() {
	const auto native = readArgumentsHook(0, nullptr);
	if (const auto result = InstallUpdateIfRequested(
			native ? *native : arguments())) {
		return *result;
	}
	return Core::Launcher::exec();
}

void Launcher::initHook() {
	if (cTestAgent()) {
		InstallTestAgentCrashHandling();
	}
}

std::optional<QStringList> Launcher::readArgumentsHook(
		int argc,
		char *argv[]) const {
	auto count = 0;
	if (const auto list = CommandLineToArgvW(GetCommandLine(), &count)) {
		const auto guard = gsl::finally([&] { LocalFree(list); });
		if (count > 0) {
			auto result = QStringList();
			result.reserve(count);
			for (auto i = 0; i != count; ++i) {
				result.push_back(QString::fromWCharArray(list[i]));
			}
			return result;
		}
	}
	return std::nullopt;
}

bool Launcher::launchUpdater(UpdaterLaunch action) {
	if (cExeName().isEmpty()) {
		return false;
	}
	if (action == UpdaterLaunch::PerformUpdate) {
		_updating = true;
		switch (Core::prepareReadyUpdate()) {
		case Core::ReadyUpdateInstall::Failed:
			return relaunchAfterUpdate(false);
		case Core::ReadyUpdateInstall::Protected:
			return installProtectedUpdate();
		case Core::ReadyUpdateInstall::Unpacked:
			break;
		}
	}

	const auto binaryPath = (action == UpdaterLaunch::JustRelaunch)
		? (cExeDir() + cExeName())
		: (cExeDir() + u"Updater.exe"_q);

	auto argumentsList = QStringList();
	const auto pushArgument = [&](const QString &argument) {
		argumentsList.push_back(argument.trimmed());
	};
	if (cLaunchMode() == LaunchModeAutoStart) {
		pushArgument(u"-autostart"_q);
	}
	if (Logs::DebugEnabled()) {
		pushArgument(u"-debug"_q);
	}
	if (cStartInTray()) {
		pushArgument(u"-startintray"_q);
	}
	if (customWorkingDir()) {
		pushArgument(u"-workdir"_q);
		pushArgument('"' + cWorkingDir() + '"');
	}
	if (cDataFile() != u"data"_q) {
		pushArgument(u"-key"_q);
		pushArgument('"' + cDataFile() + '"');
	}

	if (action == UpdaterLaunch::JustRelaunch) {
		if (!_updating || _updateFailed) {
			pushArgument(u"-noupdate"_q);
		}
		if (cRestartingToSettings()) {
			pushArgument(u"-tosettings"_q);
		}
	} else {
		pushArgument(u"-update"_q);
		auto created = FILETIME();
		auto exited = FILETIME();
		auto kernel = FILETIME();
		auto user = FILETIME();
		if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
			// WHY: Application and storage destruction precede this call, so
			// the updater can terminate us if remaining process teardown hangs.
			const auto creationTime = (quint64(created.dwHighDateTime) << 32)
				| created.dwLowDateTime;
			pushArgument(u"-finishprocess"_q);
			pushArgument(QString::number(GetCurrentProcessId()));
			pushArgument(QString::number(creationTime));
		} else {
			const auto error = GetLastError();
			LOG(("Update Error: could not query process creation time: %1"
				).arg(error));
		}
		pushArgument(u"-exename"_q);
		pushArgument('"' + cExeName() + '"');
	}
	return launch(binaryPath, argumentsList);
}

bool Launcher::installProtectedUpdate() {
	const auto executable = cExeDir() + cExeName();
	const auto package = ReadyUpdatePackagePath(cWorkingDir());
	const auto parameters = u"-installupdate \""_q
		+ QDir::toNativeSeparators(package)
		+ '"'
		+ (cInstallBetaVersion() ? u" -beta"_q : QString());
	const auto nativeExecutable = QDir::toNativeSeparators(
		executable).toStdWString();
	const auto nativeDirectory = QDir::toNativeSeparators(
		cExeDir()).toStdWString();
	const auto nativeParameters = parameters.toStdWString();

	DEBUG_LOG(("Application Info: installing with privilege %1 %2"
		).arg(executable
		).arg(parameters));
	Logs::closeMain();
	CrashReports::Finish();

	auto info = SHELLEXECUTEINFOW();
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	info.lpVerb = L"runas";
	info.lpFile = nativeExecutable.c_str();
	info.lpParameters = nativeParameters.c_str();
	info.lpDirectory = nativeDirectory.c_str();
	info.nShow = SW_SHOWNORMAL;
	auto installed = false;
	auto status = QString();
	auto installLogPath = UpdateInstallLogPath(cExeDir());
	if (!ShellExecuteExW(&info) || !info.hProcess) {
		status = u"the privileged install was not started, error %1"_q.arg(
			GetLastError());
		installLogPath = QString();
	} else {
		const auto process = info.hProcess;
		const auto guard = gsl::finally([&] { CloseHandle(process); });
		auto code = DWORD(1);
		if (WaitForSingleObject(process, INFINITE) != WAIT_OBJECT_0
			|| !GetExitCodeProcess(process, &code)) {
			status = u"could not wait for the privileged install"_q;
		} else if (code == 2) {
			status = u"another update is being installed"_q;
			installLogPath = QString();
		} else {
			installed = (code == 0);
			status = u"privileged install exit code %1"_q.arg(code);
		}
	}
	WriteUpdateLog(installLogPath, status);
	return relaunchAfterUpdate(installed);
}

bool Launcher::relaunchAfterUpdate(bool installed) {
	base::Platform::DeleteDirectory(cWorkingDir() + u"tupdates"_q);
	_updateFailed = !installed;
	return launchUpdater(UpdaterLaunch::JustRelaunch);
}

bool Launcher::launch(
		const QString &binaryPath,
		const QStringList &argumentsList) {
	const auto convertPath = [](const QString &path) {
		return QDir::toNativeSeparators(path).toStdWString();
	};
	const auto nativeBinaryPath = convertPath(binaryPath);
	const auto nativeWorkingDir = convertPath(cWorkingDir());
	const auto arguments = argumentsList.join(' ');

	DEBUG_LOG(("Application Info: executing %1 %2"
		).arg(binaryPath
		).arg(arguments
		));

	Logs::closeMain();
	CrashReports::Finish();

	const auto hwnd = HWND(0);
	const auto result = ShellExecute(
		hwnd,
		nullptr,
		nativeBinaryPath.c_str(),
		arguments.toStdWString().c_str(),
		nativeWorkingDir.empty() ? nullptr : nativeWorkingDir.c_str(),
		SW_SHOWNORMAL);
	if (int64(result) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3"
			).arg(binaryPath
			).arg(cWorkingDir()
			).arg(int64(result)
			));
		return false;
	}
	return true;
}

} // namespace Platform
