/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/specific_win.h"

#include "platform/win/main_window_win.h"
#include "platform/win/notifications_manager_win.h"
#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/windows_dlls.h"
#include "platform/win/windows_autostart_task.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/win/base_windows_co_task_mem.h"
#include "base/platform/win/base_windows_shlobj_h.h"
#include "base/platform/win/base_windows_winrt.h"
#include "base/call_delayed.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "history/history_location_manager.h"
#include "storage/localstorage.h"
#include "core/application.h"
#include "window/window_controller.h"
#include "core/crash_reports.h"

#include <QtCore/QOperatingSystemVersion>
#include <QtWidgets/QApplication>
#include <QtGui/QDesktopServices>
#include <QtGui/QWindow>

#include <Shobjidl.h>
#include <ShObjIdl_core.h>
#include <shellapi.h>

#include <openssl/conf.h>
#include <openssl/engine.h>
#include <openssl/err.h>

#include <dbghelp.h>
#include <Shlwapi.h>
#include <Strsafe.h>
#include <Windowsx.h>
#include <WtsApi32.h>

#include <SDKDDKVer.h>

#include <sal.h>
#include <Psapi.h>
#include <strsafe.h>
#include <ObjBase.h>
#include <propvarutil.h>
#include <functiondiscoverykeys.h>
#include <intsafe.h>
#include <guiddef.h>
#include <locale.h>

#include <ShellScalingApi.h>

#ifndef DCX_USESTYLE
#define DCX_USESTYLE 0x00010000
#endif

#ifndef WM_NCPOINTERUPDATE
#define WM_NCPOINTERUPDATE 0x0241
#define WM_NCPOINTERDOWN 0x0242
#define WM_NCPOINTERUP 0x0243
#endif

using namespace ::Platform;

namespace {

bool themeInited = false;
bool finished = true;
QMargins simpleMargins, margins;
HICON bigIcon = 0, smallIcon = 0, overlayIcon = 0;

[[nodiscard]] uint64 WindowIdFromHWND(HWND value) {
	return (reinterpret_cast<uint64>(value) & 0xFFFFFFFFULL);
}

struct FindToActivateRequest {
	uint64 processId = 0;
	uint64 windowId = 0;
	HWND result = nullptr;
	uint32 resultLevel = 0; // Larger is better.
};

BOOL CALLBACK FindToActivate(HWND hwnd, LPARAM lParam) {
	const auto request = reinterpret_cast<FindToActivateRequest*>(lParam);

	DWORD dwProcessId;
	::GetWindowThreadProcessId(hwnd, &dwProcessId);

	if ((uint64)dwProcessId != request->processId) {
		return TRUE;
	}
	// Found a Top-Level window.
	if (WindowIdFromHWND(hwnd) == request->windowId) {
		request->result = hwnd;
		request->resultLevel = 3;
		return FALSE;
	}
	const auto data = static_cast<uint32>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if ((data != 1 && data != 2) || (data <= request->resultLevel)) {
		return TRUE;
	}
	request->result = hwnd;
	request->resultLevel = data;
	return TRUE;
}

void DeleteMyModules() {
	constexpr auto kMaxPathLong = 32767;
	auto exePath = std::array<WCHAR, kMaxPathLong + 1>{ 0 };
	const auto exeLength = GetModuleFileName(
		nullptr,
		exePath.data(),
		kMaxPathLong + 1);
	if (!exeLength || exeLength >= kMaxPathLong + 1) {
		return;
	}
	const auto exe = std::wstring(exePath.data());
	const auto last1 = exe.find_last_of('\\');
	const auto last2 = exe.find_last_of('/');
	const auto last = std::max(
		(last1 == std::wstring::npos) ? -1 : int(last1),
		(last2 == std::wstring::npos) ? -1 : int(last2));
	if (last < 0) {
		return;
	}
	const auto modules = exe.substr(0, last + 1) + L"modules";
	const auto deleteOne = [&](const wchar_t *name, const wchar_t *arch) {
		const auto path = modules + L'\\' + arch + L'\\' + name;
		DeleteFile(path.c_str());
	};
	const auto deleteBoth = [&](const wchar_t *name) {
		deleteOne(name, L"x86");
		deleteOne(name, L"x64");
	};
	const auto removeOne = [&](const std::wstring &name) {
		const auto path = modules + L'\\' + name;
		RemoveDirectory(path.c_str());
	};
	const auto removeBoth = [&](const std::wstring &name) {
		removeOne(L"x86\\" + name);
		removeOne(L"x64\\" + name);
	};
	deleteBoth(L"d3d\\d3dcompiler_47.dll");

	removeBoth(L"d3d");
	removeOne(L"x86");
	removeOne(L"x64");
	RemoveDirectory(modules.c_str());
}

bool ManageAppLink(
		bool create,
		bool silent,
		const GUID &folderId,
		const wchar_t *args,
		const wchar_t *description) {
	if (cExeName().isEmpty()) {
		return false;
	}
	PWSTR startupFolder;
	HRESULT hr = SHGetKnownFolderPath(
		folderId,
		KF_FLAG_CREATE,
		nullptr,
		&startupFolder);
	const auto guard = gsl::finally([&] {
		CoTaskMemFree(startupFolder);
	});
	if (!SUCCEEDED(hr)) {
		WCHAR buffer[64];
		const auto size = base::array_size(buffer) - 1;
		const auto length = StringFromGUID2(folderId, buffer, size);
		if (length > 0 && length <= size) {
			buffer[length] = 0;
			if (!silent) LOG(("App Error: could not get %1 folder: %2").arg(buffer).arg(hr));
		}
		return false;
	}
	const auto lnk = QString::fromWCharArray(startupFolder)
		+ '\\'
		+ AppFile.utf16()
		+ u".lnk"_q;
	if (!create) {
		QFile::remove(lnk);
		return true;
	}
	const auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		if (!silent) LOG(("App Error: could not create instance of IID_IShellLink %1").arg(hr));
		return false;
	}
	QString exe = QDir::toNativeSeparators(cExeDir() + cExeName()), dir = QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath());
	shellLink->SetArguments(args);
	shellLink->SetPath(exe.toStdWString().c_str());
	shellLink->SetWorkingDirectory(dir.toStdWString().c_str());
	shellLink->SetDescription(description);

	if (const auto propertyStore = shellLink.try_as<IPropertyStore>()) {
		PROPVARIANT appIdPropVar;
		hr = InitPropVariantFromString(AppUserModelId::Id().c_str(), &appIdPropVar);
		if (SUCCEEDED(hr)) {
			hr = propertyStore->SetValue(AppUserModelId::Key(), appIdPropVar);
			PropVariantClear(&appIdPropVar);
			if (SUCCEEDED(hr)) {
				hr = propertyStore->Commit();
			}
		}
	}

	const auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile) {
		if (!silent) LOG(("App Error: could not create interface IID_IPersistFile %1").arg(hr));
		return false;
	}
	hr = persistFile->Save(lnk.toStdWString().c_str(), TRUE);
	if (!SUCCEEDED(hr)) {
		if (!silent) LOG(("App Error: could not save IPersistFile to path %1").arg(lnk));
		return false;
	}
	return true;
}

} // namespace

QString psAppDataPath() {
	static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
#ifdef OS_WIN_STORE
		return appData.absolutePath() + u"/Telegram Desktop UWP/"_q;
#else // OS_WIN_STORE
		return appData.absolutePath() + '/' + AppName.utf16() + '/';
#endif // OS_WIN_STORE
	}
	return QString();
}

QString psAppDataPathOld() {
	static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath() + '/' + AppNameOld.utf16() + '/';
	}
	return QString();
}

void psDoCleanup() {
	try {
		Platform::AutostartToggle(false);
		psSendToMenu(false, true);
		AppUserModelId::CleanupShortcut();
		DeleteMyModules();
	} catch (...) {
	}
}

int psCleanup() {
	__try
	{
		psDoCleanup();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
	return 0;
}

void psDoFixPrevious() {
	try {
		static const int bufSize = 4096;
		DWORD checkType = 0;
		DWORD checkSize = bufSize * 2;
		WCHAR checkStr[bufSize] = { 0 };
		HKEY newKey1 = nullptr;
		HKEY newKey2 = nullptr;
		HKEY oldKey1 = nullptr;
		HKEY oldKey2 = nullptr;

		const auto appId = AppId.utf16();
		const auto newKeyStr1 = QString("Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId).toStdWString();
		const auto newKeyStr2 = QString("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId).toStdWString();
		const auto oldKeyStr1 = QString("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId).toStdWString();
		const auto oldKeyStr2 = QString("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId).toStdWString();
		const auto newKeyRes1 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr1.c_str(), 0, KEY_READ, &newKey1);
		const auto newKeyRes2 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr2.c_str(), 0, KEY_READ, &newKey2);
		const auto oldKeyRes1 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr1.c_str(), 0, KEY_READ, &oldKey1);
		const auto oldKeyRes2 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr2.c_str(), 0, KEY_READ, &oldKey2);

		const auto existNew1 = (newKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(newKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		const auto existNew2 = (newKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(newKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		const auto existOld1 = (oldKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		const auto existOld2 = (oldKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;

		if (newKeyRes1 == ERROR_SUCCESS) RegCloseKey(newKey1);
		if (newKeyRes2 == ERROR_SUCCESS) RegCloseKey(newKey2);
		if (oldKeyRes1 == ERROR_SUCCESS) RegCloseKey(oldKey1);
		if (oldKeyRes2 == ERROR_SUCCESS) RegCloseKey(oldKey2);

		if (existNew1 || existNew2) {
			if (existOld1) RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr1.c_str());
			if (existOld2) RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr2.c_str());
		}

		QString userDesktopLnk, commonDesktopLnk;
		WCHAR userDesktopFolder[MAX_PATH], commonDesktopFolder[MAX_PATH];
		HRESULT userDesktopRes = SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, userDesktopFolder);
		HRESULT commonDesktopRes = SHGetFolderPath(0, CSIDL_COMMON_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, commonDesktopFolder);
		if (SUCCEEDED(userDesktopRes)) {
			userDesktopLnk = QString::fromWCharArray(userDesktopFolder) + "\\Telegram.lnk";
		}
		if (SUCCEEDED(commonDesktopRes)) {
			commonDesktopLnk = QString::fromWCharArray(commonDesktopFolder) + "\\Telegram.lnk";
		}
		QFile userDesktopFile(userDesktopLnk), commonDesktopFile(commonDesktopLnk);
		if (QFile::exists(userDesktopLnk) && QFile::exists(commonDesktopLnk) && userDesktopLnk != commonDesktopLnk) {
			QFile::remove(commonDesktopLnk);
		}
	} catch (...) {
	}
}

int psFixPrevious() {
	__try
	{
		psDoFixPrevious();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
	return 0;
}

namespace Platform {
namespace ThirdParty {
namespace {

void StartOpenSSL() {
	// Don't use dynamic OpenSSL config, it can load unwanted DLLs.
	OPENSSL_load_builtin_modules();
	ENGINE_load_builtin_engines();
	ERR_clear_error();
	OPENSSL_no_config();
}

} // namespace

void start() {
	StartOpenSSL();
	Dlls::CheckLoadedModules();
}

} // namespace ThirdParty

void start() {
	const auto supported = base::WinRT::Supported();
	LOG(("WinRT Supported: %1").arg(Logs::b(supported)));

	// https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setlocale-wsetlocale#utf-8-support
	setlocale(LC_ALL, ".UTF8");

	const auto appUserModelId = AppUserModelId::Id();
	SetCurrentProcessExplicitAppUserModelID(appUserModelId.c_str());
	LOG(("AppUserModelID: %1").arg(appUserModelId));
}

void finish() {
}

void SetApplicationIcon(const QIcon &icon) {
	QApplication::setWindowIcon(icon);
}

QString SingleInstanceLocalServerName(const QString &hash) {
	return u"Global\\"_q + hash + '-' + cGUIDStr();
}

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
std::optional<bool> IsDarkMode() {
	static const auto kSystemVersion = QOperatingSystemVersion::current();
	static const auto kDarkModeAddedVersion = QOperatingSystemVersion(
		QOperatingSystemVersion::Windows,
		10,
		0,
		17763);
	static const auto kSupported = (kSystemVersion >= kDarkModeAddedVersion);
	if (!kSupported) {
		return std::nullopt;
	}

	HIGHCONTRAST hcf = {};
	hcf.cbSize = static_cast<UINT>(sizeof(HIGHCONTRAST));
	if (SystemParametersInfo(SPI_GETHIGHCONTRAST, hcf.cbSize, &hcf, FALSE)
			&& (hcf.dwFlags & HCF_HIGHCONTRASTON)) {
		return std::nullopt;
	}

	const auto keyName = L""
		"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
	const auto valueName = L"AppsUseLightTheme";
	auto key = HKEY();
	auto result = RegOpenKeyEx(HKEY_CURRENT_USER, keyName, 0, KEY_READ, &key);
	if (result != ERROR_SUCCESS) {
		return std::nullopt;
	}

	DWORD value = 0, type = 0, size = sizeof(value);
	result = RegQueryValueEx(key, valueName, 0, &type, (LPBYTE)&value, &size);
	RegCloseKey(key);
	if (result != ERROR_SUCCESS) {
		return std::nullopt;
	}

	return (value == 0);
}
#endif // Qt < 6.5.0

bool AutostartSupported() {
	return true;
}

void AutostartRequestStateFromSystem(Fn<void(bool)> callback) {
#ifdef OS_WIN_STORE
	AutostartTask::RequestState([=](bool enabled) {
		crl::on_main([=] {
			callback(enabled);
		});
	});
#endif // OS_WIN_STORE
}

void AutostartToggle(bool enabled, Fn<void(bool)> done) {
#ifdef OS_WIN_STORE
	const auto requested = enabled;
	const auto callback = [=](bool enabled) { crl::on_main([=] {
		if (!Core::IsAppLaunched()) {
			return;
		}
		done(enabled);
		if (!requested || enabled) {
			return;
		} else if (const auto window = Core::App().activeWindow()) {
			window->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_auto_start_disabled_uwp(),
				.confirmed = [](Fn<void()> close) {
					AutostartTask::OpenSettings();
					close();
				},
				.confirmText = tr::lng_settings_open_system_settings(),
			}));
		}
	}); };
	AutostartTask::Toggle(
		enabled,
		done ? Fn<void(bool)>(callback) : nullptr);
#else // OS_WIN_STORE
	const auto silent = !done;
	const auto success = ManageAppLink(
		enabled,
		silent,
		FOLDERID_Startup,
		L"-autostart",
		L"Telegram autorun link.\n"
		"You can disable autorun in Telegram settings.");
	if (done) {
		done(enabled && success);
	}
#endif // OS_WIN_STORE
}

bool AutostartSkip() {
#ifdef OS_WIN_STORE
	return false;
#else // OS_WIN_STORE
	return !cAutoStart();
#endif // OS_WIN_STORE
}

void WriteCrashDumpDetails() {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	PROCESS_MEMORY_COUNTERS data = { 0 };
	if (Dlls::GetProcessMemoryInfo
		&& Dlls::GetProcessMemoryInfo(
			GetCurrentProcess(),
			&data,
			sizeof(data))) {
		const auto mb = 1024 * 1024;
		CrashReports::dump()
			<< "Memory-usage: "
			<< (data.PeakWorkingSetSize / mb)
			<< " MB (peak), "
			<< (data.WorkingSetSize / mb)
			<< " MB (current)\n";
		CrashReports::dump()
			<< "Pagefile-usage: "
			<< (data.PeakPagefileUsage / mb)
			<< " MB (peak), "
			<< (data.PagefileUsage / mb)
			<< " MB (current)\n";
	}
#endif // TDESKTOP_DISABLE_CRASH_REPORTS
}

void SetWindowPriority(not_null<QWidget*> window, uint32 priority) {
	const auto hwnd = reinterpret_cast<HWND>(window->winId());
	Assert(hwnd != nullptr);

	SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(priority));
}

uint64 ActivationWindowId(not_null<QWidget*> window) {
	return WindowIdFromHWND(reinterpret_cast<HWND>(window->winId()));
}

void ActivateOtherProcess(uint64 processId, uint64 windowId) {
	auto request = FindToActivateRequest{
		.processId = processId,
		.windowId = windowId,
	};
	::EnumWindows((WNDENUMPROC)FindToActivate, (LPARAM)&request);
	if (const auto hwnd = request.result) {
		::SetForegroundWindow(hwnd);
		::SetFocus(hwnd);
	}
}

} // namespace Platform

namespace {
	void _psLogError(const char *str, LSTATUS code) {
		LPWSTR errorTextFormatted = nullptr;
		auto formatFlags = FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_IGNORE_INSERTS;
		FormatMessage(
			formatFlags,
			NULL,
			code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&errorTextFormatted,
			0,
			0);
		auto errorText = errorTextFormatted
			? errorTextFormatted
			: L"(Unknown error)";
		LOG((str).arg(code).arg(QString::fromStdWString(errorText)));
		LocalFree(errorTextFormatted);
	}

	bool _psOpenRegKey(LPCWSTR key, PHKEY rkey) {
		DEBUG_LOG(("App Info: opening reg key %1...").arg(QString::fromStdWString(key)));
		LSTATUS status = RegOpenKeyEx(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE | KEY_WRITE, rkey);
		if (status != ERROR_SUCCESS) {
			if (status == ERROR_FILE_NOT_FOUND) {
				status = RegCreateKeyEx(HKEY_CURRENT_USER, key, 0, 0, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_WRITE, 0, rkey, 0);
				if (status != ERROR_SUCCESS) {
					QString msg = u"App Error: could not create '%1' registry key, error %2"_q.arg(QString::fromStdWString(key)).arg(u"%1: %2"_q);
					_psLogError(msg.toUtf8().constData(), status);
					return false;
				}
			} else {
				QString msg = u"App Error: could not open '%1' registry key, error %2"_q.arg(QString::fromStdWString(key)).arg(u"%1: %2"_q);
				_psLogError(msg.toUtf8().constData(), status);
				return false;
			}
		}
		return true;
	}

	bool _psSetKeyValue(HKEY rkey, LPCWSTR value, QString v) {
		static const int bufSize = 4096;
		DWORD defaultType, defaultSize = bufSize * 2;
		WCHAR defaultStr[bufSize] = { 0 };
		if (RegQueryValueEx(rkey, value, 0, &defaultType, (BYTE*)defaultStr, &defaultSize) != ERROR_SUCCESS || defaultType != REG_SZ || defaultSize != (v.size() + 1) * 2 || QString::fromStdWString(defaultStr) != v) {
			WCHAR tmp[bufSize] = { 0 };
			if (!v.isEmpty()) StringCbPrintf(tmp, bufSize, v.replace(QChar('%'), u"%%"_q).toStdWString().c_str());
			LSTATUS status = RegSetValueEx(rkey, value, 0, REG_SZ, (BYTE*)tmp, (wcslen(tmp) + 1) * sizeof(WCHAR));
			if (status != ERROR_SUCCESS) {
				QString msg = u"App Error: could not set %1, error %2"_q.arg(value ? ('\'' + QString::fromStdWString(value) + '\'') : u"(Default)"_q).arg("%1: %2");
				_psLogError(msg.toUtf8().constData(), status);
				return false;
			}
		}
		return true;
	}
}

namespace Platform {

PermissionStatus GetPermissionStatus(PermissionType type) {
	if (type == PermissionType::Microphone) {
		PermissionStatus result = PermissionStatus::Granted;
		HKEY hKey;
		LSTATUS res = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone", 0, KEY_QUERY_VALUE, &hKey);
		if (res == ERROR_SUCCESS) {
			wchar_t buf[20];
			DWORD length = sizeof(buf);
			res = RegQueryValueEx(hKey, L"Value", NULL, NULL, (LPBYTE)buf, &length);
			if (res == ERROR_SUCCESS) {
				if (wcscmp(buf, L"Deny") == 0) {
					result = PermissionStatus::Denied;
				}
			}
			RegCloseKey(hKey);
		}
		return result;
	}
	return PermissionStatus::Granted;
}

void RequestPermission(PermissionType type, Fn<void(PermissionStatus)> resultCallback) {
	resultCallback(PermissionStatus::Granted);
}

void OpenSystemSettingsForPermission(PermissionType type) {
	if (type == PermissionType::Microphone) {
		crl::on_main([] {
			ShellExecute(
				nullptr,
				L"open",
				L"ms-settings:privacy-microphone",
				nullptr,
				nullptr,
				SW_SHOWDEFAULT);
		});
	}
}

bool OpenSystemSettings(SystemSettingsType type) {
	if (type == SystemSettingsType::Audio) {
		crl::on_main([] {
			WinExec("control.exe mmsys.cpl", SW_SHOW);
			//QDesktopServices::openUrl(QUrl("ms-settings:sound"));
		});
	}
	return true;
}

void NewVersionLaunched(int oldVersion) {
	if (oldVersion <= 4009009) {
		AppUserModelId::CheckPinned();
	}
	if (oldVersion > 0 && oldVersion < 2008012) {
		// Reset icons cache, because we've changed the application icon.
		if (Dlls::SHChangeNotify) {
			Dlls::SHChangeNotify(
				SHCNE_ASSOCCHANGED,
				SHCNF_IDLIST,
				nullptr,
				nullptr);
		}
	}
}

QImage DefaultApplicationIcon() {
	return Window::Logo();
}

} // namespace Platform

void psSendToMenu(bool send, bool silent) {
	ManageAppLink(
		send,
		silent,
		FOLDERID_SendTo,
		L"-sendpath",
		L"Telegram send to link.\n"
		"You can disable send to menu item in Telegram settings.");
}

bool psLaunchMaps(const Data::LocationPoint &point) {
	const auto aar = base::WinRT::TryCreateInstance<
		IApplicationAssociationRegistration
	>(CLSID_ApplicationAssociationRegistration);
	if (!aar) {
		return false;
	}

	auto handler = base::CoTaskMemString();
	const auto result = aar->QueryCurrentDefault(
		L"bingmaps",
		AT_URLPROTOCOL,
		AL_EFFECTIVE,
		handler.put());
	if (FAILED(result)
		|| !handler
		|| !handler.data()
		|| std::wstring(handler.data()) == L"bingmaps") {
		return false;
	}

	const auto url = u"bingmaps:?lvl=16&collection=point.%1_%2_Point"_q;
	return QDesktopServices::openUrl(
		url.arg(point.latAsString()).arg(point.lonAsString()));
}

// Stub while we still support Windows 7.
extern "C" {

STDAPI GetDpiForMonitor(
		_In_ HMONITOR hmonitor,
		_In_ MONITOR_DPI_TYPE dpiType,
		_Out_ UINT *dpiX,
		_Out_ UINT *dpiY) {
	return Dlls::GetDpiForMonitor
		? Dlls::GetDpiForMonitor(hmonitor, dpiType, dpiX, dpiY)
		: E_FAIL;
}

} // extern "C"
