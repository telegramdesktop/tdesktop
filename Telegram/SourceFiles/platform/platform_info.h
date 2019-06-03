/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

[[nodiscard]] QString DeviceModelPretty();
[[nodiscard]] QString SystemVersionPretty();
[[nodiscard]] QString SystemCountry();
[[nodiscard]] QString SystemLanguage();
[[nodiscard]] QDate WhenSystemBecomesOutdated();

[[nodiscard]] constexpr bool IsWindows();
[[nodiscard]] constexpr bool IsWindowsStoreBuild();
[[nodiscard]] bool IsWindowsXPOrGreater();
[[nodiscard]] bool IsWindowsVistaOrGreater();
[[nodiscard]] bool IsWindows7OrGreater();
[[nodiscard]] bool IsWindows8OrGreater();
[[nodiscard]] bool IsWindows8Point1OrGreater();
[[nodiscard]] bool IsWindows10OrGreater();

[[nodiscard]] constexpr bool IsMac();
[[nodiscard]] constexpr bool IsMacOldBuild();
[[nodiscard]] constexpr bool IsMacStoreBuild();
[[nodiscard]] bool IsMac10_6OrGreater();
[[nodiscard]] bool IsMac10_7OrGreater();
[[nodiscard]] bool IsMac10_8OrGreater();
[[nodiscard]] bool IsMac10_9OrGreater();
[[nodiscard]] bool IsMac10_10OrGreater();
[[nodiscard]] bool IsMac10_11OrGreater();
[[nodiscard]] bool IsMac10_12OrGreater();
[[nodiscard]] bool IsMac10_13OrGreater();
[[nodiscard]] bool IsMac10_14OrGreater();

[[nodiscard]] constexpr bool IsLinux();
[[nodiscard]] constexpr bool IsLinux32Bit();
[[nodiscard]] constexpr bool IsLinux64Bit();

} // namespace Platform

#ifdef Q_OS_MAC
#include "platform/mac/info_mac.h"
#elif defined Q_OS_LINUX // Q_OS_MAC
#include "platform/linux/info_linux.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_LINUX
#include "platform/win/info_win.h"
#endif // Q_OS_MAC || Q_OS_LINUX || Q_OS_WIN
