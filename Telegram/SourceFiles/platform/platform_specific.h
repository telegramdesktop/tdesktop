/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {
enum class QuitReason;
} // namespace Core

namespace Platform {

void start();
void finish();

enum class PermissionStatus {
	Granted,
	CanRequest,
	Denied,
};

enum class PermissionType {
	Microphone,
	Camera,
};

enum class SystemSettingsType {
	Audio,
};

void SetApplicationIcon(const QIcon &icon);
[[nodiscard]] QString SingleInstanceLocalServerName(const QString &hash);
[[nodiscard]] PermissionStatus GetPermissionStatus(PermissionType type);
void RequestPermission(PermissionType type, Fn<void(PermissionStatus)> resultCallback);
void OpenSystemSettingsForPermission(PermissionType type);
bool OpenSystemSettings(SystemSettingsType type);
void IgnoreApplicationActivationRightNow();
[[nodiscard]] bool AutostartSupported();
void AutostartRequestStateFromSystem(Fn<void(bool)> callback);
void AutostartToggle(bool enabled, Fn<void(bool)> done = nullptr);
[[nodiscard]] bool AutostartSkip();
[[nodiscard]] bool TrayIconSupported();
[[nodiscard]] bool SkipTaskbarSupported();
void WriteCrashDumpDetails();
void NewVersionLaunched(int oldVersion);
[[nodiscard]] QImage DefaultApplicationIcon();
[[nodiscard]] bool PreventsQuit(Core::QuitReason reason);
[[nodiscard]] QString ExecutablePathForShortcuts();

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
[[nodiscard]] std::optional<bool> IsDarkMode();
#endif // Qt < 6.5.0

namespace ThirdParty {

void start();
void finish();

} // namespace ThirdParty
} // namespace Platform

#ifdef Q_OS_WIN
#include "platform/win/specific_win.h"
#elif defined Q_OS_MAC // Q_OS_WIN
#include "platform/mac/specific_mac.h"
#else // Q_OS_WIN || Q_OS_MAC
#include "platform/linux/specific_linux.h"
#endif // else for Q_OS_WIN || Q_OS_MAC
