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

namespace Platform {
namespace Dlls {

using base::Platform::SafeLoadLibrary;
using base::Platform::LoadMethod;

void init() {
	static bool inited = false;
	if (inited) return;
	inited = true;

	// Remove the current directory from the DLL search order.
	::SetDllDirectory(L"");

	const auto list = {
		u"dbghelp.dll"_q,
		u"dbgcore.dll"_q,
		u"propsys.dll"_q,
		u"winsta.dll"_q,
		u"textinputframework.dll"_q,
		u"uxtheme.dll"_q,
		u"igdumdim32.dll"_q,
		u"amdhdl32.dll"_q,
		u"wtsapi32.dll"_q,
		u"propsys.dll"_q,
		u"combase.dll"_q,
		u"dwmapi.dll"_q,
		u"rstrtmgr.dll"_q,
		u"psapi.dll"_q,
		u"user32.dll"_q,
	};
	for (const auto &lib : list) {
		SafeLoadLibrary(lib);
	}
}

f_SetWindowTheme SetWindowTheme;
//f_RefreshImmersiveColorPolicyState RefreshImmersiveColorPolicyState;
//f_AllowDarkModeForApp AllowDarkModeForApp;
//f_SetPreferredAppMode SetPreferredAppMode;
//f_AllowDarkModeForWindow AllowDarkModeForWindow;
//f_FlushMenuThemes FlushMenuThemes;
f_OpenAs_RunDLL OpenAs_RunDLL;
f_SHOpenWithDialog SHOpenWithDialog;
f_SHAssocEnumHandlers SHAssocEnumHandlers;
f_SHCreateItemFromParsingName SHCreateItemFromParsingName;
f_WTSRegisterSessionNotification WTSRegisterSessionNotification;
f_WTSUnRegisterSessionNotification WTSUnRegisterSessionNotification;
f_SHQueryUserNotificationState SHQueryUserNotificationState;
f_SHChangeNotify SHChangeNotify;
f_SetCurrentProcessExplicitAppUserModelID SetCurrentProcessExplicitAppUserModelID;
f_RoGetActivationFactory RoGetActivationFactory;
f_WindowsCreateStringReference WindowsCreateStringReference;
f_WindowsDeleteString WindowsDeleteString;
f_PropVariantToString PropVariantToString;
f_PSStringFromPropertyKey PSStringFromPropertyKey;
f_DwmIsCompositionEnabled DwmIsCompositionEnabled;
f_DwmSetWindowAttribute DwmSetWindowAttribute;
f_RmStartSession RmStartSession;
f_RmRegisterResources RmRegisterResources;
f_RmGetList RmGetList;
f_RmShutdown RmShutdown;
f_RmEndSession RmEndSession;
f_GetProcessMemoryInfo GetProcessMemoryInfo;
f_SetWindowCompositionAttribute SetWindowCompositionAttribute;

void start() {
	init();

	const auto LibShell32 = SafeLoadLibrary(u"shell32.dll"_q);
	LoadMethod(LibShell32, "SHAssocEnumHandlers", SHAssocEnumHandlers);
	LoadMethod(LibShell32, "SHCreateItemFromParsingName", SHCreateItemFromParsingName);
	LoadMethod(LibShell32, "SHOpenWithDialog", SHOpenWithDialog);
	LoadMethod(LibShell32, "OpenAs_RunDLLW", OpenAs_RunDLL);
	LoadMethod(LibShell32, "SHQueryUserNotificationState", SHQueryUserNotificationState);
	LoadMethod(LibShell32, "SHChangeNotify", SHChangeNotify);
	LoadMethod(LibShell32, "SetCurrentProcessExplicitAppUserModelID", SetCurrentProcessExplicitAppUserModelID);

	const auto LibUxTheme = SafeLoadLibrary(u"uxtheme.dll"_q);
	LoadMethod(LibUxTheme, "SetWindowTheme", SetWindowTheme);
	//if (IsWindows10OrGreater()) {
	//	static const auto kSystemVersion = QOperatingSystemVersion::current();
	//	static const auto kMinor = kSystemVersion.minorVersion();
	//	static const auto kBuild = kSystemVersion.microVersion();
	//	if (kMinor > 0 || (kMinor == 0 && kBuild >= 17763)) {
	//		if (kBuild < 18362) {
	//			LoadMethod(LibUxTheme, "AllowDarkModeForApp", AllowDarkModeForApp, 135);
	//		} else {
	//			LoadMethod(LibUxTheme, "SetPreferredAppMode", SetPreferredAppMode, 135);
	//		}
	//		LoadMethod(LibUxTheme, "AllowDarkModeForWindow", AllowDarkModeForWindow, 133);
	//		LoadMethod(LibUxTheme, "RefreshImmersiveColorPolicyState", RefreshImmersiveColorPolicyState, 104);
	//		LoadMethod(LibUxTheme, "FlushMenuThemes", FlushMenuThemes, 136);
	//	}
	//}

	if (IsWindowsVistaOrGreater()) {
		const auto LibWtsApi32 = SafeLoadLibrary(u"wtsapi32.dll"_q);
		LoadMethod(LibWtsApi32, "WTSRegisterSessionNotification", WTSRegisterSessionNotification);
		LoadMethod(LibWtsApi32, "WTSUnRegisterSessionNotification", WTSUnRegisterSessionNotification);

		const auto LibPropSys = SafeLoadLibrary(u"propsys.dll"_q);
		LoadMethod(LibPropSys, "PropVariantToString", PropVariantToString);
		LoadMethod(LibPropSys, "PSStringFromPropertyKey", PSStringFromPropertyKey);

		if (IsWindows8OrGreater()) {
			const auto LibComBase = SafeLoadLibrary(u"combase.dll"_q);
			LoadMethod(LibComBase, "RoGetActivationFactory", RoGetActivationFactory);
			LoadMethod(LibComBase, "WindowsCreateStringReference", WindowsCreateStringReference);
			LoadMethod(LibComBase, "WindowsDeleteString", WindowsDeleteString);
		}

		const auto LibDwmApi = SafeLoadLibrary(u"dwmapi.dll"_q);
		LoadMethod(LibDwmApi, "DwmIsCompositionEnabled", DwmIsCompositionEnabled);
		LoadMethod(LibDwmApi, "DwmSetWindowAttribute", DwmSetWindowAttribute);

		const auto LibRstrtMgr = SafeLoadLibrary(u"rstrtmgr.dll"_q);
		LoadMethod(LibRstrtMgr, "RmStartSession", RmStartSession);
		LoadMethod(LibRstrtMgr, "RmRegisterResources", RmRegisterResources);
		LoadMethod(LibRstrtMgr, "RmGetList", RmGetList);
		LoadMethod(LibRstrtMgr, "RmShutdown", RmShutdown);
		LoadMethod(LibRstrtMgr, "RmEndSession", RmEndSession);
	}

	const auto LibPsApi = SafeLoadLibrary(u"psapi.dll"_q);
	LoadMethod(LibPsApi, "GetProcessMemoryInfo", GetProcessMemoryInfo);

	const auto LibUser32 = SafeLoadLibrary(u"user32.dll"_q);
	LoadMethod(LibUser32, "SetWindowCompositionAttribute", SetWindowCompositionAttribute);
}

} // namespace Dlls
} // namespace Platform
