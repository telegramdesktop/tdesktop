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
#include "base/platform/base_platform_info.h"
#include "base/call_delayed.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "history/history_location_manager.h"
#include "storage/localstorage.h"
#include "core/crash_reports.h"

#include <QtCore/QOperatingSystemVersion>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtGui/QDesktopServices>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>

#include <Shobjidl.h>
#include <shellapi.h>

#include <roapi.h>
#include <wrl/client.h>

#include <openssl/conf.h>
#include <openssl/engine.h>
#include <openssl/err.h>

#include <dbghelp.h>
#include <shlobj.h>
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

#ifndef DCX_USESTYLE
#define DCX_USESTYLE 0x00010000
#endif

#ifndef WM_NCPOINTERUPDATE
#define WM_NCPOINTERUPDATE 0x0241
#define WM_NCPOINTERDOWN 0x0242
#define WM_NCPOINTERUP 0x0243
#endif

using namespace Microsoft::WRL;
using namespace Platform;

namespace {

bool themeInited = false;
bool finished = true;
QMargins simpleMargins, margins;
HICON bigIcon = 0, smallIcon = 0, overlayIcon = 0;

class _PsInitializer {
public:
	_PsInitializer() {
		Dlls::start();
	}
};
_PsInitializer _psInitializer;

BOOL CALLBACK _ActivateProcess(HWND hWnd, LPARAM lParam) {
	uint64 &processId(*(uint64*)lParam);

	DWORD dwProcessId;
	::GetWindowThreadProcessId(hWnd, &dwProcessId);

	if ((uint64)dwProcessId == processId) { // found top-level window
		static const int32 nameBufSize = 1024;
		WCHAR nameBuf[nameBufSize];
		int32 len = GetWindowText(hWnd, nameBuf, nameBufSize);
		if (len && len < nameBufSize) {
			if (QRegularExpression(qsl("^Telegram(\\s*\\(\\d+\\))?$")).match(QString::fromStdWString(nameBuf)).hasMatch()) {
				BOOL res = ::SetForegroundWindow(hWnd);
				::SetFocus(hWnd);
				return FALSE;
			}
		}
	}
	return TRUE;
}

}

void psActivateProcess(uint64 pid) {
	if (pid) {
		::EnumWindows((WNDENUMPROC)_ActivateProcess, (LPARAM)&pid);
	}
}

QString psAppDataPath() {
	static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
#ifdef OS_WIN_STORE
		return appData.absolutePath() + qsl("/Telegram Desktop UWP/");
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
		psAutoStart(false, true);
		psSendToMenu(false, true);
		AppUserModelId::cleanupShortcut();
	} catch (...) {
	}
}

QRect psDesktopRect() {
	static QRect _monitorRect;
	static crl::time _monitorLastGot = 0;
	auto tnow = crl::now();
	if (tnow > _monitorLastGot + 1000LL || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		HMONITOR hMonitor = MonitorFromWindow(App::wnd()->psHwnd(), MONITOR_DEFAULTTONEAREST);
		if (hMonitor) {
			MONITORINFOEX info;
			info.cbSize = sizeof(info);
			GetMonitorInfo(hMonitor, &info);
			_monitorRect = QRect(info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top);
		} else {
			_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
		}
	}
	return _monitorRect;
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
			const auto deleteKeyRes1 = existOld1 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr1.c_str()) : ERROR_SUCCESS;
			const auto deleteKeyRes2 = existOld2 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr2.c_str()) : ERROR_SUCCESS;
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
			bool removed = QFile::remove(commonDesktopLnk);
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
}

} // namespace ThirdParty

void start() {
	Dlls::init();
}

void finish() {
}

void SetApplicationIcon(const QIcon &icon) {
	QApplication::setWindowIcon(icon);
}

QString SingleInstanceLocalServerName(const QString &hash) {
	return qsl("Global\\") + hash + '-' + cGUIDStr();
}

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

bool AutostartSupported() {
	return !IsWindowsStoreBuild();
}

bool ShowWindowMenu(QWindow *window) {
	const auto pos = QCursor::pos();

	SendMessage(
		HWND(window->winId()),
		WM_SYSCOMMAND,
		SC_MOUSEMENU,
		MAKELPARAM(pos.x(), pos.y()));

	return true;
}

Window::ControlsLayout WindowControlsLayout() {
	Window::ControlsLayout controls;
	controls.right = {
		Window::Control::Minimize,
		Window::Control::Maximize,
		Window::Control::Close,
	};

	return controls;
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
					QString msg = qsl("App Error: could not create '%1' registry key, error %2").arg(QString::fromStdWString(key)).arg(qsl("%1: %2"));
					_psLogError(msg.toUtf8().constData(), status);
					return false;
				}
			} else {
				QString msg = qsl("App Error: could not open '%1' registry key, error %2").arg(QString::fromStdWString(key)).arg(qsl("%1: %2"));
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
			if (!v.isEmpty()) StringCbPrintf(tmp, bufSize, v.replace(QChar('%'), qsl("%%")).toStdWString().c_str());
			LSTATUS status = RegSetValueEx(rkey, value, 0, REG_SZ, (BYTE*)tmp, (wcslen(tmp) + 1) * sizeof(WCHAR));
			if (status != ERROR_SUCCESS) {
				QString msg = qsl("App Error: could not set %1, error %2").arg(value ? ('\'' + QString::fromStdWString(value) + '\'') : qsl("(Default)")).arg("%1: %2");
				_psLogError(msg.toUtf8().constData(), status);
				return false;
			}
		}
		return true;
	}
}

namespace Platform {

void RegisterCustomScheme(bool force) {
	if (cExeName().isEmpty()) {
		return;
	}
	DEBUG_LOG(("App Info: Checking custom scheme 'tg'..."));

	HKEY rkey;
	QString exe = QDir::toNativeSeparators(cExeDir() + cExeName());

	// Legacy URI scheme registration
	if (!_psOpenRegKey(L"Software\\Classes\\tg", &rkey)) return;
	if (!_psSetKeyValue(rkey, L"URL Protocol", QString())) return;
	if (!_psSetKeyValue(rkey, 0, qsl("URL:Telegram Link"))) return;

	if (!_psOpenRegKey(L"Software\\Classes\\tg\\DefaultIcon", &rkey)) return;
	if (!_psSetKeyValue(rkey, 0, '"' + exe + qsl(",1\""))) return;

	if (!_psOpenRegKey(L"Software\\Classes\\tg\\shell", &rkey)) return;
	if (!_psOpenRegKey(L"Software\\Classes\\tg\\shell\\open", &rkey)) return;
	if (!_psOpenRegKey(L"Software\\Classes\\tg\\shell\\open\\command", &rkey)) return;
	if (!_psSetKeyValue(rkey, 0, '"' + exe + qsl("\" -workdir \"") + cWorkingDir() + qsl("\" -- \"%1\""))) return;

	// URI scheme registration as Default Program - Windows Vista and above
	if (!_psOpenRegKey(L"Software\\Classes\\tdesktop.tg", &rkey)) return;
	if (!_psOpenRegKey(L"Software\\Classes\\tdesktop.tg\\DefaultIcon", &rkey)) return;
	if (!_psSetKeyValue(rkey, 0, '"' + exe + qsl(",1\""))) return;

	if (!_psOpenRegKey(L"Software\\Classes\\tdesktop.tg\\shell", &rkey)) return;
	if (!_psOpenRegKey(L"Software\\Classes\\tdesktop.tg\\shell\\open", &rkey)) return;
	if (!_psOpenRegKey(L"Software\\Classes\\tdesktop.tg\\shell\\open\\command", &rkey)) return;
	if (!_psSetKeyValue(rkey, 0, '"' + exe + qsl("\" -workdir \"") + cWorkingDir() + qsl("\" -- \"%1\""))) return;

	if (!_psOpenRegKey(L"Software\\TelegramDesktop", &rkey)) return;
	if (!_psOpenRegKey(L"Software\\TelegramDesktop\\Capabilities", &rkey)) return;
	if (!_psSetKeyValue(rkey, L"ApplicationName", qsl("Telegram Desktop"))) return;
	if (!_psSetKeyValue(rkey, L"ApplicationDescription", qsl("Telegram Desktop"))) return;
	if (!_psOpenRegKey(L"Software\\TelegramDesktop\\Capabilities\\UrlAssociations", &rkey)) return;
	if (!_psSetKeyValue(rkey, L"tg", qsl("tdesktop.tg"))) return;

	if (!_psOpenRegKey(L"Software\\RegisteredApplications", &rkey)) return;
	if (!_psSetKeyValue(rkey, L"Telegram Desktop", qsl("SOFTWARE\\TelegramDesktop\\Capabilities"))) return;
}

PermissionStatus GetPermissionStatus(PermissionType type) {
	if (type==PermissionType::Microphone) {
		PermissionStatus result=PermissionStatus::Granted;
		HKEY hKey;
		LSTATUS res=RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone", 0, KEY_QUERY_VALUE, &hKey);
		if(res==ERROR_SUCCESS) {
			wchar_t buf[20];
			DWORD length=sizeof(buf);
			res=RegQueryValueEx(hKey, L"Value", NULL, NULL, (LPBYTE)buf, &length);
			if(res==ERROR_SUCCESS) {
				if(wcscmp(buf, L"Deny")==0) {
					result=PermissionStatus::Denied;
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

} // namespace Platform

void psNewVersion() {
	Platform::RegisterCustomScheme();
	if (Local::oldSettingsVersion() < 8051) {
		AppUserModelId::checkPinned();
	}
	if (Local::oldSettingsVersion() > 0 && Local::oldSettingsVersion() < 10021) {
		// Reset icons cache, because we've changed the application icon.
		if (Dlls::SHChangeNotify) {
			Dlls::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
		}
	}
}

void _manageAppLnk(bool create, bool silent, int path_csidl, const wchar_t *args, const wchar_t *description) {
	if (cExeName().isEmpty()) {
		return;
	}
	WCHAR startupFolder[MAX_PATH];
	HRESULT hr = SHGetFolderPath(0, path_csidl, 0, SHGFP_TYPE_CURRENT, startupFolder);
	if (SUCCEEDED(hr)) {
		QString lnk = QString::fromWCharArray(startupFolder) + '\\' + AppFile.utf16() + qsl(".lnk");
		if (create) {
			ComPtr<IShellLink> shellLink;
			hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
			if (SUCCEEDED(hr)) {
				ComPtr<IPersistFile> persistFile;

				QString exe = QDir::toNativeSeparators(cExeDir() + cExeName()), dir = QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath());
				shellLink->SetArguments(args);
				shellLink->SetPath(exe.toStdWString().c_str());
				shellLink->SetWorkingDirectory(dir.toStdWString().c_str());
				shellLink->SetDescription(description);

				ComPtr<IPropertyStore> propertyStore;
				hr = shellLink.As(&propertyStore);
				if (SUCCEEDED(hr)) {
					PROPVARIANT appIdPropVar;
					hr = InitPropVariantFromString(AppUserModelId::getId(), &appIdPropVar);
					if (SUCCEEDED(hr)) {
						hr = propertyStore->SetValue(AppUserModelId::getKey(), appIdPropVar);
						PropVariantClear(&appIdPropVar);
						if (SUCCEEDED(hr)) {
							hr = propertyStore->Commit();
						}
					}
				}

				hr = shellLink.As(&persistFile);
				if (SUCCEEDED(hr)) {
					hr = persistFile->Save(lnk.toStdWString().c_str(), TRUE);
				} else {
					if (!silent) LOG(("App Error: could not create interface IID_IPersistFile %1").arg(hr));
				}
			} else {
				if (!silent) LOG(("App Error: could not create instance of IID_IShellLink %1").arg(hr));
			}
		} else {
			QFile::remove(lnk);
		}
	} else {
		if (!silent) LOG(("App Error: could not get CSIDL %1 folder %2").arg(path_csidl).arg(hr));
	}
}

void psAutoStart(bool start, bool silent) {
	_manageAppLnk(start, silent, CSIDL_STARTUP, L"-autostart", L"Telegram autorun link.\nYou can disable autorun in Telegram settings.");
}

void psSendToMenu(bool send, bool silent) {
	_manageAppLnk(send, silent, CSIDL_SENDTO, L"-sendpath", L"Telegram send to link.\nYou can disable send to menu item in Telegram settings.");
}

void psWriteDump() {
#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS
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
#endif // DESKTOP_APP_DISABLE_CRASH_REPORTS
}

bool psLaunchMaps(const Data::LocationPoint &point) {
	return QDesktopServices::openUrl(qsl("bingmaps:?lvl=16&collection=point.%1_%2_Point").arg(point.latAsString()).arg(point.lonAsString()));
}
