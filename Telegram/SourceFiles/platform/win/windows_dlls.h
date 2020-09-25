/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_h.h"

#include <shlobj.h>
#include <roapi.h>
#include <dwmapi.h>
#include <RestartManager.h>
#include <psapi.h>

#ifdef __MINGW32__
#define __in
#endif

namespace Platform {
namespace Dlls {

void init();

// KERNEL32.DLL
using f_SetDllDirectory = BOOL(FAR STDAPICALLTYPE*)(LPCWSTR lpPathName);
extern f_SetDllDirectory SetDllDirectory;

void start();

// UXTHEME.DLL
using f_SetWindowTheme = HRESULT(FAR STDAPICALLTYPE*)(
	HWND hWnd,
	LPCWSTR pszSubAppName,
	LPCWSTR pszSubIdList);
extern f_SetWindowTheme SetWindowTheme;

//using f_RefreshImmersiveColorPolicyState = void(FAR STDAPICALLTYPE*)();
//extern f_RefreshImmersiveColorPolicyState RefreshImmersiveColorPolicyState;
//
//using f_AllowDarkModeForApp = BOOL(FAR STDAPICALLTYPE*)(BOOL allow);
//extern f_AllowDarkModeForApp AllowDarkModeForApp;
//
//enum class PreferredAppMode {
//	Default,
//	AllowDark,
//	ForceDark,
//	ForceLight,
//	Max
//};
//
//using f_SetPreferredAppMode = PreferredAppMode(FAR STDAPICALLTYPE*)(PreferredAppMode appMode);
//extern f_SetPreferredAppMode SetPreferredAppMode;
//
//using f_AllowDarkModeForWindow = BOOL(FAR STDAPICALLTYPE*)(HWND hwnd, BOOL allow);
//extern f_AllowDarkModeForWindow AllowDarkModeForWindow;
//
//using f_FlushMenuThemes = void(FAR STDAPICALLTYPE*)();
//extern f_FlushMenuThemes FlushMenuThemes;

// SHELL32.DLL
using f_SHAssocEnumHandlers = HRESULT(FAR STDAPICALLTYPE*)(
	PCWSTR pszExtra,
	ASSOC_FILTER afFilter,
	IEnumAssocHandlers **ppEnumHandler);
extern f_SHAssocEnumHandlers SHAssocEnumHandlers;

using f_SHCreateItemFromParsingName = HRESULT(FAR STDAPICALLTYPE*)(
	PCWSTR pszPath,
	IBindCtx *pbc,
	REFIID riid,
	void **ppv);
extern f_SHCreateItemFromParsingName SHCreateItemFromParsingName;

using f_SHOpenWithDialog = HRESULT(FAR STDAPICALLTYPE*)(
	HWND hwndParent,
	const OPENASINFO *poainfo);
extern f_SHOpenWithDialog SHOpenWithDialog;

using f_OpenAs_RunDLL = HRESULT(FAR STDAPICALLTYPE*)(
	HWND hWnd,
	HINSTANCE hInstance,
	LPCWSTR lpszCmdLine,
	int nCmdShow);
extern f_OpenAs_RunDLL OpenAs_RunDLL;

using f_SHQueryUserNotificationState = HRESULT(FAR STDAPICALLTYPE*)(
	QUERY_USER_NOTIFICATION_STATE *pquns);
extern f_SHQueryUserNotificationState SHQueryUserNotificationState;

using f_SHChangeNotify = void(FAR STDAPICALLTYPE*)(
	LONG wEventId,
	UINT uFlags,
	__in_opt LPCVOID dwItem1,
	__in_opt LPCVOID dwItem2);
extern f_SHChangeNotify SHChangeNotify;

using f_SetCurrentProcessExplicitAppUserModelID
	= HRESULT(FAR STDAPICALLTYPE*)(__in PCWSTR AppID);
extern f_SetCurrentProcessExplicitAppUserModelID SetCurrentProcessExplicitAppUserModelID;

// WTSAPI32.DLL

using f_WTSRegisterSessionNotification = BOOL(FAR STDAPICALLTYPE*)(
	HWND hWnd,
	DWORD dwFlags);
extern f_WTSRegisterSessionNotification WTSRegisterSessionNotification;

using f_WTSUnRegisterSessionNotification = BOOL(FAR STDAPICALLTYPE*)(
	HWND hWnd);
extern f_WTSUnRegisterSessionNotification WTSUnRegisterSessionNotification;

// PROPSYS.DLL

using f_PropVariantToString = HRESULT(FAR STDAPICALLTYPE*)(
	_In_ REFPROPVARIANT propvar,
	_Out_writes_(cch) PWSTR psz,
	_In_ UINT cch);
extern f_PropVariantToString PropVariantToString;

using f_PSStringFromPropertyKey = HRESULT(FAR STDAPICALLTYPE*)(
	_In_ REFPROPERTYKEY pkey,
	_Out_writes_(cch) LPWSTR psz,
	_In_ UINT cch);
extern f_PSStringFromPropertyKey PSStringFromPropertyKey;

// COMBASE.DLL

using f_RoGetActivationFactory = HRESULT(FAR STDAPICALLTYPE*)(
	_In_ HSTRING activatableClassId,
	_In_ REFIID iid,
	_COM_Outptr_ void ** factory);
extern f_RoGetActivationFactory RoGetActivationFactory;

using f_WindowsCreateStringReference = HRESULT(FAR STDAPICALLTYPE*)(
	_In_reads_opt_(length + 1) PCWSTR sourceString,
	UINT32 length,
	_Out_ HSTRING_HEADER * hstringHeader,
	_Outptr_result_maybenull_ _Result_nullonfailure_ HSTRING * string);
extern f_WindowsCreateStringReference WindowsCreateStringReference;

using f_WindowsDeleteString = HRESULT(FAR STDAPICALLTYPE*)(
	_In_opt_ HSTRING string);
extern f_WindowsDeleteString WindowsDeleteString;

// DWMAPI.DLL

using f_DwmIsCompositionEnabled = HRESULT(FAR STDAPICALLTYPE*)(
	_Out_ BOOL* pfEnabled);
extern f_DwmIsCompositionEnabled DwmIsCompositionEnabled;

using f_DwmSetWindowAttribute = HRESULT(FAR STDAPICALLTYPE*)(
	HWND hwnd,
	DWORD dwAttribute,
	_In_reads_bytes_(cbAttribute) LPCVOID pvAttribute,
	DWORD cbAttribute);
extern f_DwmSetWindowAttribute DwmSetWindowAttribute;

// PSAPI.DLL

using f_GetProcessMemoryInfo = BOOL(FAR STDAPICALLTYPE*)(
	HANDLE Process,
	PPROCESS_MEMORY_COUNTERS ppsmemCounters,
	DWORD cb);
extern f_GetProcessMemoryInfo GetProcessMemoryInfo;

// USER32.DLL

enum class WINDOWCOMPOSITIONATTRIB {
	WCA_UNDEFINED = 0,
	WCA_NCRENDERING_ENABLED = 1,
	WCA_NCRENDERING_POLICY = 2,
	WCA_TRANSITIONS_FORCEDISABLED = 3,
	WCA_ALLOW_NCPAINT = 4,
	WCA_CAPTION_BUTTON_BOUNDS = 5,
	WCA_NONCLIENT_RTL_LAYOUT = 6,
	WCA_FORCE_ICONIC_REPRESENTATION = 7,
	WCA_EXTENDED_FRAME_BOUNDS = 8,
	WCA_HAS_ICONIC_BITMAP = 9,
	WCA_THEME_ATTRIBUTES = 10,
	WCA_NCRENDERING_EXILED = 11,
	WCA_NCADORNMENTINFO = 12,
	WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
	WCA_VIDEO_OVERLAY_ACTIVE = 14,
	WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
	WCA_DISALLOW_PEEK = 16,
	WCA_CLOAK = 17,
	WCA_CLOAKED = 18,
	WCA_ACCENT_POLICY = 19,
	WCA_FREEZE_REPRESENTATION = 20,
	WCA_EVER_UNCLOAKED = 21,
	WCA_VISUAL_OWNER = 22,
	WCA_HOLOGRAPHIC = 23,
	WCA_EXCLUDED_FROM_DDA = 24,
	WCA_PASSIVEUPDATEMODE = 25,
	WCA_USEDARKMODECOLORS = 26,
	WCA_LAST = 27
};

struct WINDOWCOMPOSITIONATTRIBDATA {
	WINDOWCOMPOSITIONATTRIB Attrib;
	PVOID pvData;
	SIZE_T cbData;
};

using f_SetWindowCompositionAttribute = BOOL(WINAPI *)(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA*);
extern f_SetWindowCompositionAttribute SetWindowCompositionAttribute;

} // namespace Dlls
} // namespace Platform
