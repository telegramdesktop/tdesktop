/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/specific_linux.h"

#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_gtk_integration.h"
#include "ui/platform/ui_platform_utility.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/linux/linux_gtk_integration.h"
#include "platform/linux/linux_wayland_integration.h"
#include "base/qt_adapters.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/update_checker.h"
#include "window/window_controller.h"

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "platform/linux/linux_notification_service_watcher.h"
#include "platform/linux/linux_gsd_media_keys.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtCore/QStandardPaths>
#include <QtCore/QProcess>
#include <QtGui/QWindow>

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusConnectionInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusError>
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#include <glib.h>

extern "C" {
#undef signals
#include <gio/gio.h>
#define signals public
} // extern "C"

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

#include <iostream>

using namespace Platform;
using BaseGtkIntegration = base::Platform::GtkIntegration;
using Platform::internal::WaylandIntegration;
using Platform::internal::GtkIntegration;

Q_DECLARE_METATYPE(QMargins);

namespace Platform {
namespace {

constexpr auto kDesktopFile = ":/misc/telegramdesktop.desktop"_cs;
constexpr auto kIconName = "telegram"_cs;
constexpr auto kHandlerTypeName = "x-scheme-handler/tg"_cs;

constexpr auto kXDGDesktopPortalService = "org.freedesktop.portal.Desktop"_cs;
constexpr auto kXDGDesktopPortalObjectPath = "/org/freedesktop/portal/desktop"_cs;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
std::unique_ptr<internal::NotificationServiceWatcher> NSWInstance;

QStringList ListDBusActivatableNames() {
	static const auto Result = [&] {
		const auto message = QDBusMessage::createMethodCall(
			qsl("org.freedesktop.DBus"),
			qsl("/org/freedesktop/DBus"),
			qsl("org.freedesktop.DBus"),
			qsl("ListActivatableNames"));

		const QDBusReply<QStringList> reply = QDBusConnection::sessionBus()
			.call(message);

		if (reply.isValid()) {
			return reply.value();
		} else if (reply.error().type() != QDBusError::Disconnected) {
			LOG(("ListActivatableNames Error: %1: %2")
				.arg(reply.error().name())
				.arg(reply.error().message()));
		}

		return QStringList{};
	}();

	return Result;
}

void PortalAutostart(bool start, bool silent = false) {
	if (cExeName().isEmpty()) {
		return;
	}

	QVariantMap options;
	options["reason"] = tr::lng_settings_auto_start(tr::now);
	options["autostart"] = start;
	options["commandline"] = QStringList{
		cExeName(),
		qsl("-workdir"),
		cWorkingDir(),
		qsl("-autostart")
	};
	options["dbus-activatable"] = false;

	auto message = QDBusMessage::createMethodCall(
		kXDGDesktopPortalService.utf16(),
		kXDGDesktopPortalObjectPath.utf16(),
		qsl("org.freedesktop.portal.Background"),
		qsl("RequestBackground"));

	const auto parentWindowId = [&] {
		if (const auto activeWindow = Core::App().activeWindow()) {
			if (!IsWayland()) {
				return qsl("x11:%1").arg(QString::number(
					activeWindow->widget().get()->windowHandle()->winId(),
					16));
			}
		}
		return QString();
	}();

	message.setArguments({
		parentWindowId,
		options
	});

	if (silent) {
		QDBusConnection::sessionBus().send(message);
		return;
	}

	const QDBusError error = QDBusConnection::sessionBus().call(message);
	if (error.isValid()) {
		LOG(("Flatpak Autostart Error: %1: %2")
			.arg(error.name())
			.arg(error.message()));
	}
}

bool IsXDGDesktopPortalPresent() {
	static const auto Result = QDBusInterface(
		kXDGDesktopPortalService.utf16(),
		kXDGDesktopPortalObjectPath.utf16()).isValid();

	return Result;
}

bool IsXDGDesktopPortalKDEPresent() {
	static const auto Result = QDBusInterface(
		qsl("org.freedesktop.impl.portal.desktop.kde"),
		kXDGDesktopPortalObjectPath.utf16()).isValid();

	return Result;
}

bool IsIBusPortalPresent() {
	static const auto Result = [&] {
		const auto interface = QDBusConnection::sessionBus().interface();
		const auto activatableNames = ListDBusActivatableNames();

		const auto serviceRegistered = interface
			&& interface->isServiceRegistered(
				qsl("org.freedesktop.portal.IBus"));

		const auto serviceActivatable = activatableNames.contains(
			qsl("org.freedesktop.portal.IBus"));

		return serviceRegistered || serviceActivatable;
	}();

	return Result;
}

uint FileChooserPortalVersion() {
	static const auto Result = [&]() -> uint {
		auto message = QDBusMessage::createMethodCall(
			kXDGDesktopPortalService.utf16(),
			kXDGDesktopPortalObjectPath.utf16(),
			kPropertiesInterface.utf16(),
			qsl("Get"));

		message.setArguments({
			qsl("org.freedesktop.portal.FileChooser"),
			qsl("version")
		});

		const QDBusReply<QVariant> reply = QDBusConnection::sessionBus().call(
			message);

		if (reply.isValid()) {
			return reply.value().toUInt();
		}

		LOG(("Error getting FileChooser portal version: %1: %2")
			.arg(reply.error().name())
			.arg(reply.error().message()));

		return 0;
	}();

	return Result;
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

QByteArray EscapeShell(const QByteArray &content) {
	auto result = QByteArray();

	auto b = content.constData(), e = content.constEnd();
	for (auto ch = b; ch != e; ++ch) {
		if (*ch == ' ' || *ch == '"' || *ch == '\'' || *ch == '\\') {
			if (result.isEmpty()) {
				result.reserve(content.size() * 2);
			}
			if (ch > b) {
				result.append(b, ch - b);
			}
			result.append('\\');
			b = ch;
		}
	}
	if (result.isEmpty()) {
		return content;
	}

	if (e > b) {
		result.append(b, e - b);
	}
	return result;
}

QString EscapeShellInLauncher(const QString &content) {
	return EscapeShell(content.toUtf8()).replace('\\', "\\\\");
}

QString FlatpakID() {
	static const auto Result = [] {
		if (!qEnvironmentVariableIsEmpty("FLATPAK_ID")) {
			return qEnvironmentVariable("FLATPAK_ID");
		} else {
			return GetLauncherBasename();
		}
	}();

	return Result;
}

bool RunShellCommand(const QString &program, const QStringList &arguments) {
	const auto result = QProcess::execute(program, arguments);

	const auto command = qsl("%1 %2")
		.arg(program)
		.arg(arguments.join(' '));

	if (result) {
		DEBUG_LOG(("App Error: command failed, code: %1, command: %2")
			.arg(result)
			.arg(command));

		return false;
	}

	DEBUG_LOG(("App Info: command succeeded, command: %1")
		.arg(command));

	return true;
}

bool GenerateDesktopFile(
		const QString &targetPath,
		const QString &args,
		bool silent = false) {
	if (targetPath.isEmpty() || cExeName().isEmpty()) {
		return false;
	}

	DEBUG_LOG(("App Info: placing .desktop file to %1").arg(targetPath));
	if (!QDir(targetPath).exists()) QDir().mkpath(targetPath);

	const auto sourceFile = kDesktopFile.utf16();
	const auto targetFile = targetPath + GetLauncherFilename();

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

	QFile target(targetFile);
	if (target.open(QIODevice::WriteOnly)) {
		fileText = fileText.replace(
			QRegularExpression(
				qsl("^TryExec=.*$"),
				QRegularExpression::MultilineOption),
			qsl("TryExec=%1").arg(
				QString(cExeDir() + cExeName()).replace('\\', "\\\\")));

		fileText = fileText.replace(
			QRegularExpression(
				qsl("^Exec=.*$"),
				QRegularExpression::MultilineOption),
			qsl("Exec=%1 -workdir %2").arg(
				EscapeShellInLauncher(cExeDir() + cExeName()),
				EscapeShellInLauncher(cWorkingDir()))
				+ (args.isEmpty() ? QString() : ' ' + args));

		target.write(fileText.toUtf8());
		target.close();

		if (!Core::UpdaterDisabled()) {
			DEBUG_LOG(("App Info: removing old .desktop files"));
			QFile::remove(qsl("%1telegram.desktop").arg(targetPath));
			QFile::remove(qsl("%1telegramdesktop.desktop").arg(targetPath));
		}

		return true;
	} else {
		if (!silent) {
			LOG(("App Error: Could not open '%1' for write").arg(targetFile));
		}
		return false;
	}
}

void SetGtkScaleFactor() {
	const auto integration = GtkIntegration::Instance();
	const auto ratio = Core::Sandbox::Instance().devicePixelRatio();
	if (!integration || ratio > 1.) {
		return;
	}

	const auto scaleFactor = integration->scaleFactor().value_or(1);
	if (scaleFactor == 1) {
		return;
	}

	LOG(("GTK scale factor: %1").arg(scaleFactor));
	cSetScreenScale(style::CheckScale(scaleFactor * 100));
}

void DarkModeChanged() {
	Core::Sandbox::Instance().customEnterFromEventLoop([] {
		Core::App().settings().setSystemDarkMode(IsDarkMode());
	});
}

} // namespace

void SetWatchingMediaKeys(bool watching) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	static std::unique_ptr<internal::GSDMediaKeys> Instance;

	if (watching && !Instance) {
		Instance = std::make_unique<internal::GSDMediaKeys>();
	} else if (!watching && Instance) {
		Instance = nullptr;
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

void SetApplicationIcon(const QIcon &icon) {
	QApplication::setWindowIcon(icon);
}

bool InFlatpak() {
	static const auto Result = QFileInfo::exists(qsl("/.flatpak-info"));
	return Result;
}

bool InSnap() {
	static const auto Result = qEnvironmentVariableIsSet("SNAP");
	return Result;
}

bool AreQtPluginsBundled() {
#if !defined DESKTOP_APP_USE_PACKAGED || defined DESKTOP_APP_USE_PACKAGED_LAZY
	return true;
#else // !DESKTOP_APP_USE_PACKAGED || DESKTOP_APP_USE_PACKAGED_LAZY
	return false;
#endif // DESKTOP_APP_USE_PACKAGED && !DESKTOP_APP_USE_PACKAGED_LAZY
}

bool UseXDGDesktopPortal() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	static const auto Result = [&] {
		if (InFlatpak() || InSnap()) {
			return true;
		}

		const auto envVar = qEnvironmentVariableIsSet("TDESKTOP_USE_PORTAL");
		const auto portalPresent = IsXDGDesktopPortalPresent();
		const auto neededForKde = DesktopEnvironment::IsKDE()
			&& IsXDGDesktopPortalKDEPresent();

		return portalPresent
			&& (neededForKde || envVar);
	}();

	return Result;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return false;
}

bool CanOpenDirectoryWithPortal() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	static const auto Result = [&] {
		return FileChooserPortalVersion() >= 3;
	}();

	return Result;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return false;
}

bool IsNotificationServiceActivatable() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	static const auto Result = ListDBusActivatableNames().contains(
		qsl("org.freedesktop.Notifications"));

	return Result;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return false;
}

QString AppRuntimeDirectory() {
	static const auto Result = [&] {
		auto runtimeDir = QStandardPaths::writableLocation(
			QStandardPaths::RuntimeLocation);

		if (InFlatpak()) {
			runtimeDir += qsl("/app/") + FlatpakID();
		}

		if (!QFileInfo::exists(runtimeDir)) { // non-systemd distros
			runtimeDir = QDir::tempPath();
		}

		if (!runtimeDir.endsWith('/')) {
			runtimeDir += '/';
		}

		return runtimeDir;
	}();

	return Result;
}

QString SingleInstanceLocalServerName(const QString &hash) {
	const auto idealSocketPath = AppRuntimeDirectory()
		+ hash
		+ '-'
		+ cGUIDStr();

	if (idealSocketPath.size() >= 108) {
		return AppRuntimeDirectory() + hash;
	} else {
		return idealSocketPath;
	}
}

QString GetLauncherBasename() {
	static const auto Result = [&] {
		if (!Core::UpdaterDisabled() && !cExeName().isEmpty()) {
			const auto appimagePath = qsl("file://%1%2")
				.arg(cExeDir())
				.arg(cExeName())
				.toUtf8();

			char md5Hash[33] = { 0 };
			hashMd5Hex(
				appimagePath.constData(),
				appimagePath.size(),
				md5Hash);

			return qsl("appimagekit_%1-%2")
				.arg(md5Hash)
				.arg(AppName.utf16().replace(' ', '_'));
		}

		return qsl(MACRO_TO_STRING(TDESKTOP_LAUNCHER_BASENAME));
	}();

	return Result;
}

QString GetLauncherFilename() {
	static const auto Result = GetLauncherBasename()
		+ qsl(".desktop");
	return Result;
}

QString GetIconName() {
	static const auto Result = InFlatpak()
		? FlatpakID()
		: kIconName.utf16();
	return Result;
}

QImage GetImageFromClipboard() {
	if (const auto integration = GtkIntegration::Instance()) {
		return integration->getImageFromClipboard();
	}

	return {};
}

std::optional<bool> IsDarkMode() {
	const auto integration = BaseGtkIntegration::Instance();
	if (!integration) {
		return std::nullopt;
	}

	if (integration->checkVersion(3, 0, 0)) {
		const auto preferDarkTheme = integration->getBoolSetting(
			qsl("gtk-application-prefer-dark-theme"));
		
		if (!preferDarkTheme.has_value()) {
			return std::nullopt;
		} else if (*preferDarkTheme) {
			return true;
		}
	}

	const auto themeName = integration->getStringSetting(qsl("gtk-theme-name"));
	if (!themeName.has_value()) {
		return std::nullopt;
	} else if (themeName->toLower().contains(qsl("-dark"))) {
		return true;
	}

	return false;
}

bool AutostartSupported() {
	// snap sandbox doesn't allow creating files
	// in folders with names started with a dot
	// and doesn't provide any api to add an app to autostart
	// thus, autostart isn't supported in snap
	return !InSnap();
}

bool TrayIconSupported() {
	return App::wnd()
		? App::wnd()->trayAvailable()
		: false;
}

bool SkipTaskbarSupported() {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	return !IsWayland()
		&& base::Platform::XCB::IsSupportedByWM("_NET_WM_STATE_SKIP_TASKBAR");
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	return false;
}

} // namespace Platform

QRect psDesktopRect() {
	static QRect _monitorRect;
	static auto _monitorLastGot = 0LL;
	auto tnow = crl::now();
	if (tnow > _monitorLastGot + 1000LL || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
	}
	return _monitorRect;
}

void psWriteDump() {
}

void psActivateProcess(uint64 pid) {
//	objc_activateProgram();
}

namespace {

QString GetHomeDir() {
	const auto home = QString(g_get_home_dir());

	if (!home.isEmpty() && !home.endsWith('/')) {
		return home + '/';
	}

	return home;
}

#ifdef __HAIKU__
void HaikuAutostart(bool start) {
	const auto home = GetHomeDir();
	if (home.isEmpty()) {
		return;
	}

	QFile file(home + "config/settings/boot/launch/telegram-desktop");
	if (start) {
		if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream out(&file);
			out
				<< "#!/bin/bash" << Qt::endl
				<< "cd /system/apps" << Qt::endl
				<< "./Telegram -autostart" << " &" << Qt::endl;
			file.close();
			file.setPermissions(file.permissions()
				| QFileDevice::ExeOwner
				| QFileDevice::ExeGroup
				| QFileDevice::ExeOther);
		}
	} else {
		file.remove();
	}
}
#endif // __HAIKU__

} // namespace

QString psAppDataPath() {
	// Previously we used ~/.TelegramDesktop, so look there first.
	// If we find data there, we should still use it.
	auto home = GetHomeDir();
	if (!home.isEmpty()) {
		auto oldPath = home + qsl(".TelegramDesktop/");
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
		psAutoStart(false, true);
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
	LOG(("Launcher filename: %1").arg(GetLauncherFilename()));

	qputenv("PULSE_PROP_application.name", AppName.utf8());
	qputenv("PULSE_PROP_application.icon_name", GetIconName().toLatin1());

	if (const auto integration = BaseGtkIntegration::Instance()) {
		integration->prepareEnvironment();
	} else {
		g_warning("GTK integration is disabled, some feature unavailable. ");
	}

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

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	// Tell the user when XDP file dialog is used
	DEBUG_LOG(("Checking for XDG Desktop Portal..."));
	if (IsXDGDesktopPortalPresent()) {
		DEBUG_LOG(("XDG Desktop Portal is present!"));
		if (UseXDGDesktopPortal()) {
			LOG(("Using XDG Desktop Portal."));
		} else {
			DEBUG_LOG(("Not using XDG Desktop Portal."));
		}
	} else {
		DEBUG_LOG(("XDG Desktop Portal is not present :("));
	}

	// IBus has changed its socket path several times
	// and each change should be synchronized with Qt.
	// Moreover, the last time Qt changed the path,
	// they didn't introduce a fallback to the old path
	// and made the new Qt incompatible with IBus from older distributions.
	// Since tdesktop is distributed in static binary form,
	// it makes sense to use ibus portal whenever it present
	// to ensure compatibility with the maximum range of distributions.
	if (AreQtPluginsBundled()
		&& !InFlatpak()
		&& !InSnap()
		&& IsIBusPortalPresent()) {
		LOG(("IBus portal is present! Using it."));
		qputenv("IBUS_USE_PORTAL", "1");
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

void finish() {
}

void InstallLauncher(bool force) {
	static const auto DisabledByEnv = !qEnvironmentVariableIsEmpty(
		"DESKTOPINTEGRATION");

	// don't update desktop file for alpha version or if updater is disabled
	if ((cAlphaVersion() || Core::UpdaterDisabled() || DisabledByEnv)
		&& !force) {
		return;
	}

	const auto applicationsPath = QStandardPaths::writableLocation(
		QStandardPaths::ApplicationsLocation) + '/';

	GenerateDesktopFile(applicationsPath, qsl("-- %u"));

	const auto icons = QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + qsl("/icons/");

	if (!QDir(icons).exists()) QDir().mkpath(icons);

	const auto icon = icons + kIconName.utf16() + qsl(".png");
	auto iconExists = QFile::exists(icon);
	if (Local::oldSettingsVersion() < 10021 && iconExists) {
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

	RunShellCommand("update-desktop-database", {
		applicationsPath
	});
}

void RegisterCustomScheme(bool force) {
	if (cExeName().isEmpty()) {
		return;
	}

	GError *error = nullptr;

	const auto neededCommandlineBuilder = qsl("%1 -workdir %2 --").arg(
		QString(EscapeShell(QFile::encodeName(cExeDir() + cExeName()))),
		QString(EscapeShell(QFile::encodeName(cWorkingDir()))));

	const auto neededCommandline = qsl("%1 %u")
		.arg(neededCommandlineBuilder);

	auto currentAppInfo = g_app_info_get_default_for_type(
		kHandlerTypeName.utf8().constData(),
		true);

	if (currentAppInfo) {
		const auto currentCommandline = QString(
			g_app_info_get_commandline(currentAppInfo));

		g_object_unref(currentAppInfo);

		if (currentCommandline == neededCommandline) {
			return;
		}
	}

	auto registeredAppInfoList = g_app_info_get_recommended_for_type(
		kHandlerTypeName.utf8().constData());

	for (auto l = registeredAppInfoList; l != nullptr; l = l->next) {
		const auto currentRegisteredAppInfo = reinterpret_cast<GAppInfo*>(
			l->data);

		const auto currentAppInfoId = QString(
			g_app_info_get_id(currentRegisteredAppInfo));

		const auto currentCommandline = QString(
			g_app_info_get_commandline(currentRegisteredAppInfo));

		if (currentCommandline == neededCommandline
			&& currentAppInfoId.startsWith(qsl("userapp-"))) {
			g_app_info_delete(currentRegisteredAppInfo);
		}
	}

	if (registeredAppInfoList) {
		g_list_free_full(registeredAppInfoList, g_object_unref);
	}

	auto newAppInfo = g_app_info_create_from_commandline(
		neededCommandlineBuilder.toUtf8().constData(),
		AppName.utf8().constData(),
		G_APP_INFO_CREATE_SUPPORTS_URIS,
		&error);

	if (newAppInfo) {
		g_app_info_set_as_default_for_type(
			newAppInfo,
			kHandlerTypeName.utf8().constData(),
			&error);

		g_object_unref(newAppInfo);
	}

	if (error) {
		LOG(("App Error: %1").arg(error->message));
		g_error_free(error);
	}
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
		if (DesktopEnvironment::IsUnity()) {
			add("unity-control-center", "sound");
		} else if (DesktopEnvironment::IsKDE()) {
			add("kcmshell5", "kcm_pulseaudio");
			add("kcmshell4", "phonon");
		} else if (DesktopEnvironment::IsGnome()) {
			add("gnome-control-center", "sound");
		} else if (DesktopEnvironment::IsCinnamon()) {
			add("cinnamon-settings", "sound");
		} else if (DesktopEnvironment::IsMATE()) {
			add("mate-volume-control");
		}
#ifdef __HAIKU__
		add("Media");
#endif // __ HAIKU__
		add("pavucontrol-qt");
		add("pavucontrol");
		add("alsamixergui");
		return ranges::any_of(options, [](const Command &command) {
			return QProcess::startDetached(command.command, command.arguments);
		});
	}
	return true;
}

namespace ThirdParty {

void start() {
	if (const auto integration = BaseGtkIntegration::Instance()) {
		integration->load();
	}

	if (const auto integration = GtkIntegration::Instance()) {
		integration->load();
	}

	SetGtkScaleFactor();

	BaseGtkIntegration::Instance()->connectToSetting(
		"gtk-theme-name",
		DarkModeChanged);

	if (BaseGtkIntegration::Instance()->checkVersion(3, 0, 0)) {
		BaseGtkIntegration::Instance()->connectToSetting(
			"gtk-application-prefer-dark-theme",
			DarkModeChanged);
	}

	if (BaseGtkIntegration::Instance()->checkVersion(3, 12, 0)) {
		BaseGtkIntegration::Instance()->connectToSetting(
			"gtk-decoration-layout",
			Ui::Platform::NotifyTitleControlsLayoutChanged);
	}

	// wait for interface announce to know if native window frame is supported
	if (const auto integration = WaylandIntegration::Instance()) {
		integration->waitForInterfaceAnnounce();
	}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	NSWInstance = std::make_unique<internal::NotificationServiceWatcher>();
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

void finish() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	NSWInstance = nullptr;
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
}

} // namespace ThirdParty

} // namespace Platform

void psNewVersion() {
#ifndef __HAIKU__
	Platform::InstallLauncher();
#endif // __HAIKU__
	Platform::RegisterCustomScheme();
}

void psAutoStart(bool start, bool silent) {
#ifdef __HAIKU__
	HaikuAutostart(start);
	return;
#endif // __HAIKU__

	if (InFlatpak()) {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
		PortalAutostart(start, silent);
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	} else {
		const auto autostart = QStandardPaths::writableLocation(
			QStandardPaths::GenericConfigLocation)
			+ qsl("/autostart/");

		if (start) {
			GenerateDesktopFile(autostart, qsl("-autostart"), silent);
		} else {
			QFile::remove(autostart + GetLauncherFilename());
		}
	}
}

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
