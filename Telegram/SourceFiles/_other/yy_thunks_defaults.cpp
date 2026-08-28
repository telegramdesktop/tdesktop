/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <windows.h>

// YY-Thunks makes these hooks weak with closed tool we skip, so define defaults.
extern "C" {

// Keep the DLL preloading that a stock YY-Thunks build has enabled.
BOOL __YY_Thunks_Disable_Rreload_Dlls = FALSE;

// Const at namespace scope is internal linkage, so declare extern first.
extern HMODULE (__fastcall * const __pfnYY_Thunks_CustomLoadLibrary)(
	const wchar_t *moduleName,
	DWORD flags);
HMODULE (__fastcall * const __pfnYY_Thunks_CustomLoadLibrary)(
	const wchar_t *moduleName,
	DWORD flags) = nullptr;

// No custom entry point, so the original CRT startup is used.
extern BOOL (WINAPI * const __pfnDllMainCRTStartupForYY_Thunks)(
	HINSTANCE instance,
	DWORD reason,
	LPVOID reserved);
BOOL (WINAPI * const __pfnDllMainCRTStartupForYY_Thunks)(
	HINSTANCE instance,
	DWORD reason,
	LPVOID reserved) = nullptr;

} // extern "C"
