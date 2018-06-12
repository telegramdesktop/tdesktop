/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <windows.h>
#include <shlobj.h>
#include <roapi.h>

namespace Platform {
namespace Dlls {

void init();

// KERNEL32.DLL
typedef BOOL (FAR STDAPICALLTYPE *f_SetDllDirectory)(LPCWSTR lpPathName);
extern f_SetDllDirectory SetDllDirectory;

void start();

template <typename Function>
bool load(HINSTANCE library, LPCSTR name, Function &func) {
	if (!library) return false;

	func = reinterpret_cast<Function>(GetProcAddress(library, name));
	return (func != nullptr);
}

// UXTHEME.DLL
typedef HRESULT (FAR STDAPICALLTYPE *f_SetWindowTheme)(HWND hWnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);
extern f_SetWindowTheme SetWindowTheme;

// SHELL32.DLL
typedef HRESULT (FAR STDAPICALLTYPE *f_SHAssocEnumHandlers)(PCWSTR pszExtra, ASSOC_FILTER afFilter, IEnumAssocHandlers **ppEnumHandler);
extern f_SHAssocEnumHandlers SHAssocEnumHandlers;

typedef HRESULT (FAR STDAPICALLTYPE *f_SHCreateItemFromParsingName)(PCWSTR pszPath, IBindCtx *pbc, REFIID riid, void **ppv);
extern f_SHCreateItemFromParsingName SHCreateItemFromParsingName;

typedef HRESULT (FAR STDAPICALLTYPE *f_SHOpenWithDialog)(HWND hwndParent, const OPENASINFO *poainfo);
extern f_SHOpenWithDialog SHOpenWithDialog;

typedef HRESULT (FAR STDAPICALLTYPE *f_OpenAs_RunDLL)(HWND hWnd, HINSTANCE hInstance, LPCWSTR lpszCmdLine, int nCmdShow);
extern f_OpenAs_RunDLL OpenAs_RunDLL;

typedef HRESULT (FAR STDAPICALLTYPE *f_SHQueryUserNotificationState)(QUERY_USER_NOTIFICATION_STATE *pquns);
extern f_SHQueryUserNotificationState SHQueryUserNotificationState;

typedef void (FAR STDAPICALLTYPE *f_SHChangeNotify)(LONG wEventId, UINT uFlags, __in_opt LPCVOID dwItem1, __in_opt LPCVOID dwItem2);
extern f_SHChangeNotify SHChangeNotify;

typedef HRESULT (FAR STDAPICALLTYPE *f_SetCurrentProcessExplicitAppUserModelID)(__in PCWSTR AppID);
extern f_SetCurrentProcessExplicitAppUserModelID SetCurrentProcessExplicitAppUserModelID;

// WTSAPI32.DLL

typedef BOOL (FAR STDAPICALLTYPE *f_WTSRegisterSessionNotification)(HWND hWnd, DWORD dwFlags);
extern f_WTSRegisterSessionNotification WTSRegisterSessionNotification;

typedef BOOL (FAR STDAPICALLTYPE *f_WTSUnRegisterSessionNotification)(HWND hWnd);
extern f_WTSUnRegisterSessionNotification WTSUnRegisterSessionNotification;

// PROPSYS.DLL

typedef HRESULT (FAR STDAPICALLTYPE *f_PropVariantToString)(_In_ REFPROPVARIANT propvar, _Out_writes_(cch) PWSTR psz, _In_ UINT cch);
extern f_PropVariantToString PropVariantToString;

typedef HRESULT (FAR STDAPICALLTYPE *f_PSStringFromPropertyKey)(_In_ REFPROPERTYKEY pkey, _Out_writes_(cch) LPWSTR psz, _In_ UINT cch);
extern f_PSStringFromPropertyKey PSStringFromPropertyKey;

// COMBASE.DLL

typedef HRESULT (FAR STDAPICALLTYPE *f_RoGetActivationFactory)(_In_ HSTRING activatableClassId, _In_ REFIID iid, _COM_Outptr_ void ** factory);
extern f_RoGetActivationFactory RoGetActivationFactory;

typedef HRESULT (FAR STDAPICALLTYPE *f_WindowsCreateStringReference)(_In_reads_opt_(length + 1) PCWSTR sourceString, UINT32 length, _Out_ HSTRING_HEADER * hstringHeader, _Outptr_result_maybenull_ _Result_nullonfailure_ HSTRING * string);
extern f_WindowsCreateStringReference WindowsCreateStringReference;

typedef HRESULT (FAR STDAPICALLTYPE *f_WindowsDeleteString)(_In_opt_ HSTRING string);
extern f_WindowsDeleteString WindowsDeleteString;



} // namespace Dlls
} // namespace Platform
