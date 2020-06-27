/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/specific_linux.h"

#include "platform/linux/linux_libs.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/linux/file_utilities_linux.h"
#include "platform/platform_notifications_manager.h"
#include "storage/localstorage.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtCore/QStandardPaths>
#include <QtCore/QProcess>
#include <QtCore/QVersionNumber>

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusError>
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

#include <iostream>

using namespace Platform;
using Platform::File::internal::EscapeShell;

namespace Platform {
namespace {

constexpr auto kDesktopFile = ":/misc/telegramdesktop.desktop"_cs;
constexpr auto kSnapLauncherDir = "/var/lib/snapd/desktop/applications/"_cs;
constexpr auto kIconName = "telegram"_cs;

constexpr auto kXDGDesktopPortalService = "org.freedesktop.portal.Desktop"_cs;
constexpr auto kXDGDesktopPortalObjectPath = "/org/freedesktop/portal/desktop"_cs;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

QStringList PlatformThemes;

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
void PortalAutostart(bool autostart, bool silent = false) {
	QVariantMap options;
	options["reason"] = tr::lng_settings_auto_start(tr::now);
	options["autostart"] = autostart;
	options["commandline"] = QStringList({
		cExeName(),
		qsl("-autostart")
	});
	options["dbus-activatable"] = false;

	auto message = QDBusMessage::createMethodCall(
		kXDGDesktopPortalService.utf16(),
		kXDGDesktopPortalObjectPath.utf16(),
		qsl("org.freedesktop.portal.Background"),
		qsl("RequestBackground"));

	message.setArguments({
		QString(),
		options
	});

	if (silent) {
		QDBusConnection::sessionBus().send(message);
	} else {
		const QDBusReply<void> reply = QDBusConnection::sessionBus().call(
			message);

		if (!reply.isValid()) {
			LOG(("Flatpak autostart error: %1").arg(reply.error().message()));
		}
	}
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

		const QDBusReply<uint> reply = QDBusConnection::sessionBus().call(message);

		if (reply.isValid()) {
			return reply.value();
		} else {
			LOG(("Error getting FileChooser portal version: %1")
				.arg(reply.error().message()));
		}

		return 0;
	}();

	return Result;
}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

bool RunShellCommand(const QByteArray &command) {
	auto result = system(command.constData());
	if (result) {
		DEBUG_LOG(("App Error: command failed, code: %1, command (in utf8): %2").arg(result).arg(command.constData()));
		return false;
	}
	DEBUG_LOG(("App Info: command succeeded, command (in utf8): %1").arg(command.constData()));
	return true;
}

[[nodiscard]] bool CheckFontConfigCrash() {
	return InSnap();
}

[[nodiscard]] QString FallbackFontConfigCheckPath() {
	return cWorkingDir() + "tdata/fc-check";
}

#ifdef TDESKTOP_USE_FONTCONFIG_FALLBACK

[[nodiscard]] bool BadFontConfigVersion() {
	if (CheckFontConfigCrash()) {
		return QFile(FallbackFontConfigCheckPath()).exists();
	}
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start("fc-list", QStringList() << "--version");
	process.waitForFinished();
	if (process.exitCode() > 0) {
		LOG(("App Error: Could not start fc-list. Process exited with code: %1.").arg(process.exitCode()));
		return false;
	}

	QString result(process.readAllStandardOutput());
	DEBUG_LOG(("Fontconfig version string: ") + result);

	QVersionNumber version = QVersionNumber::fromString(result.split("version ").last());
	if (version.isNull()) {
		LOG(("App Error: Could not get version from fc-list output."));
		return false;
	}

	LOG(("Fontconfig version: %1.").arg(version.toString()));
	if (version < QVersionNumber::fromString("2.13")) {
		if (!qEnvironmentVariableIsSet("TDESKTOP_FORCE_CUSTOM_FONTCONFIG")) {
			return false;
		}
	}
	return true;
}

void FallbackFontConfig() {
	const auto custom = cWorkingDir() + "tdata/fc-custom-1.conf";

	auto doFallback = [&] {
		if (QFile(custom).exists()) {
			LOG(("Custom FONTCONFIG_FILE: ") + custom);
			qputenv("FONTCONFIG_FILE", QFile::encodeName(custom));
			return true;
		}
		return false;
	};

	if (doFallback()) {
		return;
	}

	if (BadFontConfigVersion()) {
		QFile(":/fc/fc-custom.conf").copy(custom);
		doFallback();
	}
}

#endif // TDESKTOP_USE_FONTCONFIG_FALLBACK

bool GenerateDesktopFile(
		const QString &targetPath,
		const QString &args,
		bool silent = false) {
	DEBUG_LOG(("App Info: placing .desktop file to %1").arg(targetPath));
	if (!QDir(targetPath).exists()) QDir().mkpath(targetPath);

	const auto sourceFile = [&] {
		if (InSnap()) {
			return kSnapLauncherDir.utf16() + GetLauncherFilename();
		} else {
			return kDesktopFile.utf16();
		}
	}();

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
		if (IsStaticBinary() || InAppImage()) {
			fileText = fileText.replace(
				QRegularExpression(
					qsl("^TryExec=.*$"),
					QRegularExpression::MultilineOption),
				qsl("TryExec=")
					+ QFile::encodeName(cExeDir() + cExeName())
						.replace('\\', qsl("\\\\")));
			fileText = fileText.replace(
				QRegularExpression(
					qsl("^Exec=.*$"),
					QRegularExpression::MultilineOption),
				qsl("Exec=")
					+ EscapeShell(QFile::encodeName(cExeDir() + cExeName()))
						.replace('\\', qsl("\\\\"))
					+ (args.isEmpty() ? QString() : ' ' + args));
		} else {
			fileText = fileText.replace(
				QRegularExpression(
					qsl("^Exec=(.*) -- %u$"),
					QRegularExpression::MultilineOption),
				qsl("Exec=\\1")
					+ (args.isEmpty() ? QString() : ' ' + args));
		}

		target.write(fileText.toUtf8());
		target.close();

		DEBUG_LOG(("App Info: removing old .desktop file"));
		QFile(qsl("%1telegram.desktop").arg(targetPath)).remove();

		return true;
	} else {
		if (!silent) {
			LOG(("App Error: Could not open '%1' for write").arg(targetFile));
		}
		return false;
	}
}

} // namespace

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

bool InAppImage() {
	static const auto Result = qEnvironmentVariableIsSet("APPIMAGE");
	return Result;
}

bool IsStaticBinary() {
#ifdef DESKTOP_APP_USE_PACKAGED
		return false;
#else // DESKTOP_APP_USE_PACKAGED
		return true;
#endif // !DESKTOP_APP_USE_PACKAGED
}

bool UseGtkIntegration() {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	static const auto Result = !qEnvironmentVariableIsSet(
		"TDESKTOP_DISABLE_GTK_INTEGRATION");

	return Result;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

	return false;
}

bool IsGtkIntegrationForced() {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	static const auto Result = [&] {
		return PlatformThemes.contains(qstr("gtk3"), Qt::CaseInsensitive)
			|| PlatformThemes.contains(qstr("gtk2"), Qt::CaseInsensitive);
	}();

	return Result;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

	return false;
}

bool UseGtkFileDialog() {
#ifdef TDESKTOP_USE_GTK_FILE_DIALOG
	return true;
#else // TDESKTOP_USE_GTK_FILE_DIALOG
	return false;
#endif // !TDESKTOP_USE_GTK_FILE_DIALOG
}

bool IsQtPluginsBundled() {
#ifdef DESKTOP_APP_USE_PACKAGED_LAZY
	return true;
#else // DESKTOP_APP_USE_PACKAGED_LAZY
	return false;
#endif // !DESKTOP_APP_USE_PACKAGED_LAZY
}

bool IsXDGDesktopPortalPresent() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	static const auto Result = QDBusInterface(
		kXDGDesktopPortalService.utf16(),
		kXDGDesktopPortalObjectPath.utf16()).isValid();

	return Result;
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	return false;
}

bool UseXDGDesktopPortal() {
	static const auto Result = [&] {
		const auto envVar = qEnvironmentVariableIsSet("TDESKTOP_USE_PORTAL");
		const auto portalPresent = IsXDGDesktopPortalPresent();

		return (
			DesktopEnvironment::IsKDE()
				|| envVar
			) && portalPresent;
	}();

	return Result;
}

bool CanOpenDirectoryWithPortal() {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED) && !defined TDESKTOP_DISABLE_DBUS_INTEGRATION
	return FileChooserPortalVersion() >= 3;
#else // (Qt >= 5.15 || DESKTOP_APP_QT_PATCHED) && !TDESKTOP_DISABLE_DBUS_INTEGRATION
	return false;
#endif // (Qt < 5.15 && !DESKTOP_APP_QT_PATCHED) || TDESKTOP_DISABLE_DBUS_INTEGRATION
}

QString ProcessNameByPID(const QString &pid) {
	constexpr auto kMaxPath = 1024;
	char result[kMaxPath] = { 0 };
	auto count = readlink("/proc/" + pid.toLatin1() + "/exe", result, kMaxPath);
	if (count > 0) {
		auto filename = QFile::decodeName(result);
		auto deletedPostfix = qstr(" (deleted)");
		if (filename.endsWith(deletedPostfix) && !QFileInfo(filename).exists()) {
			filename.chop(deletedPostfix.size());
		}
		return filename;
	}

	return QString();
}

QString RealExecutablePath(int argc, char *argv[]) {
	const auto processName = ProcessNameByPID(qsl("self"));

	// Fallback to the first command line argument.
	return !processName.isEmpty()
		? processName
		: argc
			? QFile::decodeName(argv[0])
			: QString();
}

QString CurrentExecutablePath(int argc, char *argv[]) {
	if (InAppImage()) {
		const auto appimagePath = QString::fromUtf8(qgetenv("APPIMAGE"));
		const auto appimagePathList = appimagePath.split('/');

		if (qEnvironmentVariableIsSet("ARGV0")
			&& appimagePathList.size() >= 5
			&& appimagePathList[1] == qstr("run")
			&& appimagePathList[2] == qstr("user")
			&& appimagePathList[4] == qstr("appimagelauncherfs")) {
			return QString::fromUtf8(qgetenv("ARGV0"));
		}

		return appimagePath;
	}

	return RealExecutablePath(argc, argv);
}

QString AppRuntimeDirectory() {
	static const auto Result = [&] {
		auto runtimeDir = QStandardPaths::writableLocation(
			QStandardPaths::RuntimeLocation);

		if (InFlatpak()) {
			const auto flatpakId = [&] {
				if (!qEnvironmentVariableIsEmpty("FLATPAK_ID")) {
					return QString::fromLatin1(qgetenv("FLATPAK_ID"));
				} else {
					return GetLauncherBasename();
				}
			}();

			runtimeDir += qsl("/app/")
				+ flatpakId;
		}

		if (!QFileInfo::exists(runtimeDir)) { // non-systemd distros
			runtimeDir = QDir::tempPath();
		}

		if (runtimeDir.isEmpty()) {
			runtimeDir = qsl("/tmp/");
		}

		if (!runtimeDir.endsWith('/')) {
			runtimeDir += '/';
		}

		return runtimeDir;
	}();

	return Result;
}

QString SingleInstanceLocalServerName(const QString &hash) {
	if (InFlatpak() || InSnap()) {
		return AppRuntimeDirectory() + hash;
	} else {
		return AppRuntimeDirectory() + hash + '-' + cGUIDStr();
	}
}

QString GetLauncherBasename() {
	static const auto Result = [&] {
		if (InSnap()) {
			const auto snapNameKey =
				qEnvironmentVariableIsSet("SNAP_INSTANCE_NAME")
					? "SNAP_INSTANCE_NAME"
					: "SNAP_NAME";

			return qsl("%1_%2")
				.arg(QString::fromLatin1(qgetenv(snapNameKey)))
				.arg(cExeName());
		}

		if (InAppImage()) {
			const auto appimagePath = qsl("file://%1%2")
				.arg(cExeDir())
				.arg(cExeName())
				.toUtf8();

			char md5Hash[33] = { 0 };
			hashMd5Hex(appimagePath.constData(), appimagePath.size(), md5Hash);

			return qsl("appimagekit_%1-%2")
				.arg(md5Hash)
				.arg(AppName.utf16().replace(' ', '_'));
		}

		const auto possibleBasenames = std::vector<QString>{
			qsl(MACRO_TO_STRING(TDESKTOP_LAUNCHER_BASENAME)),
			qsl("Telegram")
		};

		for (const auto &it : possibleBasenames) {
			if (!QStandardPaths::locate(
				QStandardPaths::ApplicationsLocation,
				it + qsl(".desktop")).isEmpty()) {
				return it;
			}
		}

		return possibleBasenames[0];
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
		? GetLauncherBasename()
		: kIconName.utf16();
	return Result;
}

bool GtkClipboardSupported() {
	return (Libs::gtk_clipboard_get != nullptr)
		&& (Libs::gtk_clipboard_wait_for_contents != nullptr)
		&& (Libs::gtk_clipboard_wait_for_image != nullptr)
		&& (Libs::gtk_selection_data_targets_include_image != nullptr)
		&& (Libs::gtk_selection_data_free != nullptr)
		&& (Libs::gdk_pixbuf_get_pixels != nullptr)
		&& (Libs::gdk_pixbuf_get_width != nullptr)
		&& (Libs::gdk_pixbuf_get_height != nullptr)
		&& (Libs::gdk_pixbuf_get_rowstride != nullptr)
		&& (Libs::gdk_pixbuf_get_has_alpha != nullptr)
		&& (Libs::gdk_atom_intern != nullptr)
		&& (Libs::g_object_unref != nullptr);
}

QImage GetImageFromClipboard() {
	QImage data;

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	if (!GtkClipboardSupported() || !App::wnd()->gtkClipboard()) {
		return data;
	}

	auto gsel = Libs::gtk_clipboard_wait_for_contents(
		App::wnd()->gtkClipboard(),
		Libs::gdk_atom_intern("TARGETS", true));

	if (gsel) {
		if (Libs::gtk_selection_data_targets_include_image(gsel, false)) {
			auto img = Libs::gtk_clipboard_wait_for_image(App::wnd()->gtkClipboard());

			if (img) {
				data = QImage(
					Libs::gdk_pixbuf_get_pixels(img),
					Libs::gdk_pixbuf_get_width(img),
					Libs::gdk_pixbuf_get_height(img),
					Libs::gdk_pixbuf_get_rowstride(img),
					Libs::gdk_pixbuf_get_has_alpha(img)
						? QImage::Format_RGBA8888
						: QImage::Format_RGB888).copy();

				Libs::g_object_unref(img);
			}
		}

		Libs::gtk_selection_data_free(gsel);
	}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

	return data;
}

std::optional<crl::time> LastUserInputTime() {
	// TODO: a fallback pure-X11 implementation, this one covers only major DEs on X11 and Wayland
	// an example: https://stackoverflow.com/q/9049087
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	static auto NotSupported = false;

	if (NotSupported) {
		return std::nullopt;
	}

	static const auto Message = QDBusMessage::createMethodCall(
		qsl("org.freedesktop.ScreenSaver"),
		qsl("/org/freedesktop/ScreenSaver"),
		qsl("org.freedesktop.ScreenSaver"),
		qsl("GetSessionIdleTime"));

	const QDBusReply<uint> reply = QDBusConnection::sessionBus().call(
		Message);

	static const auto NotSupportedErrors = {
		QDBusError::ServiceUnknown,
		QDBusError::NotSupported,
	};

	static const auto NotSupportedErrorsToLog = {
		QDBusError::Disconnected,
		QDBusError::AccessDenied,
	};

	if (reply.isValid()) {
		return (crl::now() - static_cast<crl::time>(reply.value()));
	} else if (ranges::contains(NotSupportedErrors, reply.error().type())) {
		NotSupported = true;
	} else {
		if (ranges::contains(NotSupportedErrorsToLog, reply.error().type())) {
			NotSupported = true;
		}

		LOG(("Unable to get last user input time: %1: %2")
			.arg(reply.error().name())
			.arg(reply.error().message()));
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	return std::nullopt;
}

bool AutostartSupported() {
	// snap sandbox doesn't allow creating files in folders with names started with a dot
	// and doesn't provide any api to add an app to autostart
	// thus, autostart isn't supported in snap
	return !InSnap();
}

void FallbackFontConfigCheckBegin() {
	if (!CheckFontConfigCrash()) {
		return;
	}
	auto file = QFile(FallbackFontConfigCheckPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write("1", 1);
	}
}

void FallbackFontConfigCheckEnd() {
	if (!CheckFontConfigCrash()) {
		return;
	}
	QFile(FallbackFontConfigCheckPath()).remove();
}

} // namespace Platform

namespace {

QRect _monitorRect;
auto _monitorLastGot = 0LL;

} // namespace

QRect psDesktopRect() {
	auto tnow = crl::now();
	if (tnow > _monitorLastGot + 1000LL || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
	}
	return _monitorRect;
}

void psWriteDump() {
}

bool _removeDirectory(const QString &path) { // from http://stackoverflow.com/questions/2256945/removing-a-non-empty-directory-programmatically-in-c-or-c
	QByteArray pathRaw = QFile::encodeName(path);
	DIR *d = opendir(pathRaw.constData());
	if (!d) return false;

	while (struct dirent *p = readdir(d)) {
		/* Skip the names "." and ".." as we don't want to recurse on them. */
		if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

		QString fname = path + '/' + p->d_name;
		QByteArray fnameRaw = QFile::encodeName(fname);
		struct stat statbuf;
		if (!stat(fnameRaw.constData(), &statbuf)) {
			if (S_ISDIR(statbuf.st_mode)) {
				if (!_removeDirectory(fname)) {
					closedir(d);
					return false;
				}
			} else {
				if (unlink(fnameRaw.constData())) {
					closedir(d);
					return false;
				}
			}
		}
	}
	closedir(d);

	return !rmdir(pathRaw.constData());
}

void psDeleteDir(const QString &dir) {
	_removeDirectory(dir);
}

void psActivateProcess(uint64 pid) {
//	objc_activateProgram();
}

namespace {

QString getHomeDir() {
	auto home = QDir::homePath();

	if (home != QDir::rootPath())
		return home + '/';

	struct passwd *pw = getpwuid(getuid());
	return (pw && pw->pw_dir && strlen(pw->pw_dir)) ? (QFile::decodeName(pw->pw_dir) + '/') : QString();
}

} // namespace

QString psAppDataPath() {
	// Previously we used ~/.TelegramDesktop, so look there first.
	// If we find data there, we should still use it.
	auto home = getHomeDir();
	if (!home.isEmpty()) {
		auto oldPath = home + qsl(".TelegramDesktop/");
		auto oldSettingsBase = oldPath + qsl("tdata/settings");
		if (QFile(oldSettingsBase + '0').exists()
			|| QFile(oldSettingsBase + '1').exists()
			|| QFile(oldSettingsBase + 's').exists()) {
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
	PlatformThemes = QString::fromUtf8(qgetenv("QT_QPA_PLATFORMTHEME"))
		.split(':', QString::SkipEmptyParts);

	LOG(("Launcher filename: %1").arg(GetLauncherFilename()));

#ifdef TDESKTOP_USE_FONTCONFIG_FALLBACK
	FallbackFontConfig();
#endif // TDESKTOP_USE_FONTCONFIG_FALLBACK

	qputenv("PULSE_PROP_application.name", AppName.utf8());
	qputenv("PULSE_PROP_application.icon_name", GetIconName().toLatin1());

	// if gtk integration and qgtk3/qgtk2 platformtheme (or qgtk2 style)
	// is used at the same time, the app will crash
	if (UseGtkIntegration()
		&& !IsStaticBinary()
		&& !qEnvironmentVariableIsSet(
			"TDESKTOP_I_KNOW_ABOUT_GTK_INCOMPATIBILITY")) {
		qunsetenv("QT_QPA_PLATFORMTHEME");
		qunsetenv("QT_STYLE_OVERRIDE");
	}

	if(IsStaticBinary()
		|| InAppImage()
		|| InFlatpak()
		|| InSnap()
		|| IsQtPluginsBundled()) {
		qputenv("QT_WAYLAND_DECORATION", "material");
	}

	if((IsStaticBinary()
		|| InAppImage()
		|| InSnap()
		|| UseGtkFileDialog()
		|| IsQtPluginsBundled())
		&& !InFlatpak()) {
		LOG(("Checking for XDG Desktop Portal..."));
		// this can give us a chance to use a proper file dialog for current session
		if (IsXDGDesktopPortalPresent()) {
			LOG(("XDG Desktop Portal is present!"));
			if (UseXDGDesktopPortal()) {
				LOG(("Using XDG Desktop Portal."));
				qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
			} else {
				LOG(("Not using XDG Desktop Portal."));
			}
		} else {
			LOG(("XDG Desktop Portal is not present :("));
		}
	}
}

void finish() {
}

void RegisterCustomScheme(bool force) {
#ifndef TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
	const auto home = getHomeDir();
	if (home.isEmpty() || cExeName().isEmpty())
		return;

	static const auto DisabledByEnv = qEnvironmentVariableIsSet(
		"TDESKTOP_DISABLE_DESKTOP_FILE_GENERATION");

	// don't update desktop file for alpha version or if updater is disabled
	if ((cAlphaVersion() || Core::UpdaterDisabled() || DisabledByEnv)
		&& !force)
		return;

	const auto applicationsPath = QStandardPaths::writableLocation(
		QStandardPaths::ApplicationsLocation) + '/';

	GenerateDesktopFile(applicationsPath, qsl("-- %u"));

	const auto icons =
		QStandardPaths::writableLocation(
			QStandardPaths::GenericDataLocation)
			+ qsl("/icons/");

	if (!QDir(icons).exists()) QDir().mkpath(icons);

	const auto icon = icons + qsl("telegram.png");
	auto iconExists = QFile(icon).exists();
	if (Local::oldSettingsVersion() < 10021 && iconExists) {
		// Icon was changed.
		if (QFile(icon).remove()) {
			iconExists = false;
		}
	}
	if (!iconExists) {
		if (QFile(qsl(":/gui/art/logo_256.png")).copy(icon)) {
			DEBUG_LOG(("App Info: Icon copied to '%1'").arg(icon));
		}
	}

	RunShellCommand("update-desktop-database "
		+ EscapeShell(QFile::encodeName(applicationsPath)));

	RunShellCommand("xdg-mime default "
		+ GetLauncherFilename().toLatin1()
		+ " x-scheme-handler/tg");
#endif // !TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
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
		auto options = std::vector<QString>();
		const auto add = [&](const char *option) {
			options.emplace_back(option);
		};
		if (DesktopEnvironment::IsUnity()) {
			add("unity-control-center sound");
		} else if (DesktopEnvironment::IsKDE()) {
			add("kcmshell5 kcm_pulseaudio");
			add("kcmshell4 phonon");
		} else if (DesktopEnvironment::IsGnome()) {
			add("gnome-control-center sound");
		} else if (DesktopEnvironment::IsCinnamon()) {
			add("cinnamon-settings sound");
		} else if (DesktopEnvironment::IsMATE()) {
			add("mate-volume-control");
		}
		add("pavucontrol-qt");
		add("pavucontrol");
		add("alsamixergui");
		return ranges::any_of(options, [](const QString &command) {
			return QProcess::startDetached(command);
		});
	}
	return true;
}

namespace ThirdParty {

void start() {
	Libs::start();
	MainWindow::LibsLoaded();
}

void finish() {
}

} // namespace ThirdParty

} // namespace Platform

void psNewVersion() {
	Platform::RegisterCustomScheme();
}

bool psShowOpenWithMenu(int x, int y, const QString &file) {
	return false;
}

void psAutoStart(bool start, bool silent) {
	const auto home = getHomeDir();
	if (home.isEmpty() || cExeName().isEmpty())
		return;

	if (InFlatpak()) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
		PortalAutostart(start, silent);
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
	} else {
		const auto autostart = [&] {
			if (InSnap()) {
				QDir realHomeDir(home);
				realHomeDir.cd(qsl("../../.."));

				return realHomeDir
					.absoluteFilePath(qsl(".config/autostart/"));
			} else {
				return QStandardPaths::writableLocation(
					QStandardPaths::GenericConfigLocation)
					+ qsl("/autostart/");
			}
		}();

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
