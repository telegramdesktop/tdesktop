/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_dlls.h"

namespace Platform {
namespace Dlls {

f_SetDllDirectory SetDllDirectory;

HINSTANCE LibKernel32;

void init() {
	static bool inited = false;
	if (inited) return;
	inited = true;

	LibKernel32 = LoadLibrary(L"KERNEL32.DLL");
	load(LibKernel32, "SetDllDirectoryW", SetDllDirectory);
	if (SetDllDirectory) {
		// Remove the current directory from the DLL search order.
		SetDllDirectory(L"");
	}
}

f_SetWindowTheme SetWindowTheme;
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

HINSTANCE LibUxTheme;
HINSTANCE LibShell32;
HINSTANCE LibWtsApi32;
HINSTANCE LibPropSys;
HINSTANCE LibComBase;
HINSTANCE LibDwmApi;

void start() {
	init();

	LibShell32 = LoadLibrary(L"SHELL32.DLL");
	load(LibShell32, "SHAssocEnumHandlers", SHAssocEnumHandlers);
	load(LibShell32, "SHCreateItemFromParsingName", SHCreateItemFromParsingName);
	load(LibShell32, "SHOpenWithDialog", SHOpenWithDialog);
	load(LibShell32, "OpenAs_RunDLLW", OpenAs_RunDLL);
	load(LibShell32, "SHQueryUserNotificationState", SHQueryUserNotificationState);
	load(LibShell32, "SHChangeNotify", SHChangeNotify);
	load(LibShell32, "SetCurrentProcessExplicitAppUserModelID", SetCurrentProcessExplicitAppUserModelID);

	if (cBetaVersion() == 10020001 && SHChangeNotify) { // Temp - app icon was changed
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	}

	LibUxTheme = LoadLibrary(L"UXTHEME.DLL");
	load(LibUxTheme, "SetWindowTheme", SetWindowTheme);

	auto version = QSysInfo::windowsVersion();
	if (version >= QSysInfo::WV_VISTA) {
		LibWtsApi32 = LoadLibrary(L"WTSAPI32.DLL");
		load(LibWtsApi32, "WTSRegisterSessionNotification", WTSRegisterSessionNotification);
		load(LibWtsApi32, "WTSUnRegisterSessionNotification", WTSUnRegisterSessionNotification);

		LibPropSys = LoadLibrary(L"PROPSYS.DLL");
		load(LibPropSys, "PropVariantToString", PropVariantToString);
		load(LibPropSys, "PSStringFromPropertyKey", PSStringFromPropertyKey);

		if (version >= QSysInfo::WV_WINDOWS8) {
			LibComBase = LoadLibrary(L"COMBASE.DLL");
			load(LibComBase, "RoGetActivationFactory", RoGetActivationFactory);
			load(LibComBase, "WindowsCreateStringReference", WindowsCreateStringReference);
			load(LibComBase, "WindowsDeleteString", WindowsDeleteString);
		}

		LibDwmApi = LoadLibrary(L"DWMAPI.DLL");
		load(LibDwmApi, "DwmIsCompositionEnabled", DwmIsCompositionEnabled);
	}
}

} // namespace Dlls
} // namespace Platform
