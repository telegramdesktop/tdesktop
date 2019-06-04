/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_info.h"

namespace Platform {

inline constexpr bool IsWindows() {
	return true;
}

inline constexpr bool IsWindowsStoreBuild() {
#ifdef OS_WIN_STORE
	return true;
#else // OS_WIN_STORE
	return false;
#endif // OS_WIN_STORE
}

inline constexpr bool IsMac() { return false; }
inline constexpr bool IsMacOldBuild() { return false; }
inline constexpr bool IsMacStoreBuild() { return false; }
inline bool IsMac10_6OrGreater() { return false; }
inline bool IsMac10_7OrGreater() { return false; }
inline bool IsMac10_8OrGreater() { return false; }
inline bool IsMac10_9OrGreater() { return false; }
inline bool IsMac10_10OrGreater() { return false; }
inline bool IsMac10_11OrGreater() { return false; }
inline bool IsMac10_12OrGreater() { return false; }
inline bool IsMac10_13OrGreater() { return false; }
inline bool IsMac10_14OrGreater() { return false; }
inline constexpr bool IsLinux() { return false; }
inline constexpr bool IsLinux32Bit() { return false; }
inline constexpr bool IsLinux64Bit() { return false; }

} // namespace Platform
