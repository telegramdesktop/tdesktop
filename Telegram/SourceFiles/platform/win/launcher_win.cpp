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
#include "platform/win/launcher_win.h"

#include "core/crash_reports.h"
#include "platform/platform_specific.h"

#include <windows.h>
#include <shellapi.h>

namespace Platform {

base::optional<QStringList> Launcher::readArgumentsHook(
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
	return base::none;
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
	if (cDebug()) {
		pushArgument(qsl("-debug"));
	}
	if (cStartInTray()) {
		pushArgument(qsl("-startintray"));
	}
	if (cTestMode()) {
		pushArgument(qsl("-testmode"));
	}
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
