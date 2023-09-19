/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/specific_linux.h"

#include "base/random.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"
#include "base/platform/linux/base_linux_xdp_utilities.h"
#include "ui/platform/ui_platform_window_title.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/linux/linux_wayland_integration.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "core/launcher.h"
#include "core/sandbox.h"
#include "core/core_settings.h"
#include "core/update_checker.h"
#include "webview/platform/linux/webview_linux_webkitgtk.h"

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#include <QtWidgets/QApplication>
#include <QtWidgets/QSystemTrayIcon>
#include <QtCore/QStandardPaths>
#include <QtCore/QProcess>

#include <kshell.h>
#include <ksandbox.h>

#include <glibmm.h>
#include <giomm.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

#include <iostream>

using namespace Platform;
using Platform::internal::WaylandIntegration;

namespace Platform {
namespace {

void PortalAutostart(bool enabled, Fn<void(bool)> done) {
	if (cExeName().isEmpty()) {
		if (done) {
			done(false);
		}
		return;
	}

	const auto connection = [&] {
		try {
			return Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::SESSION);
		} catch (const std::exception &e) {
			if (done) {
				LOG(("Portal Autostart Error: %1").arg(e.what()));
			}
			return Glib::RefPtr<Gio::DBus::Connection>();
		}
	}();

	if (!connection) {
		if (done) {
			done(false);
		}
		return;
	}

	const auto handleToken = Glib::ustring("tdesktop")
		+ std::to_string(base::RandomValue<uint>());

	std::vector<Glib::ustring> commandline;
	commandline.push_back(cExeName().toStdString());
	if (Core::Launcher::Instance().customWorkingDir()) {
		commandline.push_back("-workdir");
		commandline.push_back(cWorkingDir().toStdString());
	}
	commandline.push_back("-autostart");

	std::map<Glib::ustring, Glib::VariantBase> options;
	options["handle_token"] = Glib::create_variant(handleToken);
	options["reason"] = Glib::create_variant(
		Glib::ustring(
			tr::lng_settings_auto_start(tr::now).toStdString()));
	options["autostart"] = Glib::create_variant(enabled);
	options["commandline"] = Glib::create_variant(commandline);
	options["dbus-activatable"] = Glib::create_variant(false);

	auto uniqueName = connection->get_unique_name();
	uniqueName.erase(0, 1);
	uniqueName.replace(uniqueName.find('.'), 1, 1, '_');

	const auto requestPath = Glib::ustring(
			"/org/freedesktop/portal/desktop/request/")
		+ uniqueName
		+ '/'
		+ handleToken;

	const auto window = std::make_shared<QWidget>();
	window->setAttribute(Qt::WA_DontShowOnScreen);
	window->setWindowModality(Qt::ApplicationModal);
	window->show();

	const auto signalId = std::make_shared<uint>();
	*signalId = connection->signal_subscribe(
		[=](
			const Glib::RefPtr<Gio::DBus::Connection> &connection,
			const Glib::ustring &sender_name,
			const Glib::ustring &object_path,
			const Glib::ustring &interface_name,
			const Glib::ustring &signal_name,
			const Glib::VariantContainerBase &parameters) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				(void)window; // don't destroy until finish

				try {
					const auto response = parameters.get_child(
						0
					).get_dynamic<uint>();

					if (response) {
						if (done) {
							LOG(("Portal Autostart Error: Request denied"));
							done(false);
						}
					} else if (done) {
						done(enabled);
					}
				} catch (const std::exception &e) {
					if (done) {
						LOG(("Portal Autostart Error: %1").arg(e.what()));
						done(false);
					}
				}

				if (*signalId) {
					connection->signal_unsubscribe(*signalId);
				}
			});
		},
		base::Platform::XDP::kService,
		base::Platform::XDP::kRequestInterface,
		"Response",
		requestPath);

	connection->call(
		base::Platform::XDP::kObjectPath,
		"org.freedesktop.portal.Background",
		"RequestBackground",
		Glib::create_variant(std::tuple{
			base::Platform::XDP::ParentWindowID(),
			options,
		}),
		[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				try {
					connection->call_finish(result);
				} catch (const std::exception &e) {
					if (done) {
						LOG(("Portal Autostart Error: %1").arg(e.what()));
						done(false);
					}

					if (*signalId) {
						connection->signal_unsubscribe(*signalId);
					}
				}
			});
		},
		base::Platform::XDP::kService);
}

bool GenerateDesktopFile(
		const QString &targetPath,
		const QStringList &args = {},
		bool onlyMainGroup = false,
		bool silent = false) {
	const auto executable = ExecutablePathForShortcuts();
	if (targetPath.isEmpty() || executable.isEmpty()) {
		return false;
	}

	DEBUG_LOG(("App Info: placing .desktop file to %1").arg(targetPath));
	if (!QDir(targetPath).exists()) QDir().mkpath(targetPath);

	const auto sourceFile = u":/misc/org.telegram.desktop.desktop"_q;
	const auto targetFile = targetPath
		+ QGuiApplication::desktopFileName()
		+ u".desktop"_q;

	const auto sourceText = [&] {
		QFile source(sourceFile);
		if (source.open(QIODevice::ReadOnly)) {
			return source.readAll().toStdString();
		}
		return std::string();
	}();

	if (sourceText.empty()) {
		if (!silent) {
			LOG(("App Error: Could not open '%1' for read").arg(sourceFile));
		}
		return false;
	}

	try {
		const auto target = Glib::KeyFile::create();
		target->load_from_data(
			sourceText,
			Glib::KeyFile::Flags::KEEP_COMMENTS
				| Glib::KeyFile::Flags::KEEP_TRANSLATIONS);

		for (const auto &group : target->get_groups()) {
			if (onlyMainGroup && group != "Desktop Entry") {
				target->remove_group(group);
				continue;
			}

			if (target->has_key(group, "TryExec")) {
				target->set_string(
					group,
					"TryExec",
					KShell::joinArgs({ executable }).replace(
						'\\',
						qstr("\\\\")).toStdString());
			}

			if (target->has_key(group, "Exec")) {
				if (group == "Desktop Entry" && !args.isEmpty()) {
					QStringList exec;
					exec.append(executable);
					if (Core::Launcher::Instance().customWorkingDir()) {
						exec.append(u"-workdir"_q);
						exec.append(cWorkingDir());
					}
					exec.append(args);
					target->set_string(
						group,
						"Exec",
						KShell::joinArgs(exec).replace(
							'\\',
							qstr("\\\\")).toStdString());
				} else {
					auto exec = KShell::splitArgs(
						QString::fromStdString(
							target->get_string(group, "Exec")
						).replace(
							qstr("\\\\"),
							qstr("\\")));

					if (!exec.isEmpty()) {
						exec[0] = executable;
						if (Core::Launcher::Instance().customWorkingDir()) {
							exec.insert(1, u"-workdir"_q);
							exec.insert(2, cWorkingDir());
						}
						target->set_string(
							group,
							"Exec",
							KShell::joinArgs(exec).replace(
								'\\',
								qstr("\\\\")).toStdString());
					}
				}
			}
		}

		target->save_to_file(targetFile.toStdString());
	} catch (const std::exception &e) {
		if (!silent) {
			LOG(("App Error: %1").arg(e.what()));
		}
		return false;
	}

	QFile::setPermissions(
		targetFile,
		QFile::permissions(targetFile)
			| QFileDevice::ExeOwner
			| QFileDevice::ExeGroup
			| QFileDevice::ExeOther);

	if (!Core::UpdaterDisabled()) {
		DEBUG_LOG(("App Info: removing old .desktop files"));
		QFile::remove(u"%1telegram.desktop"_q.arg(targetPath));
		QFile::remove(u"%1telegramdesktop.desktop"_q.arg(targetPath));

		const auto appimagePath = u"file://%1%2"_q.arg(
			cExeDir(),
			cExeName()).toUtf8();

		char md5Hash[33] = { 0 };
		hashMd5Hex(
			appimagePath.constData(),
			appimagePath.size(),
			md5Hash);

		QFile::remove(u"%1appimagekit_%2-%3.desktop"_q.arg(
			targetPath,
			md5Hash,
			AppName.utf16().replace(' ', '_')));

		const auto d = QFile::encodeName(QDir(cWorkingDir()).absolutePath());
		hashMd5Hex(d.constData(), d.size(), md5Hash);

		if (!Core::Launcher::Instance().customWorkingDir()) {
			QFile::remove(u"%1org.telegram.desktop._%2.desktop"_q.arg(
				targetPath,
				md5Hash));

			const auto exePath = QFile::encodeName(
				cExeDir() + cExeName());
			hashMd5Hex(exePath.constData(), exePath.size(), md5Hash);
		}

		QFile::remove(u"%1org.telegram.desktop.%2.desktop"_q.arg(
			targetPath,
			md5Hash));
	}

	return true;
}

bool GenerateServiceFile(bool silent = false) {
	const auto executable = ExecutablePathForShortcuts();
	if (executable.isEmpty()) {
		return false;
	}

	const auto targetPath = QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + u"/dbus-1/services/"_q;

	const auto targetFile = targetPath
		+ QGuiApplication::desktopFileName()
		+ u".service"_q;

	DEBUG_LOG(("App Info: placing D-Bus service file to %1").arg(targetPath));
	if (!QDir(targetPath).exists()) QDir().mkpath(targetPath);

	const auto target = Glib::KeyFile::create();
	constexpr auto group = "D-BUS Service";

	target->set_string(
		group,
		"Name",
		QGuiApplication::desktopFileName().toStdString());

	QStringList exec;
	exec.append(executable);
	if (Core::Launcher::Instance().customWorkingDir()) {
		exec.append(u"-workdir"_q);
		exec.append(cWorkingDir());
	}
	target->set_string(
		group,
		"Exec",
		KShell::joinArgs(exec).toStdString());

	try {
		target->save_to_file(targetFile.toStdString());
	} catch (const std::exception &e) {
		if (!silent) {
			LOG(("App Error: %1").arg(e.what()));
		}
		return false;
	}

	if (!Core::UpdaterDisabled() && !Core::Launcher::Instance().customWorkingDir()) {
		DEBUG_LOG(("App Info: removing old D-Bus service files"));

		char md5Hash[33] = { 0 };
		const auto d = QFile::encodeName(QDir(cWorkingDir()).absolutePath());
		hashMd5Hex(d.constData(), d.size(), md5Hash);

		QFile::remove(u"%1org.telegram.desktop._%2.service"_q.arg(
			targetPath,
			md5Hash));
	}

	try {
		Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION
		)->call(
			base::Platform::DBus::kObjectPath,
			base::Platform::DBus::kInterface,
			"ReloadConfig",
			{},
			{},
			base::Platform::DBus::kService
		);
	} catch (...) {
	}

	return true;
}

void InstallLauncher() {
	static const auto DisabledByEnv = !qEnvironmentVariableIsEmpty(
		"DESKTOPINTEGRATION");

	// don't update desktop file for alpha version or if updater is disabled
	if (cAlphaVersion() || Core::UpdaterDisabled() || DisabledByEnv) {
		return;
	}

	const auto applicationsPath = QStandardPaths::writableLocation(
		QStandardPaths::ApplicationsLocation) + '/';

	GenerateDesktopFile(applicationsPath);
	GenerateServiceFile();

	const auto icons = QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + u"/icons/"_q;

	if (!QDir(icons).exists()) QDir().mkpath(icons);

	const auto icon = icons + base::IconName() + u".png"_q;
	QFile::remove(icon);
	if (QFile::copy(u":/gui/art/logo_256.png"_q, icon)) {
		DEBUG_LOG(("App Info: Icon copied to '%1'").arg(icon));
	}

	QProcess::execute("update-desktop-database", {
		applicationsPath
	});
}

} // namespace

void SetApplicationIcon(const QIcon &icon) {
	QApplication::setWindowIcon(icon);
}

QString SingleInstanceLocalServerName(const QString &hash) {
#if defined Q_OS_LINUX && QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
	if (KSandbox::isSnap()) {
		return u"snap."_q
			+ qEnvironmentVariable("SNAP_INSTANCE_NAME")
			+ '.'
			+ hash;
	}
	return hash + '-' + cGUIDStr();
#else // Q_OS_LINUX && Qt >= 6.2.0
	return QDir::tempPath() + '/' + hash + '-' + cGUIDStr();
#endif // !Q_OS_LINUX || Qt < 6.2.0
}

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
std::optional<bool> IsDarkMode() {
	const auto result = base::Platform::XDP::ReadSetting<uint>(
		"org.freedesktop.appearance",
		"color-scheme");

	return result.has_value()
		? std::make_optional(*result == 1)
		: std::nullopt;
}
#endif // Qt < 6.5.0

bool AutostartSupported() {
	return true;
}

void AutostartToggle(bool enabled, Fn<void(bool)> done) {
	if (KSandbox::isFlatpak()) {
		PortalAutostart(enabled, done);
		return;
	}

	const auto success = [&] {
		const auto autostart = QStandardPaths::writableLocation(
			QStandardPaths::GenericConfigLocation)
			+ u"/autostart/"_q;

		if (!enabled) {
			return QFile::remove(
				autostart
					+ QGuiApplication::desktopFileName()
					+ u".desktop"_q);
		}

		return GenerateDesktopFile(
			autostart,
			{ u"-autostart"_q },
			true,
			!done);
	}();

	if (done) {
		done(enabled && success);
	}
}

bool AutostartSkip() {
	return !cAutoStart();
}

bool TrayIconSupported() {
	return QSystemTrayIcon::isSystemTrayAvailable();
}

bool SkipTaskbarSupported() {
	if (const auto integration = WaylandIntegration::Instance()) {
		return integration->skipTaskbarSupported();
	}

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (IsX11()) {
		return base::Platform::XCB::IsSupportedByWM(
			base::Platform::XCB::GetConnectionFromQt(),
			"_NET_WM_STATE_SKIP_TASKBAR");
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	return false;
}

bool RunInBackground() {
	using Ui::Platform::TitleControl;
	const auto layout = Ui::Platform::TitleControlsLayout();
	return (ranges::contains(layout.left, TitleControl::Close)
		|| ranges::contains(layout.right, TitleControl::Close))
		&& !ranges::contains(layout.left, TitleControl::Minimize)
		&& !ranges::contains(layout.right, TitleControl::Minimize);
}

QString ExecutablePathForShortcuts() {
	if (Core::UpdaterDisabled()) {
		const auto &arguments = Core::Launcher::Instance().arguments();
		if (!arguments.isEmpty()) {
			const auto result = QFileInfo(arguments.first()).fileName();
			if (!result.isEmpty()) {
				return result;
			}
		}
		return cExeName();
	}
	return cExeDir() + cExeName();
}

} // namespace Platform

QString psAppDataPath() {
	// Previously we used ~/.TelegramDesktop, so look there first.
	// If we find data there, we should still use it.
	auto home = QDir::homePath();
	if (!home.isEmpty()) {
		auto oldPath = home + u"/.TelegramDesktop/"_q;
		auto oldSettingsBase = oldPath + u"tdata/settings"_q;
		if (QFile::exists(oldSettingsBase + '0')
			|| QFile::exists(oldSettingsBase + '1')
			|| QFile::exists(oldSettingsBase + 's')) {
			return oldPath;
		}
	}

	return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + '/';
}

void psDoCleanup() {
	try {
		Platform::AutostartToggle(false);
		psSendToMenu(false, true);
	} catch (...) {
	}
}

int psCleanup() {
	psDoCleanup();
	return 0;
}

void psDoFixPrevious() {
}

int psFixPrevious() {
	psDoFixPrevious();
	return 0;
}

namespace Platform {

void start() {
	const auto d = QFile::encodeName(QDir(cWorkingDir()).absolutePath());
	char h[33] = { 0 };
	hashMd5Hex(d.constData(), d.size(), h);

	QGuiApplication::setDesktopFileName([&] {
		if (KSandbox::isFlatpak()) {
			return qEnvironmentVariable("FLATPAK_ID");
		}

		if (KSandbox::isSnap()) {
			return qEnvironmentVariable("SNAP_INSTANCE_NAME")
				+ '_'
				+ cExeName();
		}

		if (!Core::UpdaterDisabled()) {
			QByteArray md5Hash(h);
			if (!Core::Launcher::Instance().customWorkingDir()) {
				const auto exePath = QFile::encodeName(
					cExeDir() + cExeName());

				hashMd5Hex(
					exePath.constData(),
					exePath.size(),
					md5Hash.data());
			}

			return u"org.telegram.desktop._%1"_q.arg(md5Hash.constData());
		}

		return u"org.telegram.desktop"_q;
	}());

	LOG(("App ID: %1").arg(QGuiApplication::desktopFileName()));

	if (!qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN")
		&& qEnvironmentVariableIsSet("DESKTOP_STARTUP_ID")) {
		qputenv("XDG_ACTIVATION_TOKEN", qgetenv("DESKTOP_STARTUP_ID"));
	}

	qputenv("PULSE_PROP_application.name", AppName.utf8());
	qputenv("PULSE_PROP_application.icon_name", base::IconName().toLatin1());

	Glib::set_prgname(cExeName().toStdString());
	Glib::set_application_name(AppName.data());

	Glib::init();
	Gio::init();

	Webview::WebKitGTK::SetSocketPath(u"%1/%2-%3-webview-%4"_q.arg(
		QDir::tempPath(),
		h,
		cGUIDStr(),
		u"%1"_q).toStdString());

	InstallLauncher();
}

void finish() {
}

PermissionStatus GetPermissionStatus(PermissionType type) {
	return PermissionStatus::Granted;
}

void RequestPermission(PermissionType type, Fn<void(PermissionStatus)> resultCallback) {
	resultCallback(PermissionStatus::Granted);
}

void OpenSystemSettingsForPermission(PermissionType type) {
}

bool OpenSystemSettings(SystemSettingsType type) {
	if (type == SystemSettingsType::Audio) {
		struct Command {
			QString command;
			QStringList arguments;
		};
		auto options = std::vector<Command>();
		const auto add = [&](const char *option, const char *arg = nullptr) {
			auto command = Command{ .command = option };
			if (arg) {
				command.arguments.push_back(arg);
			}
			options.push_back(std::move(command));
		};
		for (const auto &type : DesktopEnvironment::Get()) {
			using DesktopEnvironment::Type;
			if (type == Type::Unity) {
				add("unity-control-center", "sound");
			} else if (type == Type::KDE) {
				add("kcmshell5", "kcm_pulseaudio");
				add("kcmshell4", "phonon");
			} else if (type == Type::Gnome) {
				add("gnome-control-center", "sound");
			} else if (type == Type::Cinnamon) {
				add("cinnamon-settings", "sound");
			} else if (type == Type::MATE) {
				add("mate-volume-control");
			}
		}
		add("pavucontrol-qt");
		add("pavucontrol");
		add("alsamixergui");
		return ranges::any_of(options, [](const Command &command) {
			return QProcess::startDetached(
				command.command,
				command.arguments);
		});
	}
	return true;
}

void NewVersionLaunched(int oldVersion) {
	if (oldVersion <= 4001001 && cAutoStart()) {
		AutostartToggle(true);
	}
}

QImage DefaultApplicationIcon() {
	return Window::Logo();
}

namespace ThirdParty {

void start() {
}

void finish() {
}

} // namespace ThirdParty

} // namespace Platform

void psSendToMenu(bool send, bool silent) {
}

bool linuxMoveFile(const char *from, const char *to) {
	FILE *ffrom = fopen(from, "rb"), *fto = fopen(to, "wb");
	if (!ffrom) {
		if (fto) fclose(fto);
		return false;
	}
	if (!fto) {
		fclose(ffrom);
		return false;
	}
	static const int BufSize = 65536;
	char buf[BufSize];
	while (size_t size = fread(buf, 1, BufSize, ffrom)) {
		fwrite(buf, 1, size, fto);
	}

	struct stat fst; // from http://stackoverflow.com/questions/5486774/keeping-fileowner-and-permissions-after-copying-file-in-c
	//let's say this wont fail since you already worked OK on that fp
	if (fstat(fileno(ffrom), &fst) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}
	//update to the same uid/gid
	if (fchown(fileno(fto), fst.st_uid, fst.st_gid) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}
	//update the permissions
	if (fchmod(fileno(fto), fst.st_mode) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}

	fclose(ffrom);
	fclose(fto);

	if (unlink(from)) {
		return false;
	}

	return true;
}

bool psLaunchMaps(const Data::LocationPoint &point) {
	return false;
}
