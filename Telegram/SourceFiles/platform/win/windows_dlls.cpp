/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_dlls.h"

#include "base/platform/win/base_windows_safe_library.h"
#include "ui/gl/gl_detection.h"

#include <VersionHelpers.h>
#include <QtCore/QSysInfo>

#define LOAD_SYMBOL(lib, name) ::base::Platform::LoadMethod(lib, #name, name)

#ifdef DESKTOP_APP_USE_ANGLE
bool DirectXResolveCompiler();
#endif // DESKTOP_APP_USE_ANGLE

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

	//if (IsWindows10OrGreater()) {
	//	static const auto kSystemVersion = QOperatingSystemVersion::current();
	//	static const auto kMinor = kSystemVersion.minorVersion();
	//	static const auto kBuild = kSystemVersion.microVersion();
	//	if (kMinor > 0 || (kMinor == 0 && kBuild >= 17763)) {
	//		const auto LibUxTheme = LoadLibrary(L"uxtheme.dll");
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

	const auto LibPropSys = LoadLibrary(L"propsys.dll");
	LOAD_SYMBOL(LibPropSys, PSStringFromPropertyKey);

	const auto LibPsApi = LoadLibrary(L"psapi.dll");
	LOAD_SYMBOL(LibPsApi, GetProcessMemoryInfo);

	const auto LibUser32 = LoadLibrary(L"user32.dll");
	LOAD_SYMBOL(LibUser32, SetWindowCompositionAttribute);

	const auto LibShCore = LoadLibrary(L"Shcore.dll");
	LOAD_SYMBOL(LibShCore, GetDpiForMonitor);
}

SafeIniter kSafeIniter;

} // namespace

void CheckLoadedModules() {
#ifdef DESKTOP_APP_USE_ANGLE
	if (DirectXResolveCompiler()) {
		auto LibD3DCompiler = HMODULE();
		if (GetModuleHandleEx(0, L"d3dcompiler_47.dll", &LibD3DCompiler)) {
			constexpr auto kMaxPathLong = 32767;
			auto path = std::array<WCHAR, kMaxPathLong + 1>{ 0 };
			const auto length = GetModuleFileName(
				LibD3DCompiler,
				path.data(),
				kMaxPathLong);
			if (length > 0 && length < kMaxPathLong) {
				LOG(("Using DirectX compiler '%1'."
					).arg(QString::fromWCharArray(path.data())));
			} else {
				LOG(("Error: Could not resolve DirectX compiler path."));
			}
		} else {
			LOG(("Error: Could not resolve DirectX compiler module."));
		}
	} else {
		LOG(("Error: Could not resolve DirectX compiler library."));
	}
#endif // DESKTOP_APP_USE_ANGLE
}

} // namespace Dlls
} // namespace Platform
