/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_dlls.h"

#include "base/platform/win/base_windows_safe_library.h"

#include <VersionHelpers.h>
#include <QtCore/QSysInfo>

#define LOAD_SYMBOL(lib, name) ::base::Platform::LoadMethod(lib, #name, name)

namespace Platform {
namespace Dlls {
namespace {

struct SafeIniter {
	SafeIniter();
};

SafeIniter::SafeIniter() {
	base::Platform::InitDynamicLibraries();

	const auto LibShell32 = LoadLibrary(L"shell32.dll");
	LOAD_SYMBOL(LibShell32, SHAssocEnumHandlers);
	LOAD_SYMBOL(LibShell32, SHCreateItemFromParsingName);
	LOAD_SYMBOL(LibShell32, SHOpenWithDialog);
	LOAD_SYMBOL(LibShell32, OpenAs_RunDLL);
	LOAD_SYMBOL(LibShell32, SHQueryUserNotificationState);
	LOAD_SYMBOL(LibShell32, SHChangeNotify);
	LOAD_SYMBOL(LibShell32, SetCurrentProcessExplicitAppUserModelID);

	const auto LibUxTheme = LoadLibrary(L"uxtheme.dll");
	LOAD_SYMBOL(LibUxTheme, SetWindowTheme);
	//if (IsWindows10OrGreater()) {
	//	static const auto kSystemVersion = QOperatingSystemVersion::current();
	//	static const auto kMinor = kSystemVersion.minorVersion();
	//	static const auto kBuild = kSystemVersion.microVersion();
	//	if (kMinor > 0 || (kMinor == 0 && kBuild >= 17763)) {
	//		if (kBuild < 18362) {
	//			LOAD_SYMBOL(LibUxTheme, AllowDarkModeForApp, 135);
	//		} else {
	//			LOAD_SYMBOL(LibUxTheme, SetPreferredAppMode, 135);
	//		}
	//		LOAD_SYMBOL(LibUxTheme, AllowDarkModeForWindow, 133);
	//		LOAD_SYMBOL(LibUxTheme, RefreshImmersiveColorPolicyState, 104);
	//		LOAD_SYMBOL(LibUxTheme, FlushMenuThemes, 136);
	//	}
	//}

	const auto LibWtsApi32 = LoadLibrary(L"wtsapi32.dll");
	LOAD_SYMBOL(LibWtsApi32, WTSRegisterSessionNotification);
	LOAD_SYMBOL(LibWtsApi32, WTSUnRegisterSessionNotification);

	const auto LibPropSys = LoadLibrary(L"propsys.dll");
	LOAD_SYMBOL(LibPropSys, PropVariantToString);
	LOAD_SYMBOL(LibPropSys, PSStringFromPropertyKey);

	const auto LibDwmApi = LoadLibrary(L"dwmapi.dll");
	LOAD_SYMBOL(LibDwmApi, DwmIsCompositionEnabled);
	LOAD_SYMBOL(LibDwmApi, DwmSetWindowAttribute);

	const auto LibPsApi = LoadLibrary(L"psapi.dll");
	LOAD_SYMBOL(LibPsApi, GetProcessMemoryInfo);

	const auto LibUser32 = LoadLibrary(L"user32.dll");
	LOAD_SYMBOL(LibUser32, SetWindowCompositionAttribute);
}

SafeIniter kSafeIniter;

} // namespace
} // namespace Dlls
} // namespace Platform
