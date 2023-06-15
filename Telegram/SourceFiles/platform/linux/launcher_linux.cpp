/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/launcher_linux.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "webview/platform/linux/webview_linux_webkitgtk.h"

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

int Launcher::exec() {
	for (auto i = arguments().begin(), e = arguments().end(); i != e; ++i) {
		if (*i == u"-webviewhelper"_q && std::distance(i, e) > 1) {
			Webview::WebKitGTK::SetSocketPath((i + 1)->toStdString());
			return Webview::WebKitGTK::Exec();
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

	const auto justRelaunch = action == UpdaterLaunch::JustRelaunch;
	const auto writeProtectedUpdate = action == UpdaterLaunch::PerformUpdate
		&& cWriteProtected();

	const auto binaryPath = justRelaunch
		? QFile::encodeName(cExeDir() + cExeName())
		: QFile::encodeName(cWriteProtected()
			? (cWorkingDir() + u"tupdates/temp/Updater"_q)
			: (cExeDir() + u"Updater"_q));

	auto argumentsList = Arguments();
	if (writeProtectedUpdate) {
		argumentsList.push("pkexec");
	}
	argumentsList.push((justRelaunch && !arguments().isEmpty())
		? QFile::encodeName(arguments().first())
		: binaryPath);

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

	if (justRelaunch) {
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
		if (!arguments().isEmpty()) {
			argumentsList.push("-argv0");
			argumentsList.push(QFile::encodeName(arguments().first()));
		}
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
	case 0:
		execvp(
			writeProtectedUpdate ? args[0] : binaryPath.constData(),
			args);
		return false;
	}

	// pkexec needs an alive parent
	if (writeProtectedUpdate) {
		waitpid(pid, nullptr, 0);
		// launch new version in the same environment
		return launchUpdater(UpdaterLaunch::JustRelaunch);
	}

	return true;
}

} // namespace
