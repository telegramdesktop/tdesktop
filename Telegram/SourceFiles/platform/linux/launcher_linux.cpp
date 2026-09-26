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
#include <ksandbox.h>
#include <glib/glib.hpp>

#ifdef __GLIBC__
#include <malloc.h>
#endif // __GLIBC__

using namespace gi::repository;

namespace Platform {

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv) {
#ifdef __GLIBC__
	mallopt(M_ARENA_MAX, 1);
#endif // __GLIBC__
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

bool Launcher::launchUpdater(UpdaterLaunch action) {
	if (cExeName().isEmpty()) {
		return false;
	}

	const auto justRelaunch = action == UpdaterLaunch::JustRelaunch
		|| KSandbox::isInside();

	if (action == UpdaterLaunch::PerformUpdate) {
		_updating = true;
	}

	std::vector<std::string> argumentsList;

	if (KSandbox::isFlatpak() && _updating) {
		argumentsList.push_back("flatpak-spawn");
		argumentsList.push_back("--latest-version");
		argumentsList.push_back((cExeDir() + cExeName()).toStdString());
	} else if (justRelaunch) {
		// What we are launching.
		const auto launching = (cExeDir() + cExeName());
		argumentsList.push_back(launching.toStdString());
		// argv[0] that is passed to what we are launching.
		// It should be added explicitly in case of FILE_AND_ARGV_ZERO_.
		const auto argv0 = !arguments().isEmpty()
			? arguments().first()
			: launching;
		argumentsList.push_back(argv0.toStdString());
	} else if (cWriteProtected()) {
		argumentsList.push_back(GLib::find_program_in_path("run0")
			? "run0"
			: "pkexec");
		argumentsList.push_back(
			cWorkingDir().toStdString() + "tupdates/temp/Updater");
	} else {
		argumentsList.push_back(cExeDir().toStdString() + "Updater");
	}

	if (Logs::DebugEnabled()) {
		argumentsList.push_back("-debug");
	}

	if (justRelaunch) {
		if (cLaunchMode() == LaunchModeAutoStart) {
			argumentsList.push_back("-autostart");
		}
		if (cStartInTray()) {
			argumentsList.push_back("-startintray");
		}
		if (cDataFile() != u"data"_q) {
			argumentsList.push_back("-key");
			argumentsList.push_back(cDataFile().toStdString());
		}
		if (!_updating) {
			argumentsList.push_back("-noupdate");
			argumentsList.push_back("-tosettings");
		}
		if (customWorkingDir()) {
			argumentsList.push_back("-workdir");
			argumentsList.push_back(cWorkingDir().toStdString());
		}
	} else {
		// Don't relaunch Telegram.
		argumentsList.push_back("-justupdate");

		argumentsList.push_back("-workpath");
		argumentsList.push_back(cWorkingDir().toStdString());
		argumentsList.push_back("-exename");
		argumentsList.push_back(cExeName().toStdString());
		argumentsList.push_back("-exepath");
		argumentsList.push_back(cExeDir().toStdString());
		if (cWriteProtected()) {
			argumentsList.push_back("-writeprotected");
		}
	}

	Logs::closeMain();
	CrashReports::Finish();

	int waitStatus = 0;
	if (justRelaunch) {
		return GLib::spawn_async(
			initialWorkingDir().toStdString(),
			argumentsList,
			{},
			KSandbox::isFlatpak() && _updating
				? GLib::SpawnFlags::SEARCH_PATH_
				: GLib::SpawnFlags::FILE_AND_ARGV_ZERO_,
			nullptr,
			nullptr,
			nullptr);
	} else if (!GLib::spawn_sync(
			argumentsList,
			{},
			// if the spawn is sync, working directory is not set
			// and GLib::SpawnFlags::LEAVE_DESCRIPTORS_OPEN_ is set,
			// it goes through an optimized code path
			GLib::SpawnFlags::SEARCH_PATH_
				| GLib::SpawnFlags::LEAVE_DESCRIPTORS_OPEN_,
			nullptr,
			nullptr,
			nullptr,
			&waitStatus,
			nullptr) || !g_spawn_check_exit_status(waitStatus, nullptr)) {
		return false;
	}
	return launchUpdater(UpdaterLaunch::JustRelaunch);
}

} // namespace
