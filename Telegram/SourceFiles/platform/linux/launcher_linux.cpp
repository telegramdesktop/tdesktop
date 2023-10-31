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
	if (action == UpdaterLaunch::PerformUpdate) {
		_updating = true;
	}

	std::vector<std::string> argumentsList;

	// What we are launching.
	const auto launching = justRelaunch
		? (cExeDir() + cExeName())
		: cWriteProtected()
		? u"pkexec"_q
		: (cExeDir() + u"Updater"_q);
	argumentsList.push_back(launching.toStdString());

	if (justRelaunch) {
		// argv[0] that is passed to what we are launching.
		// It should be added explicitly in case of FILE_AND_ARGV_ZERO_.
		const auto argv0 = !arguments().isEmpty()
			? arguments().first()
			: launching;
		argumentsList.push_back(argv0.toStdString());
	} else if (cWriteProtected()) {
		// Elevated process that pkexec should launch.
		const auto elevated = cWorkingDir() + u"tupdates/temp/Updater"_q;
		argumentsList.push_back(elevated.toStdString());
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

	if (justRelaunch) {
		return GLib::spawn_async(
			initialWorkingDir().toStdString(),
			argumentsList,
			std::nullopt,
			GLib::SpawnFlags::FILE_AND_ARGV_ZERO_,
			nullptr,
			nullptr,
			nullptr);
	} else if (!GLib::spawn_sync(
			argumentsList,
			std::nullopt,
			// if the spawn is sync, working directory is not set
			// and GLib::SpawnFlags::LEAVE_DESCRIPTORS_OPEN_ is set,
			// it goes through an optimized code path
			GLib::SpawnFlags::SEARCH_PATH_
				| GLib::SpawnFlags::LEAVE_DESCRIPTORS_OPEN_,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr)) {
		return false;
	}
	return launchUpdater(UpdaterLaunch::JustRelaunch);
}

} // namespace
