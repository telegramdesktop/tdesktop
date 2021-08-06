/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/launcher_linux.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "platform/linux/linux_gtk_integration.h"

#include <QtWidgets/QApplication>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

namespace Platform {
namespace {

using Platform::internal::GtkIntegration;

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
: Core::Launcher(argc, argv)
, _arguments(argv, argv + argc) {
}

int Launcher::exec() {
	for (auto i = begin(_arguments), e = end(_arguments); i != e; ++i) {
		if (*i == "-basegtkintegration" && std::distance(i, e) > 2) {
			return GtkIntegration::Exec(
				GtkIntegration::Type::Base,
				QString::fromStdString(*(i + 1)),
				QString::fromStdString(*(i + 2)));
		} else if (*i == "-webviewhelper" && std::distance(i, e) > 2) {
			return GtkIntegration::Exec(
				GtkIntegration::Type::Webview,
				QString::fromStdString(*(i + 1)),
				QString::fromStdString(*(i + 2)));
		} else if (*i == "-gtkintegration" && std::distance(i, e) > 2) {
			return GtkIntegration::Exec(
				GtkIntegration::Type::TDesktop,
				QString::fromStdString(*(i + 1)),
				QString::fromStdString(*(i + 2)));
		}
	}

	return Core::Launcher::exec();
}

void Launcher::initHook() {
	QApplication::setAttribute(Qt::AA_DisableSessionManager, true);
	QApplication::setDesktopFileName([] {
		if (!Core::UpdaterDisabled() && !cExeName().isEmpty()) {
			const auto appimagePath = qsl("file://%1%2").arg(
				cExeDir(),
				cExeName()).toUtf8();

			char md5Hash[33] = { 0 };
			hashMd5Hex(
				appimagePath.constData(),
				appimagePath.size(),
				md5Hash);

			return qsl("appimagekit_%1-%2.desktop").arg(
				md5Hash,
				AppName.utf16().replace(' ', '_'));
		}

		return qsl(QT_STRINGIFY(TDESKTOP_LAUNCHER_BASENAME) ".desktop");
	}());
}

bool Launcher::launchUpdater(UpdaterLaunch action) {
	if (cExeName().isEmpty()) {
		return false;
	}

	const auto binaryPath = (action == UpdaterLaunch::JustRelaunch)
		? (cExeDir() + cExeName())
		: (cWriteProtected()
			? (cWorkingDir() + qsl("tupdates/temp/Updater"))
			: (cExeDir() + qsl("Updater")));

	auto argumentsList = Arguments();
	if (action == UpdaterLaunch::PerformUpdate && cWriteProtected()) {
		argumentsList.push("pkexec");
	}
	argumentsList.push(QFile::encodeName(binaryPath));

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
		if (cWriteProtected()) {
			argumentsList.push("-writeprotected");
		}
	}

	Logs::closeMain();
	CrashReports::Finish();

	const auto args = argumentsList.result();

	pid_t pid = fork();
	switch (pid) {
	case -1: return false;
	case 0: execvp(args[0], args); return false;
	}

	// pkexec needs an alive parent
	if (action == UpdaterLaunch::PerformUpdate && cWriteProtected()) {
		waitpid(pid, nullptr, 0);
		// launch new version in the same environment
		return launchUpdater(UpdaterLaunch::JustRelaunch);
	}

	return true;
}

} // namespace
