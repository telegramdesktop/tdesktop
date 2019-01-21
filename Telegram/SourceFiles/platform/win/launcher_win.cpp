/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/launcher_win.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "platform/platform_specific.h"

#include <windows.h>
#include <shellapi.h>

namespace Platform {
namespace {

QString DeviceModel() {
	return "PC";
}

QString SystemVersion() {
	switch (QSysInfo::windowsVersion()) {
	case QSysInfo::WV_XP: return "Windows XP";
	case QSysInfo::WV_2003: return "Windows 2003";
	case QSysInfo::WV_VISTA: return "Windows Vista";
	case QSysInfo::WV_WINDOWS7: return "Windows 7";
	case QSysInfo::WV_WINDOWS8: return "Windows 8";
	case QSysInfo::WV_WINDOWS8_1: return "Windows 8.1";
	case QSysInfo::WV_WINDOWS10: return "Windows 10";
	default: return "Windows";
	}
}

} // namespace

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv, DeviceModel(), SystemVersion()) {
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

	const auto operation = (action == UpdaterLaunch::JustRelaunch)
		? QString()
		: (cWriteProtected()
			? qsl("runas")
			: QString());
	const auto binaryPath = (action == UpdaterLaunch::JustRelaunch)
		? (cExeDir() + cExeName())
		: (cWriteProtected()
			? (cWorkingDir() + qsl("tupdates/temp/Updater.exe"))
			: (cExeDir() + qsl("Updater.exe")));

	auto argumentsList = QStringList();
	const auto pushArgument = [&](const QString &argument) {
		argumentsList.push_back(argument.trimmed());
	};
	if (cLaunchMode() == LaunchModeAutoStart) {
		pushArgument(qsl("-autostart"));
	}
	if (Logs::DebugEnabled()) {
		pushArgument(qsl("-debug"));
	}
	if (cStartInTray()) {
		pushArgument(qsl("-startintray"));
	}
	if (cTestMode()) {
		pushArgument(qsl("-testmode"));
	}
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (Core::UpdaterDisabled()) {
		pushArgument(qsl("-externalupdater"));
	}
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	if (customWorkingDir()) {
		pushArgument(qsl("-workdir"));
		pushArgument('"' + cWorkingDir() + '"');
	}
	if (cDataFile() != qsl("data")) {
		pushArgument(qsl("-key"));
		pushArgument('"' + cDataFile() + '"');
	}

	if (action == UpdaterLaunch::JustRelaunch) {
		pushArgument(qsl("-noupdate"));
		if (cRestartingToSettings()) {
			pushArgument(qsl("-tosettings"));
		}
	} else {
		pushArgument(qsl("-update"));
		pushArgument(qsl("-exename"));
		pushArgument('"' + cExeName() + '"');
		if (cWriteProtected()) {
			pushArgument(qsl("-writeprotected"));
			pushArgument('"' + cExeDir() + '"');
		}
	}
	return launch(operation, binaryPath, argumentsList);
}

bool Launcher::launch(
		const QString &operation,
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
		operation.isEmpty() ? nullptr : operation.toStdWString().c_str(),
		nativeBinaryPath.c_str(),
		arguments.toStdWString().c_str(),
		nativeWorkingDir.empty() ? nullptr : nativeWorkingDir.c_str(),
		SW_SHOWNORMAL);
	if (long(result) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3"
			).arg(binaryPath
			).arg(cWorkingDir()
			).arg(long(result)
			));
		return false;
	}
	return true;
}

} // namespace
