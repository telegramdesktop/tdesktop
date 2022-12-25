/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/launcher_linux.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "webview/platform/linux/webview_linux_webkit2gtk.h"

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

Launcher *LauncherInstance = nullptr;

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
	Expects(LauncherInstance == nullptr);

	LauncherInstance = this;
}

Launcher &Launcher::Instance() {
	Expects(LauncherInstance != nullptr);

	return *LauncherInstance;
}

int Launcher::exec() {
	for (auto i = begin(_arguments), e = end(_arguments); i != e; ++i) {
		if (*i == "-webviewhelper" && std::distance(i, e) > 1) {
			Webview::WebKit2Gtk::SetSocketPath(*(i + 1));
			return Webview::WebKit2Gtk::Exec();
		}
	}

	return Core::Launcher::exec();
}

void Launcher::initHook() {
	QApplication::setAttribute(Qt::AA_DisableSessionManager, true);
}

bool Launcher::launchUpdater(UpdaterLaunch action) {
	if (cExeName().isEmpty()) {
		return false;
	}

	const auto binaryPath = (action == UpdaterLaunch::JustRelaunch)
		? (cExeDir() + cExeName())
		: (cWriteProtected()
			? (cWorkingDir() + u"tupdates/temp/Updater"_q)
			: (cExeDir() + u"Updater"_q));

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
	if (cDataFile() != u"data"_q) {
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
