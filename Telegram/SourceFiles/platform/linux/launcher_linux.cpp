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
#include "platform/linux/launcher_linux.h"

#include "core/crash_reports.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

namespace Platform {
namespace {

class Arguments {
public:
	void push(QByteArray argument) {
		argument.append(char(0));
		_argumentValues.push_back(argument);
		_arguments.push_back(_argumentValues.back().data());
	}

	char **result() {
		_arguments.push_back(nullptr);
		return _arguments.data();
	}

private:
	std::vector<QByteArray> _argumentValues;
	std::vector<char*> _arguments;

};

} // namespace

bool Launcher::launchUpdater(UpdaterLaunch action) {
	if (cExeName().isEmpty()) {
		return false;
	}

	const auto binaryName = (action == UpdaterLaunch::JustRelaunch)
		? cExeName()
		: QStringLiteral("Updater");

	auto argumentsList = Arguments();
	argumentsList.push(QFile::encodeName(cExeDir() + binaryName));

	if (cLaunchMode() == LaunchModeAutoStart) {
		argumentsList.push("-autostart");
	}
	if (cDebug()) {
		argumentsList.push("-debug");
	}
	if (cStartInTray()) {
		argumentsList.push("-startintray");
	}
	if (cTestMode()) {
		argumentsList.push("-testmode");
	}
	if (cDataFile() != qsl("data")) {
		argumentsList.push("-key");
		argumentsList.push(QFile::encodeName(cDataFile()));
	}

	if (action == UpdaterLaunch::JustRelaunch) {
		argumentsList.push("-noupdate");
		argumentsList.push("-tosettings");
		if (customWorkingDir()) {
			argumentsList.push("-workdir");
			argumentsList.push(QFile::encodeName(cWorkingDir()));
		}
	} else {
		argumentsList.push("-workpath");
		argumentsList.push(QFile::encodeName(cWorkingDir()));
		argumentsList.push("-exename");
		argumentsList.push(QFile::encodeName(cExeName()));
		argumentsList.push("-exepath");
		argumentsList.push(QFile::encodeName(cExeDir()));
		if (customWorkingDir()) {
			argumentsList.push("-workdir_custom");
		}
	}

	Logs::closeMain();
	CrashReports::Finish();

	const auto args = argumentsList.result();

	pid_t pid = fork();
	switch (pid) {
	case -1: return false;
	case 0: execv(args[0], args); return false;
	}
	return true;
}

} // namespace
