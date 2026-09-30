/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/launcher_linux.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "platform/linux/update_install_linux.h"
#include "webview/platform/linux/webview_linux_webkitgtk.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <glib/glib.hpp>
#include <ksandbox.h>

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#ifdef __GLIBC__
#include <malloc.h>
#endif // __GLIBC__

using namespace gi::repository;

namespace Platform {
namespace {

using SpawnOutput = gi::Collection<gi::ZTSpan, guint8, gi::transfer_full_t>;

void ReportLaunchError(const QString &error) {
	LOG(("Update Error: %1").arg(error));
	fprintf(stderr, "Update Error: %s\n", error.toUtf8().constData());
}

[[nodiscard]] bool WriteUpdateLog(
		const SpawnOutput &output,
		const SpawnOutput &errors,
		bool spawned,
		int status,
		const QString &spawnError) {
	const auto working = open(
		QFile::encodeName(cWorkingDir()).constData(),
		O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (working < 0) {
		return false;
	}
	const auto closeWorking = gsl::finally([=] { close(working); });
	if (mkdirat(working, "DebugLogs", 0700) && errno != EEXIST) {
		return false;
	}
	const auto directory = openat(
		working,
		"DebugLogs",
		O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (directory < 0) {
		return false;
	}
	const auto closeDirectory = gsl::finally([=] { close(directory); });
	const auto timestamp = QDateTime::currentDateTime()
		.toString(u"yyyyMMdd_hhmmss"_q).toLatin1();
	for (auto attempt = 0; attempt != 100; ++attempt) {
		const auto name = timestamp
			+ (attempt ? '_' + QByteArray::number(attempt) : QByteArray())
			+ "_upd.txt";
		const auto descriptor = openat(
			directory,
			name.constData(),
			O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
			0600);
		if (descriptor < 0) {
			if (errno == EEXIST) {
				continue;
			}
			return false;
		}
		const auto closeFile = gsl::finally([=] {
			if (close(descriptor)) {
				ReportLaunchError(u"Could not close the user update log."_q);
			}
		});
		auto file = QFile();
		if (!file.open(
				descriptor,
				QIODevice::WriteOnly,
				QFileDevice::DontCloseHandle)) {
			return false;
		}
		const auto result = u"\nUpdate launcher: spawned=%1, wait status=%2%3\n"_q
			.arg(spawned)
			.arg(status)
			.arg(spawnError.isEmpty() ? QString() : u", "_q + spawnError)
			.toUtf8();
		if (file.write(
				reinterpret_cast<const char*>(output.data()),
				output.size()) != qint64(output.size())
			|| file.write(
				reinterpret_cast<const char*>(errors.data()),
				errors.size()) != qint64(errors.size())
			|| file.write(result) != result.size()
			|| !file.flush()) {
			return false;
		}
		file.close();
		return file.error() == QFileDevice::NoError;
	}
	return false;
}

} // namespace

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv) {
#ifdef __GLIBC__
	mallopt(M_ARENA_MAX, 1);
#endif // __GLIBC__
}

int Launcher::exec() {
	if (const auto result = InstallUpdateIfRequested(arguments())) {
		return *result;
	}
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
		if (!justRelaunch) {
			if (!Core::checkReadyUpdate()) {
				ReportLaunchError(u"The pending update is no longer ready."_q);
				return relaunchAfterUpdate(false);
			} else if (ReadyUpdatePackagePresent(cWorkingDir())) {
				return installProtectedUpdate();
			}
		}
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
		if (!_updating || _updateFailed) {
			argumentsList.push_back("-noupdate");
		}
		if (!_updating) {
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
	}

	Logs::closeMain();
	CrashReports::Finish();

	int waitStatus = 0;
	if (justRelaunch) {
		auto error = GLib::Error();
		const auto result = GLib::spawn_async(
			initialWorkingDir().toStdString(),
			argumentsList,
			{},
			KSandbox::isFlatpak() && _updating
				? GLib::SpawnFlags::SEARCH_PATH_
				: GLib::SpawnFlags::FILE_AND_ARGV_ZERO_,
			nullptr,
			nullptr,
			&error);
		if (!result) {
			ReportLaunchError(u"Could not relaunch Telegram: %1"_q.arg(
				QString::fromUtf8(error.message_().c_str())));
		}
		return result;
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
			nullptr) || !GLib::spawn_check_exit_status(waitStatus, nullptr)) {
		ReportLaunchError(u"The ordinary update helper failed (status %1)."_q
			.arg(waitStatus));
		return relaunchAfterUpdate(false);
	}
	return launchUpdater(UpdaterLaunch::JustRelaunch);
}

bool Launcher::installProtectedUpdate() {
	auto error = QString();
	const auto mode = GetUpdateInstallMode(cExeDir() + cExeName(), error);
	if (mode != UpdateInstallMode::Protected) {
		ReportLaunchError(error.isEmpty()
			? u"Update permissions changed before installation."_q
			: error);
		return relaunchAfterUpdate(false);
	}
	const auto package = ReadyUpdatePackagePath(cWorkingDir());
	const auto executable = [&]() -> std::optional<QString> {
		const auto bytes = ReadUpdatePackage(package, error);
		return bytes
			? ValidateProtectedUpdate(*bytes, cInstallBetaVersion(), error)
			: std::nullopt;
	}();
	if (!executable) {
		ReportLaunchError(error);
		return relaunchAfterUpdate(false);
	}
	auto command = std::vector<std::string>{
		GLib::find_program_in_path("run0") ? "run0" : "pkexec",
		executable->toStdString(),
		"-installupdate",
		package.toStdString(),
	};
	if (cInstallBetaVersion()) {
		command.push_back("-beta");
	}
	Logs::closeMain();
	CrashReports::Finish();
	auto output = SpawnOutput();
	auto errors = SpawnOutput();
	auto spawnError = GLib::Error();
	auto status = 0;
	const auto spawned = GLib::spawn_sync(
		command,
		{},
		GLib::SpawnFlags::SEARCH_PATH_,
		nullptr,
		&output,
		&errors,
		&status,
		&spawnError);
	const auto description = spawned
		? QString()
		: QString::fromUtf8(spawnError.message_().c_str());
	if (!WriteUpdateLog(output, errors, spawned, status, description)) {
		ReportLaunchError(u"Could not write a fresh user update log."_q);
	}
	const auto installed = spawned
		&& GLib::spawn_check_exit_status(status, nullptr);
	if (!installed) {
		ReportLaunchError(u"Protected update failed (wait status %1): %2"_q
			.arg(status)
			.arg(description));
	}
	return relaunchAfterUpdate(installed);
}

bool Launcher::relaunchAfterUpdate(bool installed) {
	auto error = QString();
	if (!ClearUpdateData(cWorkingDir(), error)) {
		ReportLaunchError(error);
		installed = false;
	}
	_updateFailed = !installed;
	return launchUpdater(UpdaterLaunch::JustRelaunch);
}

} // namespace Platform
