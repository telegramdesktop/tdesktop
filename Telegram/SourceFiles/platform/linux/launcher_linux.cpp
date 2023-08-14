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
#include <glib/glib.hpp>

using namespace gi::repository;

namespace Platform {

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
		? (cExeDir() + cExeName()).toStdString()
		: (cWriteProtected()
			? (cWorkingDir() + u"tupdates/temp/Updater"_q)
			: (cExeDir() + u"Updater"_q)).toStdString();

	std::vector<std::string> argumentsList;
	if (writeProtectedUpdate) {
		argumentsList.push_back("pkexec");
		argumentsList.push_back("--keep-cwd");
	} else {
		argumentsList.push_back(binaryPath);
	}
	argumentsList.push_back((justRelaunch && !arguments().isEmpty())
		? arguments().first().toStdString()
		: binaryPath);

	if (cLaunchMode() == LaunchModeAutoStart) {
		argumentsList.push_back("-autostart");
	}
	if (Logs::DebugEnabled()) {
		argumentsList.push_back("-debug");
	}
	if (cStartInTray()) {
		argumentsList.push_back("-startintray");
	}
	if (cDataFile() != u"data"_q) {
		argumentsList.push_back("-key");
		argumentsList.push_back(cDataFile().toStdString());
	}

	if (justRelaunch) {
		argumentsList.push_back("-noupdate");
		argumentsList.push_back("-tosettings");
		if (customWorkingDir()) {
			argumentsList.push_back("-workdir");
			argumentsList.push_back(cWorkingDir().toStdString());
		}
	} else {
		argumentsList.push_back("-workpath");
		argumentsList.push_back(cWorkingDir().toStdString());
		argumentsList.push_back("-exename");
		argumentsList.push_back(cExeName().toStdString());
		argumentsList.push_back("-exepath");
		argumentsList.push_back(cExeDir().toStdString());
		if (!arguments().isEmpty()) {
			argumentsList.push_back("-argv0");
			argumentsList.push_back(arguments().first().toStdString());
		}
		if (customWorkingDir()) {
			argumentsList.push_back("-workdir_custom");
		}
		if (cWriteProtected()) {
			argumentsList.push_back("-writeprotected");
		}
	}

	Logs::closeMain();
	CrashReports::Finish();

	// pkexec needs an alive parent
	if (writeProtectedUpdate) {
		if (!GLib::spawn_sync(
				initialWorkingDir().toStdString(),
				argumentsList,
				std::nullopt,
				GLib::SpawnFlags::SEARCH_PATH_,
				nullptr,
				nullptr,
				nullptr,
				nullptr,
				nullptr)) {
			return false;
		}
		// launch new version in the same environment
		return launchUpdater(UpdaterLaunch::JustRelaunch);
	}

	return GLib::spawn_async(
		initialWorkingDir().toStdString(),
		argumentsList,
		std::nullopt,
		GLib::SpawnFlags::FILE_AND_ARGV_ZERO_,
		nullptr,
		nullptr,
		nullptr);
}

} // namespace
