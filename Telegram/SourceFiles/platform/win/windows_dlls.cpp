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

#include <d3d11.h>

#define LOAD_SYMBOL(lib, name) ::base::Platform::LoadMethod(lib, #name, name)

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
		u"d3d11.dll"_q,
		u"dxgi.dll"_q,
	};
	for (const auto &lib : list) {
		SafeLoadLibrary(lib);
	}
}

// D3D11.DLL

HRESULT (__stdcall *D3D11CreateDevice)(
	_In_opt_ IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	_In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	_COM_Outptr_opt_ ID3D11Device** ppDevice,
	_Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
	_COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext);

// DXGI.DLL

HRESULT (__stdcall *CreateDXGIFactory1)(
	REFIID riid,
	_COM_Outptr_ void **ppFactory);

void start() {
	init();

	const auto LibShell32 = SafeLoadLibrary(u"shell32.dll"_q);
	LOAD_SYMBOL(LibShell32, SHAssocEnumHandlers);
	LOAD_SYMBOL(LibShell32, SHCreateItemFromParsingName);
	LOAD_SYMBOL(LibShell32, SHOpenWithDialog);
	LOAD_SYMBOL(LibShell32, OpenAs_RunDLL);
	LOAD_SYMBOL(LibShell32, SHQueryUserNotificationState);
	LOAD_SYMBOL(LibShell32, SHChangeNotify);
	LOAD_SYMBOL(LibShell32, SetCurrentProcessExplicitAppUserModelID);

	const auto LibUxTheme = SafeLoadLibrary(u"uxtheme.dll"_q);
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

	if (IsWindowsVistaOrGreater()) {
		const auto LibWtsApi32 = SafeLoadLibrary(u"wtsapi32.dll"_q);
		LOAD_SYMBOL(LibWtsApi32, WTSRegisterSessionNotification);
		LOAD_SYMBOL(LibWtsApi32, WTSUnRegisterSessionNotification);

		const auto LibPropSys = SafeLoadLibrary(u"propsys.dll"_q);
		LOAD_SYMBOL(LibPropSys, PropVariantToString);
		LOAD_SYMBOL(LibPropSys, PSStringFromPropertyKey);

		const auto LibDwmApi = SafeLoadLibrary(u"dwmapi.dll"_q);
		LOAD_SYMBOL(LibDwmApi, DwmIsCompositionEnabled);
		LOAD_SYMBOL(LibDwmApi, DwmSetWindowAttribute);
	}

	const auto LibPsApi = SafeLoadLibrary(u"psapi.dll"_q);
	LOAD_SYMBOL(LibPsApi, GetProcessMemoryInfo);

	const auto LibUser32 = SafeLoadLibrary(u"user32.dll"_q);
	LOAD_SYMBOL(LibUser32, SetWindowCompositionAttribute);

	const auto LibD3D11 = SafeLoadLibrary(u"d3d11.dll"_q);
	LOAD_SYMBOL(LibD3D11, D3D11CreateDevice);

	const auto LibDXGI = SafeLoadLibrary(u"dxgi.dll"_q);
	LOAD_SYMBOL(LibDXGI, CreateDXGIFactory1);
}

} // namespace Dlls
} // namespace Platform

HRESULT WINAPI D3D11CreateDevice(
		_In_opt_ IDXGIAdapter* pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software,
		UINT Flags,
		_In_reads_opt_(FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		_COM_Outptr_opt_ ID3D11Device** ppDevice,
		_Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
		_COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext) {
	return Platform::Dlls::D3D11CreateDevice
		? Platform::Dlls::D3D11CreateDevice(
			pAdapter,
			DriverType,
			Software,
			Flags,
			pFeatureLevels,
			FeatureLevels,
			SDKVersion,
			ppDevice,
			pFeatureLevel,
			ppImmediateContext)
		: S_FALSE;
}

HRESULT WINAPI CreateDXGIFactory1(
		REFIID riid,
		_COM_Outptr_ void **ppFactory) {
	return Platform::Dlls::CreateDXGIFactory1
		? Platform::Dlls::CreateDXGIFactory1(riid, ppFactory)
		: S_FALSE;
}
