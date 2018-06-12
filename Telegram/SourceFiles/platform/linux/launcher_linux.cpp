/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

QString DeviceModel() {
#ifdef Q_OS_LINUX64
	return "PC 64bit";
#else // Q_OS_LINUX64
	return "PC 32bit";
#endif // Q_OS_LINUX64
}

QString SystemVersion() {
	const auto result = getenv("XDG_CURRENT_DESKTOP");
	const auto value = result ? QString::fromLatin1(result) : QString();
	const auto list = value.split(':', QString::SkipEmptyParts);
	return list.isEmpty() ? "Linux" : "Linux " + list[0];
}

} // namespace

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv, DeviceModel(), SystemVersion()) {
}

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
	if (Logs::DebugEnabled()) {
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
