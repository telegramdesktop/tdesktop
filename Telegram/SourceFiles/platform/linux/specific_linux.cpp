/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/specific_linux.h"

#include "base/random.h"
#include "base/options.h"
#include "base/platform/base_platform_info.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/linux/linux_wayland_integration.h"
#include "platform/platform_launcher.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/local_url_handlers.h"
#include "core/core_settings.h"
#include "core/update_checker.h"
#include "window/window_controller.h"
#include "webview/platform/linux/webview_linux_webkit2gtk.h"

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"
#include "base/platform/linux/base_linux_xdp_utilities.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#include <QtWidgets/QApplication>
#include <QtWidgets/QSystemTrayIcon>
#include <QtCore/QStandardPaths>
#include <QtCore/QProcess>

#include <kshell.h>
#include <ksandbox.h>

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include <glibmm.h>
#include <giomm.h>
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

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

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
typedef GApplication TDesktopApplication;
typedef GApplicationClass TDesktopApplicationClass;

G_DEFINE_TYPE(
	TDesktopApplication,
	t_desktop_application,
	G_TYPE_APPLICATION)

static void t_desktop_application_class_init(
		TDesktopApplicationClass *klass) {
	const auto application_class = G_APPLICATION_CLASS(klass);

	application_class->before_emit = [](
			GApplication *application,
			GVariant *platformData) {
		if (Platform::IsWayland()) {
			static const auto keys = {
				"activation-token",
				"desktop-startup-id",
			};
			for (const auto &key : keys) {
				const char *token = nullptr;
				g_variant_lookup(platformData, key, "&s", &token);
				if (token) {
					qputenv("XDG_ACTIVATION_TOKEN", token);
					break;
				}
			}
		}
	};

	application_class->add_platform_data = [](
			GApplication *application,
			GVariantBuilder *builder) {
		if (Platform::IsWayland()) {
			const auto token = qgetenv("XDG_ACTIVATION_TOKEN");
			if (!token.isEmpty()) {
				g_variant_builder_add(
					builder,
					"{sv}",
					"activation-token",
					g_variant_new_string(token.constData()));
			}
		}
	};
}

static void t_desktop_application_init(TDesktopApplication *application) {
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

namespace Platform {
namespace {

constexpr auto kDesktopFile = ":/misc/org.telegram.desktop.desktop"_cs;

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
void PortalAutostart(bool start, bool silent) {
	if (cExeName().isEmpty()) {
		return;
	}

	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);

		const auto parentWindowId = [&]() -> Glib::ustring {
			const auto activeWindow = Core::App().activeWindow();
			if (!activeWindow) {
				return {};
			}

			return base::Platform::XDP::ParentWindowID(
				activeWindow->widget()->windowHandle());
		}();

		const auto handleToken = Glib::ustring("tdesktop")
			+ std::to_string(base::RandomValue<uint>());

		std::vector<Glib::ustring> commandline;
		commandline.push_back(cExeName().toStdString());
		if (Core::Sandbox::Instance().customWorkingDir()) {
			commandline.push_back("-workdir");
			commandline.push_back(cWorkingDir().toStdString());
		}
		commandline.push_back("-autostart");

		std::map<Glib::ustring, Glib::VariantBase> options;
		options["handle_token"] = Glib::Variant<Glib::ustring>::create(
			handleToken);
		options["reason"] = Glib::Variant<Glib::ustring>::create(
			tr::lng_settings_auto_start(tr::now).toStdString());
		options["autostart"] = Glib::Variant<bool>::create(start);
		options["commandline"] = base::Platform::MakeGlibVariant(commandline);
		options["dbus-activatable"] = Glib::Variant<bool>::create(false);

		auto uniqueName = connection->get_unique_name();
		uniqueName.erase(0, 1);
		uniqueName.replace(uniqueName.find('.'), 1, 1, '_');

		const auto requestPath = Glib::ustring(
				"/org/freedesktop/portal/desktop/request/")
			+ uniqueName
			+ '/'
			+ handleToken;

		const auto loop = Glib::MainLoop::create();

		const auto signalId = connection->signal_subscribe(
			[&](
				const Glib::RefPtr<Gio::DBus::Connection> &connection,
				const Glib::ustring &sender_name,
				const Glib::ustring &object_path,
				const Glib::ustring &interface_name,
				const Glib::ustring &signal_name,
				Glib::VariantContainerBase parameters) {
				try {
					const auto response = base::Platform::GlibVariantCast<
						uint>(parameters.get_child(0));

					if (response && !silent) {
						LOG(("Portal Autostart Error: Request denied"));
					}
				} catch (const std::exception &e) {
					if (!silent) {
						LOG(("Portal Autostart Error: %1").arg(
							QString::fromStdString(e.what())));
					}
				}

				loop->quit();
			},
			std::string(base::Platform::XDP::kService),
			"org.freedesktop.portal.Request",
			"Response",
			requestPath);

		const auto signalGuard = gsl::finally([&] {
			if (signalId != 0) {
				connection->signal_unsubscribe(signalId);
			}
		});

		connection->call_sync(
			std::string(base::Platform::XDP::kObjectPath),
			"org.freedesktop.portal.Background",
			"RequestBackground",
			base::Platform::MakeGlibVariant(std::tuple{
				parentWindowId,
				options,
			}),
			std::string(base::Platform::XDP::kService));

		if (signalId != 0) {
			QWidget window;
			window.setAttribute(Qt::WA_DontShowOnScreen);
			window.setWindowModality(Qt::ApplicationModal);
			window.show();
			loop->run();
		}
	} catch (const std::exception &e) {
		if (!silent) {
			LOG(("Portal Autostart Error: %1").arg(
				QString::fromStdString(e.what())));
		}
	}
}

void LaunchGApplication() {
	const auto connection = [] {
		try {
			return Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::SESSION);
		} catch (...) {
			return Glib::RefPtr<Gio::DBus::Connection>();
		}
	}();

	using namespace base::Platform::DBus;
	const auto activatableNames = [&] {
		try {
			if (connection) {
				return ListActivatableNames(connection);
			}
		} catch (...) {
		}

		return std::vector<Glib::ustring>();
	}();

	const auto freedesktopNotifications = [&] {
		try {
			if (connection && NameHasOwner(
				connection,
				"org.freedesktop.Notifications")) {
				return true;
			}
		} catch (...) {
		}

		if (ranges::contains(
			activatableNames,
			"org.freedesktop.Notifications")) {
			return true;
		}

		return false;
	};

	if (OptionGApplication.value()
		|| (KSandbox::isFlatpak() && !freedesktopNotifications())) {
		Glib::signal_idle().connect_once([] {
			const auto appId = QGuiApplication::desktopFileName()
				.chopped(8)
				.toStdString();

			const auto app = Glib::wrap(
				G_APPLICATION(
					g_object_new(
						t_desktop_application_get_type(),
						"application-id",
						Gio::Application::id_is_valid(appId)
							? appId.c_str()
							: nullptr,
						"flags",
						G_APPLICATION_HANDLES_OPEN,
						nullptr)));

			app->signal_startup().connect([=] {
				QEventLoop loop;
				loop.exec(QEventLoop::ApplicationExec);
				app->quit();
			}, true);

			app->signal_activate().connect([] {
				Core::Sandbox::Instance().customEnterFromEventLoop([] {
					if (const auto w = App::wnd()) {
						w->activate();
					}
				});
			}, true);

			app->signal_open().connect([](
					const Gio::Application::type_vec_files &files,
					const Glib::ustring &hint) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					for (const auto &file : files) {
						if (file->get_uri_scheme() == "file") {
							gSendPaths.append(
								QString::fromStdString(file->get_path()));
							continue;
						}
						const auto url = QString::fromStdString(
							file->get_uri());
						if (url.isEmpty()) {
							continue;
						}
						if (url.startsWith(qstr("interpret://"))) {
							gSendPaths.append(url);
							continue;
						}
						if (Core::StartUrlRequiresActivate(url)) {
							if (const auto w = App::wnd()) {
								w->activate();
							}
						}
						cSetStartUrl(url);
						Core::App().checkStartUrl();
					}
					if (!cSendPaths().isEmpty()) {
						if (const auto w = App::wnd()) {
							w->sendPaths();
						}
					}
				});
			}, true);

			app->add_action("Quit", [] {
				Core::Sandbox::Instance().customEnterFromEventLoop([] {
					Core::Quit();
				});
			});

			using Window::Notifications::Manager;
			using NotificationId = Manager::NotificationId;
			using NotificationIdTuple = std::result_of<
				decltype(&NotificationId::toTuple)(NotificationId*)
			>::type;

			const auto notificationIdVariantType = [] {
				try {
					return base::Platform::MakeGlibVariant(
						NotificationId().toTuple()).get_type();
				} catch (...) {
					return Glib::VariantType();
				}
			}();

			app->add_action_with_parameter(
				"notification-reply",
				notificationIdVariantType,
				[](const Glib::VariantBase &parameter) {
					Core::Sandbox::Instance().customEnterFromEventLoop([&] {
						try {
							const auto &app = Core::App();
							const auto &notifications = app.notifications();
							notifications.manager().notificationActivated(
								NotificationId::FromTuple(
									base::Platform::GlibVariantCast<
										NotificationIdTuple
									>(parameter)));
						} catch (...) {
						}
					});
				});

			app->add_action_with_parameter(
				"notification-mark-as-read",
				notificationIdVariantType,
				[](const Glib::VariantBase &parameter) {
					Core::Sandbox::Instance().customEnterFromEventLoop([&] {
						try {
							const auto &app = Core::App();
							const auto &notifications = app.notifications();
							notifications.manager().notificationReplied(
								NotificationId::FromTuple(
									base::Platform::GlibVariantCast<
										NotificationIdTuple
									>(parameter)),
								{});
						} catch (...) {
						}
					});
				});

			app->hold();
			app->run(0, nullptr);
		});
	}
}

bool GenerateDesktopFile(
		const QString &targetPath,
		const QStringList &args = {},
		bool silent = false) {
	if (targetPath.isEmpty() || cExeName().isEmpty()) {
		return false;
	}

	DEBUG_LOG(("App Info: placing .desktop file to %1").arg(targetPath));
	if (!QDir(targetPath).exists()) QDir().mkpath(targetPath);

	const auto sourceFile = kDesktopFile.utf16();
	const auto targetFile = targetPath + QGuiApplication::desktopFileName();

	QString fileText;
	QFile source(sourceFile);
	if (source.open(QIODevice::ReadOnly)) {
		QTextStream s(&source);
		fileText = s.readAll();
		source.close();
	} else {
		if (!silent) {
			LOG(("App Error: Could not open '%1' for read").arg(sourceFile));
		}
		return false;
	}

	try {
		const auto target = Glib::KeyFile::create();
		target->load_from_data(
			fileText.toStdString(),
			Glib::KeyFile::Flags::KEEP_COMMENTS
				| Glib::KeyFile::Flags::KEEP_TRANSLATIONS);

		for (const auto &group : target->get_groups()) {
			if (target->has_key(group, "TryExec")) {
				target->set_string(
					group,
					"TryExec",
					KShell::joinArgs({ cExeDir() + cExeName() }).replace(
						'\\',
						qstr("\\\\")).toStdString());
			}

			if (target->has_key(group, "Exec")) {
				if (group == "Desktop Entry" && !args.isEmpty()) {
					QStringList exec;
					exec.append(cExeDir() + cExeName());
					if (Core::Sandbox::Instance().customWorkingDir()) {
						exec.append(qsl("-workdir"));
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
						exec[0] = cExeDir() + cExeName();
						if (Core::Sandbox::Instance().customWorkingDir()) {
							exec.insert(1, qsl("-workdir"));
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

			target->save_to_file(targetFile.toStdString());
		}
	} catch (const std::exception &e) {
		if (!silent) {
			LOG(("App Error: %1").arg(QString::fromStdString(e.what())));
		}
		return false;
	}

	if (!Core::UpdaterDisabled()) {
		DEBUG_LOG(("App Info: removing old .desktop files"));
		QFile::remove(qsl("%1telegram.desktop").arg(targetPath));
		QFile::remove(qsl("%1telegramdesktop.desktop").arg(targetPath));

		const auto appimagePath = qsl("file://%1%2").arg(
			cExeDir(),
			cExeName()).toUtf8();

		char md5Hash[33] = { 0 };
		hashMd5Hex(
			appimagePath.constData(),
			appimagePath.size(),
			md5Hash);

		QFile::remove(qsl("%1appimagekit_%2-%3.desktop").arg(
			targetPath,
			md5Hash,
			AppName.utf16().replace(' ', '_')));
	}

	return true;
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

} // namespace

void SetApplicationIcon(const QIcon &icon) {
	QApplication::setWindowIcon(icon);
}

QString SingleInstanceLocalServerName(const QString &hash) {
	return QDir::tempPath() + '/' + hash + '-' + cGUIDStr();
}

std::optional<bool> IsDarkMode() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	[[maybe_unused]] static const auto Inited = [] {
		using XDPSettingWatcher = base::Platform::XDP::SettingWatcher;
		static const XDPSettingWatcher Watcher(
			[=](
				const Glib::ustring &group,
				const Glib::ustring &key,
				const Glib::VariantBase &value) {
				if (group == "org.freedesktop.appearance"
					&& key == "color-scheme") {
					try {
						const auto ivalue = base::Platform::GlibVariantCast<uint>(value);

						crl::on_main([=] {
							Core::App().settings().setSystemDarkMode(ivalue == 1);
						});
					} catch (...) {
					}
				}
			});

		return true;
	}();

	try {
		const auto result = base::Platform::XDP::ReadSetting(
			"org.freedesktop.appearance",
			"color-scheme");

		if (result.has_value()) {
			const auto value = base::Platform::GlibVariantCast<uint>(*result);
			return value == 1;
		}
	} catch (...) {
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return std::nullopt;
}

bool AutostartSupported() {
	// snap sandbox doesn't allow creating files
	// in folders with names started with a dot
	// and doesn't provide any api to add an app to autostart
	// thus, autostart isn't supported in snap
	return !KSandbox::isSnap();
}

void AutostartToggle(bool enabled, Fn<void(bool)> done) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	const auto guard = gsl::finally([&] {
		if (done) {
			done(enabled);
		}
	});

	const auto silent = !done;
	if (KSandbox::isFlatpak()) {
		PortalAutostart(enabled, silent);
	} else {
		const auto autostart = QStandardPaths::writableLocation(
			QStandardPaths::GenericConfigLocation)
			+ qsl("/autostart/");

		if (enabled) {
			GenerateDesktopFile(autostart, { qsl("-autostart") }, silent);
		} else {
			QFile::remove(autostart + QGuiApplication::desktopFileName());
		}
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
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

} // namespace Platform

void psActivateProcess(uint64 pid) {
//	objc_activateProgram();
}

QString psAppDataPath() {
	// Previously we used ~/.TelegramDesktop, so look there first.
	// If we find data there, we should still use it.
	auto home = QDir::homePath();
	if (!home.isEmpty()) {
		auto oldPath = home + qsl("/.TelegramDesktop/");
		auto oldSettingsBase = oldPath + qsl("tdata/settings");
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
			return qEnvironmentVariable("FLATPAK_ID") + qsl(".desktop");
		}

		if (KSandbox::isSnap()) {
			return qEnvironmentVariable("SNAP_INSTANCE_NAME")
				+ '_'
				+ cExeName()
				+ qsl(".desktop");
		}

		if (!Core::UpdaterDisabled()) {
			QByteArray md5Hash(h);
			if (!Launcher::Instance().customWorkingDir()) {
				const auto exePath = QFile::encodeName(
					cExeDir() + cExeName());

				hashMd5Hex(
					exePath.constData(),
					exePath.size(),
					md5Hash.data());
			}

			return qsl("org.telegram.desktop.%1.desktop").arg(md5Hash);
		}

		return qsl("org.telegram.desktop.desktop");
	}());

	LOG(("Launcher filename: %1").arg(QGuiApplication::desktopFileName()));

	qputenv("PULSE_PROP_application.name", AppName.utf8());
	qputenv("PULSE_PROP_application.icon_name", base::IconName().toLatin1());

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	Glib::init();
	Gio::init();

	Glib::set_prgname(cExeName().toStdString());
	Glib::set_application_name(AppName.data());

#ifdef DESKTOP_APP_USE_PACKAGED_RLOTTIE
	g_warning(
		"Application has been built with foreign rlottie, "
		"animated emojis won't be colored to the selected pack.");
#endif // DESKTOP_APP_USE_PACKAGED_RLOTTIE

#ifdef DESKTOP_APP_USE_PACKAGED_FONTS
	g_warning(
		"Application was built without embedded fonts, "
		"this may lead to font issues.");
#endif // DESKTOP_APP_USE_PACKAGED_FONTS
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	Webview::WebKit2Gtk::SetSocketPath(qsl("%1/%2-%3-webview-%4").arg(
		QDir::tempPath(),
		h,
		cGUIDStr(),
		qsl("%1")).toStdString());
}

void finish() {
}

void InstallLauncher(bool force) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	static const auto DisabledByEnv = !qEnvironmentVariableIsEmpty(
		"DESKTOPINTEGRATION");

	// don't update desktop file for alpha version or if updater is disabled
	if ((cAlphaVersion() || Core::UpdaterDisabled() || DisabledByEnv)
		&& !force) {
		return;
	}

	const auto applicationsPath = QStandardPaths::writableLocation(
		QStandardPaths::ApplicationsLocation) + '/';

	GenerateDesktopFile(applicationsPath);

	const auto icons = QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + qsl("/icons/");

	if (!QDir(icons).exists()) QDir().mkpath(icons);

	const auto icon = icons + base::IconName() + qsl(".png");
	auto iconExists = QFile::exists(icon);
	if (Local::oldSettingsVersion() < 2008012 && iconExists) {
		// Icon was changed.
		if (QFile::remove(icon)) {
			iconExists = false;
		}
	}
	if (!iconExists) {
		if (QFile::copy(qsl(":/gui/art/logo_256.png"), icon)) {
			DEBUG_LOG(("App Info: Icon copied to '%1'").arg(icon));
		}
	}

	QProcess::execute("update-desktop-database", {
		applicationsPath
	});
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
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
	InstallLauncher();
	if (oldVersion > 0
		&& oldVersion <= 4000002
		&& qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
		&& DesktopEnvironment::IsGnome()
		&& !QFile::exists(cWorkingDir() + qsl("tdata/nowayland"))) {
		QFile f(cWorkingDir() + qsl("tdata/nowayland"));
		if (f.open(QIODevice::WriteOnly)) {
			f.write("1");
			f.close();
			Core::Restart(); // restart with X backend
		}
	}
	if (oldVersion <= 4001001 && cAutoStart()) {
		AutostartToggle(true);
	}
}

namespace ThirdParty {

void start() {
	LOG(("Icon theme: %1").arg(QIcon::themeName()));
	LOG(("Fallback icon theme: %1").arg(QIcon::fallbackThemeName()));

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	LaunchGApplication();
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
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
