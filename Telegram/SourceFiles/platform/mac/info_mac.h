/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_info.h"

namespace Platform {

inline constexpr bool IsMac() {
	return true;
}

inline constexpr bool IsMacOldBuild() {
#ifdef OS_MAC_OLD
	return true;
#else // OS_MAC_OLD
	return false;
#endif // OS_MAC_OLD
}

inline constexpr bool IsMacStoreBuild() {
#ifdef OS_MAC_STORE
	return true;
#else // OS_MAC_STORE
	return false;
#endif // OS_MAC_STORE
}

inline constexpr bool IsWindows() { return false; }
inline constexpr bool IsWindowsStoreBuild() { return false; }
inline bool IsWindowsXPOrGreater() { return false; }
inline bool IsWindowsVistaOrGreater() { return false; }
inline bool IsWindows7OrGreater() { return false; }
inline bool IsWindows8OrGreater() { return false; }
inline bool IsWindows8Point1OrGreater() { return false; }
inline bool IsWindows10OrGreater() { return false; }
inline constexpr bool IsLinux() { return false; }
inline constexpr bool IsLinux32Bit() { return false; }
inline constexpr bool IsLinux64Bit() { return false; }

} // namespace Platform
