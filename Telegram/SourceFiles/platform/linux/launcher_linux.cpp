/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/launcher_linux.h"

#include "platform/linux/specific_linux.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"

#include <QtWidgets/QApplication>

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

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv) {
}

void Launcher::initHook() {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
	QApplication::setAttribute(Qt::AA_DisableSessionManager, true);
#endif // Qt >= 5.14

	QApplication::setDesktopFileName(GetLauncherFilename());
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
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (Core::UpdaterDisabled()) {
		argumentsList.push("-externalupdater");
	}
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
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
