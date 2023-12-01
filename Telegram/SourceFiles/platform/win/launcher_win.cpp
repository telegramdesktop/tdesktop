/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/launcher_win.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"

#include <windows.h>
#include <shellapi.h>
#include <VersionHelpers.h>

namespace Platform {

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv) {
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
			? u"runas"_q
			: QString());
	const auto binaryPath = (action == UpdaterLaunch::JustRelaunch)
		? (cExeDir() + cExeName())
		: (cWriteProtected()
			? (cWorkingDir() + u"tupdates/temp/Updater.exe"_q)
			: (cExeDir() + u"Updater.exe"_q));

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
		pushArgument(u"-noupdate"_q);
		if (cRestartingToSettings()) {
			pushArgument(u"-tosettings"_q);
		}
	} else {
		pushArgument(u"-update"_q);
		pushArgument(u"-exename"_q);
		pushArgument('"' + cExeName() + '"');
		if (cWriteProtected()) {
			pushArgument(u"-writeprotected"_q);
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
