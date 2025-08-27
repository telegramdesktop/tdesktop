/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_shlobj_h.h"

#include <windows.h>
#include <shellapi.h>
#include <ShellScalingApi.h>
#include <dwmapi.h>
#include <RestartManager.h>
#include <psapi.h>

namespace Platform {
namespace Dlls {

void CheckLoadedModules();

//inline void(__stdcall *RefreshImmersiveColorPolicyState)();
//
//inline BOOL(__stdcall *AllowDarkModeForApp)(BOOL allow);
//
//enum class PreferredAppMode {
//	Default,
//	AllowDark,
//	ForceDark,
//	ForceLight,
//	Max
//};
//
//inline PreferredAppMode(__stdcall *SetPreferredAppMode)(
//	PreferredAppMode appMode);
//inline BOOL(__stdcall *AllowDarkModeForWindow)(HWND hwnd, BOOL allow);
//inline void(__stdcall *FlushMenuThemes)();

// SHELL32.DLL
inline HRESULT(__stdcall *SHAssocEnumHandlers)(
	PCWSTR pszExtra,
	ASSOC_FILTER afFilter,
	IEnumAssocHandlers **ppEnumHandler);
inline HRESULT(__stdcall *SHCreateItemFromParsingName)(
	PCWSTR pszPath,
	IBindCtx *pbc,
	REFIID riid,
	void **ppv);
inline HRESULT(__stdcall *SHOpenWithDialog)(
	HWND hwndParent,
	const OPENASINFO *poainfo);
inline HRESULT(__stdcall *OpenAs_RunDLL)(
	HWND hWnd,
	HINSTANCE hInstance,
	LPCWSTR lpszCmdLine,
	int nCmdShow);
inline HRESULT(__stdcall *SHQueryUserNotificationState)(
	QUERY_USER_NOTIFICATION_STATE *pquns);
inline void(__stdcall *SHChangeNotify)(
	LONG wEventId,
	UINT uFlags,
	__in_opt LPCVOID dwItem1,
	__in_opt LPCVOID dwItem2);

// PROPSYS.DLL

inline HRESULT(__stdcall *PSStringFromPropertyKey)(
	_In_ REFPROPERTYKEY pkey,
	_Out_writes_(cch) LPWSTR psz,
	_In_ UINT cch);

// PSAPI.DLL

inline BOOL(__stdcall *GetProcessMemoryInfo)(
	HANDLE Process,
	PPROCESS_MEMORY_COUNTERS ppsmemCounters,
	DWORD cb);

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

inline BOOL(__stdcall *SetWindowCompositionAttribute)(
	HWND hWnd,
	WINDOWCOMPOSITIONATTRIBDATA*);

// SHCORE.DLL
inline HRESULT(__stdcall *GetDpiForMonitor)(
	_In_ HMONITOR hmonitor,
	_In_ MONITOR_DPI_TYPE dpiType,
	_Out_ UINT *dpiX,
	_Out_ UINT *dpiY);

} // namespace Dlls
} // namespace Platform
