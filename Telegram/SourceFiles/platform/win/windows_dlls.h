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

// RSTRTMGR.DLL

using f_RmStartSession = DWORD(FAR STDAPICALLTYPE*)(
	_Out_ DWORD *pSessionHandle,
	_Reserved_ DWORD dwSessionFlags,
	_Out_writes_(CCH_RM_SESSION_KEY + 1) WCHAR strSessionKey[]);
extern f_RmStartSession RmStartSession;

using f_RmRegisterResources = DWORD(FAR STDAPICALLTYPE*)(
	_In_ DWORD dwSessionHandle,
	_In_ UINT nFiles,
	_In_reads_opt_(nFiles) LPCWSTR rgsFileNames[],
	_In_ UINT nApplications,
	_In_reads_opt_(nApplications) RM_UNIQUE_PROCESS rgApplications[],
	_In_ UINT nServices,
	_In_reads_opt_(nServices) LPCWSTR rgsServiceNames[]);
extern f_RmRegisterResources RmRegisterResources;

using f_RmGetList = DWORD(FAR STDAPICALLTYPE*)(
	_In_ DWORD dwSessionHandle,
	_Out_ UINT *pnProcInfoNeeded,
	_Inout_ UINT *pnProcInfo,
	_Inout_updates_opt_(*pnProcInfo) RM_PROCESS_INFO rgAffectedApps[],
	_Out_ LPDWORD lpdwRebootReasons);
extern f_RmGetList RmGetList;

using f_RmShutdown = DWORD(FAR STDAPICALLTYPE*)(
	_In_ DWORD dwSessionHandle,
	_In_ ULONG lActionFlags,
	_In_opt_ RM_WRITE_STATUS_CALLBACK fnStatus);
extern f_RmShutdown RmShutdown;

using f_RmEndSession = DWORD(FAR STDAPICALLTYPE*)(
	_In_ DWORD dwSessionHandle);
extern f_RmEndSession RmEndSession;

// PSAPI.DLL

using f_GetProcessMemoryInfo = BOOL(FAR STDAPICALLTYPE*)(
	HANDLE Process,
	PPROCESS_MEMORY_COUNTERS ppsmemCounters,
	DWORD cb);
extern f_GetProcessMemoryInfo GetProcessMemoryInfo;

} // namespace Dlls
} // namespace Platform
