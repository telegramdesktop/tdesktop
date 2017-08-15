/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "platform/win/specific_win.h"

#include "platform/win/main_window_win.h"
#include "platform/win/notifications_manager_win.h"
#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/windows_dlls.h"
#include "platform/win/windows_event_filter.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "history/history_location_manager.h"
#include "storage/localstorage.h"
#include "passcodewidget.h"
#include "base/task_queue.h"

#include <Shobjidl.h>
#include <shellapi.h>

#include <roapi.h>
#include <wrl\client.h>
#include <wrl\implements.h>
#include <windows.ui.notifications.h>

#pragma warning(push)
#pragma warning(disable:4091)
#include <dbghelp.h>
#include <shlobj.h>
#pragma warning(pop)

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

#include <qpa/qplatformnativeinterface.h>

#ifndef DCX_USESTYLE
#define DCX_USESTYLE 0x00010000
#endif

#ifndef WM_NCPOINTERUPDATE
#define WM_NCPOINTERUPDATE              0x0241
#define WM_NCPOINTERDOWN                0x0242
#define WM_NCPOINTERUP                  0x0243
#endif

using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Notifications;
using namespace ABI::Windows::Data::Xml::Dom;
using namespace Windows::Foundation;
using namespace Platform;

namespace {
    QStringList _initLogs;

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

};

QAbstractNativeEventFilter *psNativeEventFilter() {
	return EventFilter::createInstance();
}

void psDeleteDir(const QString &dir) {
	std::wstring wDir = QDir::toNativeSeparators(dir).toStdWString();
	WCHAR path[4096];
	memcpy(path, wDir.c_str(), (wDir.size() + 1) * sizeof(WCHAR));
	path[wDir.size() + 1] = 0;
	SHFILEOPSTRUCT file_op = {
		NULL,
		FO_DELETE,
		path,
		L"",
		FOF_NOCONFIRMATION |
		FOF_NOERRORUI |
		FOF_SILENT,
		false,
		0,
		L""
	};
	int res = SHFileOperation(&file_op);
}

namespace {
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

namespace {

TimeMs _lastUserAction = 0;

} // namespace

void psUserActionDone() {
	_lastUserAction = getms(true);
	EventFilter::getInstance()->setSessionLoggedOff(false);
}

bool psIdleSupported() {
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	return GetLastInputInfo(&lii);
}

TimeMs psIdleTime() {
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	return GetLastInputInfo(&lii) ? (GetTickCount() - lii.dwTime) : (getms(true) - _lastUserAction);
}

QStringList psInitLogs() {
    return _initLogs;
}

void psClearInitLogs() {
    _initLogs = QStringList();
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
		return appData.absolutePath() + '/' + str_const_toString(AppName) + '/';
#endif // OS_WIN_STORE
	}
	return QString();
}

QString psAppDataPathOld() {
	static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath() + '/' + str_const_toString(AppNameOld) + '/';
	}
	return QString();
}

QString psDownloadPath() {
	return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + '/' + str_const_toString(AppName) + '/';
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
		psSendToMenu(false, true);
		AppUserModelId::cleanupShortcut();
	} catch (...) {
	}
}

namespace {

QRect _monitorRect;
TimeMs _monitorLastGot = 0;

} // namespace

QRect psDesktopRect() {
	auto tnow = getms();
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

void psShowOverAll(QWidget *w, bool canFocus) {
}

void psBringToBack(QWidget *w) {
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
		DWORD checkType, checkSize = bufSize * 2;
		WCHAR checkStr[bufSize];

		QString appId = str_const_toString(AppId);
		QString newKeyStr1 = QString("Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString newKeyStr2 = QString("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString oldKeyStr1 = QString("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString oldKeyStr2 = QString("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		HKEY newKey1, newKey2, oldKey1, oldKey2;
		LSTATUS newKeyRes1 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr1.toStdWString().c_str(), 0, KEY_READ, &newKey1);
		LSTATUS newKeyRes2 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr2.toStdWString().c_str(), 0, KEY_READ, &newKey2);
		LSTATUS oldKeyRes1 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr1.toStdWString().c_str(), 0, KEY_READ, &oldKey1);
		LSTATUS oldKeyRes2 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr2.toStdWString().c_str(), 0, KEY_READ, &oldKey2);

		bool existNew1 = (newKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(newKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existNew2 = (newKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(newKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existOld1 = (oldKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existOld2 = (oldKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;

		if (newKeyRes1 == ERROR_SUCCESS) RegCloseKey(newKey1);
		if (newKeyRes2 == ERROR_SUCCESS) RegCloseKey(newKey2);
		if (oldKeyRes1 == ERROR_SUCCESS) RegCloseKey(oldKey1);
		if (oldKeyRes2 == ERROR_SUCCESS) RegCloseKey(oldKey2);

		if (existNew1 || existNew2) {
			oldKeyRes1 = existOld1 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr1.toStdWString().c_str()) : ERROR_SUCCESS;
			oldKeyRes2 = existOld2 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr2.toStdWString().c_str()) : ERROR_SUCCESS;
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

void start() {
	Dlls::init();
}

void finish() {
	EventFilter::destroy();
}

QString SystemCountry() {
	int chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, 0, 0);
	if (chCount && chCount < 128) {
		WCHAR wstrCountry[128];
		int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, wstrCountry, chCount);
		if (len) {
			return QString::fromStdWString(std::wstring(wstrCountry));
		}
	}
	return QString();
}

QString CurrentExecutablePath(int argc, char *argv[]) {
	WCHAR result[MAX_PATH + 1] = { 0 };
	auto count = GetModuleFileName(nullptr, result, MAX_PATH + 1);
	if (count < MAX_PATH + 1) {
		auto info = QFileInfo(QDir::fromNativeSeparators(QString::fromWCharArray(result)));
		return info.absoluteFilePath();
	}

	// Fallback to the first command line argument.
	auto argsCount = 0;
	if (auto args = CommandLineToArgvW(GetCommandLine(), &argsCount)) {
		auto info = QFileInfo(QDir::fromNativeSeparators(QString::fromWCharArray(args[0])));
		LocalFree(args);
		return info.absoluteFilePath();
	}
	return QString();
}

namespace {

QString GetLangCodeById(unsigned lngId) {
	auto primary = (lngId & 0xFFU);
	switch (primary) {
	case 0x36: return qsl("af");
	case 0x1C: return qsl("sq");
	case 0x5E: return qsl("am");
	case 0x01: return qsl("ar");
	case 0x2B: return qsl("hy");
	case 0x4D: return qsl("as");
	case 0x2C: return qsl("az");
	case 0x45: return qsl("bn");
	case 0x6D: return qsl("ba");
	case 0x2D: return qsl("eu");
	case 0x23: return qsl("be");
	case 0x1A:
	if (lngId == LANG_CROATIAN) {
		return qsl("hr");
	} else if (lngId == LANG_BOSNIAN_NEUTRAL || lngId == LANG_BOSNIAN) {
		return qsl("bs");
	}
	return qsl("sr");
	break;
	case 0x7E: return qsl("br");
	case 0x02: return qsl("bg");
	case 0x92: return qsl("ku");
	case 0x03: return qsl("ca");
	case 0x04: return qsl("zh");
	case 0x83: return qsl("co");
	case 0x05: return qsl("cs");
	case 0x06: return qsl("da");
	case 0x65: return qsl("dv");
	case 0x13: return qsl("nl");
	case 0x09: return qsl("en");
	case 0x25: return qsl("et");
	case 0x38: return qsl("fo");
	case 0x0B: return qsl("fi");
	case 0x0c: return qsl("fr");
	case 0x62: return qsl("fy");
	case 0x56: return qsl("gl");
	case 0x37: return qsl("ka");
	case 0x07: return qsl("de");
	case 0x08: return qsl("el");
	case 0x6F: return qsl("kl");
	case 0x47: return qsl("gu");
	case 0x68: return qsl("ha");
	case 0x0D: return qsl("he");
	case 0x39: return qsl("hi");
	case 0x0E: return qsl("hu");
	case 0x0F: return qsl("is");
	case 0x70: return qsl("ig");
	case 0x21: return qsl("id");
	case 0x5D: return qsl("iu");
	case 0x3C: return qsl("ga");
	case 0x34: return qsl("xh");
	case 0x35: return qsl("zu");
	case 0x10: return qsl("it");
	case 0x11: return qsl("ja");
	case 0x4B: return qsl("kn");
	case 0x3F: return qsl("kk");
	case 0x53: return qsl("kh");
	case 0x87: return qsl("rw");
	case 0x12: return qsl("ko");
	case 0x40: return qsl("ky");
	case 0x54: return qsl("lo");
	case 0x26: return qsl("lv");
	case 0x27: return qsl("lt");
	case 0x6E: return qsl("lb");
	case 0x2F: return qsl("mk");
	case 0x3E: return qsl("ms");
	case 0x4C: return qsl("ml");
	case 0x3A: return qsl("mt");
	case 0x81: return qsl("mi");
	case 0x4E: return qsl("mr");
	case 0x50: return qsl("mn");
	case 0x61: return qsl("ne");
	case 0x14: return qsl("no");
	case 0x82: return qsl("oc");
	case 0x48: return qsl("or");
	case 0x63: return qsl("ps");
	case 0x29: return qsl("fa");
	case 0x15: return qsl("pl");
	case 0x16: return qsl("pt");
	case 0x67: return qsl("ff");
	case 0x46: return qsl("pa");
	case 0x18: return qsl("ro");
	case 0x17: return qsl("rm");
	case 0x19: return qsl("ru");
	case 0x3B: return qsl("se");
	case 0x4F: return qsl("sa");
	case 0x32: return qsl("tn");
	case 0x59: return qsl("sd");
	case 0x5B: return qsl("si");
	case 0x1B: return qsl("sk");
	case 0x24: return qsl("sl");
	case 0x0A: return qsl("es");
	case 0x41: return qsl("sw");
	case 0x1D: return qsl("sv");
	case 0x28: return qsl("tg");
	case 0x49: return qsl("ta");
	case 0x44: return qsl("tt");
	case 0x4A: return qsl("te");
	case 0x1E: return qsl("th");
	case 0x51: return qsl("bo");
	case 0x73: return qsl("ti");
	case 0x1F: return qsl("tr");
	case 0x42: return qsl("tk");
	case 0x22: return qsl("uk");
	case 0x20: return qsl("ur");
	case 0x80: return qsl("ug");
	case 0x43: return qsl("uz");
	case 0x2A: return qsl("vi");
	case 0x52: return qsl("cy");
	case 0x88: return qsl("wo");
	case 0x78: return qsl("ii");
	case 0x6A: return qsl("yo");
	}
	return QString();
}

} // namespace

QString SystemLanguage() {
	constexpr auto kMaxLanguageLength = 128;

	auto uiLanguageId = GetUserDefaultUILanguage();
	auto uiLanguageLength = GetLocaleInfo(uiLanguageId, LOCALE_SNAME, nullptr, 0);
	if (uiLanguageLength > 0 && uiLanguageLength < kMaxLanguageLength) {
		WCHAR uiLanguageWideString[kMaxLanguageLength] = { 0 };
		uiLanguageLength = GetLocaleInfo(uiLanguageId, LOCALE_SNAME, uiLanguageWideString, uiLanguageLength);
		if (uiLanguageLength <= 0) {
			return QString();
		}
		return QString::fromWCharArray(uiLanguageWideString);
	}
	auto uiLanguageCodeLength = GetLocaleInfo(uiLanguageId, LOCALE_ILANGUAGE, nullptr, 0);
	if (uiLanguageCodeLength > 0 && uiLanguageCodeLength < kMaxLanguageLength) {
		WCHAR uiLanguageCodeWideString[kMaxLanguageLength] = { 0 };
		uiLanguageCodeLength = GetLocaleInfo(uiLanguageId, LOCALE_ILANGUAGE, uiLanguageCodeWideString, uiLanguageCodeLength);
		if (uiLanguageCodeLength <= 0) {
			return QString();
		}

		auto languageCode = 0U;
		for (auto i = 0; i != uiLanguageCodeLength; ++i) {
			auto ch = uiLanguageCodeWideString[i];
			if (!ch) {
				break;
			}
			languageCode *= 0x10U;
			if (ch >= WCHAR('0') && ch <= WCHAR('9')) {
				languageCode += static_cast<unsigned>(int(ch) - int(WCHAR('0')));
			} else if (ch >= WCHAR('A') && ch <= WCHAR('F')) {
				languageCode += static_cast<unsigned>(0x0A + int(ch) - int(WCHAR('A')));
			} else {
				return QString();
			}
		}
		return GetLangCodeById(languageCode);
	}
	return QString();
}

} // namespace Platform

namespace {
	void _psLogError(const char *str, LSTATUS code) {
		LPTSTR errorText = NULL, errorTextDefault = L"(Unknown error)";
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&errorText, 0, 0);
		if (!errorText) {
			errorText = errorTextDefault;
		}
		LOG((str).arg(code).arg(QString::fromStdWString(errorText)));
		if (errorText != errorTextDefault) {
			LocalFree(errorText);
		}
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
			if (!v.isEmpty()) wsprintf(tmp, v.replace(QChar('%'), qsl("%%")).toStdWString().c_str());
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

void RegisterCustomScheme() {
	if (cExeName().isEmpty()) {
		return;
	}
#ifndef TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
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
#endif // !TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
}

void psNewVersion() {
	RegisterCustomScheme();
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

void psExecUpdater() {
	if (cExeName().isEmpty()) {
		return;
	}

	QString targs = qsl("-update -exename \"") + cExeName() + '"';
	if (cLaunchMode() == LaunchModeAutoStart) targs += qsl(" -autostart");
	if (cDebug()) targs += qsl(" -debug");
	if (cStartInTray()) targs += qsl(" -startintray");
	if (cWriteProtected()) targs += qsl(" -writeprotected \"") + cExeDir() + '"';

	QString updaterPath = cWriteProtected() ? (cWorkingDir() + qsl("tupdates/temp/Updater.exe")) : (cExeDir() + qsl("Updater.exe"));

	QString updater(QDir::toNativeSeparators(updaterPath)), wdir(QDir::toNativeSeparators(cWorkingDir()));

	DEBUG_LOG(("Application Info: executing %1 %2").arg(cExeDir() + "Updater.exe").arg(targs));
	HINSTANCE r = ShellExecute(0, cWriteProtected() ? L"runas" : 0, updater.toStdWString().c_str(), targs.toStdWString().c_str(), wdir.isEmpty() ? 0 : wdir.toStdWString().c_str(), SW_SHOWNORMAL);
	if (long(r) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3").arg(updater).arg(wdir).arg(long(r)));
		psDeleteDir(cWorkingDir() + qsl("tupdates/temp"));
	}
}

void psExecTelegram(const QString &crashreport) {
	if (cExeName().isEmpty()) {
		return;
	}
	QString targs = crashreport.isEmpty() ? qsl("-noupdate") : ('"' + crashreport + '"');
	if (crashreport.isEmpty()) {
		if (cRestartingToSettings()) targs += qsl(" -tosettings");
		if (cLaunchMode() == LaunchModeAutoStart) targs += qsl(" -autostart");
		if (cDebug()) targs += qsl(" -debug");
		if (cStartInTray()) targs += qsl(" -startintray");
		if (cTestMode()) targs += qsl(" -testmode");
		if (cDataFile() != qsl("data")) targs += qsl(" -key \"") + cDataFile() + '"';
	}
	QString telegram(QDir::toNativeSeparators(cExeDir() + cExeName())), wdir(QDir::toNativeSeparators(cWorkingDir()));

	DEBUG_LOG(("Application Info: executing %1 %2").arg(cExeDir() + cExeName()).arg(targs));
	Logs::closeMain();
	SignalHandlers::finish();
	HINSTANCE r = ShellExecute(0, 0, telegram.toStdWString().c_str(), targs.toStdWString().c_str(), wdir.isEmpty() ? 0 : wdir.toStdWString().c_str(), SW_SHOWNORMAL);
	if (long(r) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3").arg(telegram).arg(wdir).arg(long(r)));
	}
}

void _manageAppLnk(bool create, bool silent, int path_csidl, const wchar_t *args, const wchar_t *description) {
	if (cExeName().isEmpty()) {
		return;
	}
	WCHAR startupFolder[MAX_PATH];
	HRESULT hr = SHGetFolderPath(0, path_csidl, 0, SHGFP_TYPE_CURRENT, startupFolder);
	if (SUCCEEDED(hr)) {
		QString lnk = QString::fromWCharArray(startupFolder) + '\\' + str_const_toString(AppFile) + qsl(".lnk");
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

void psUpdateOverlayed(TWidget *widget) {
	bool wm = widget->testAttribute(Qt::WA_Mapped), wv = widget->testAttribute(Qt::WA_WState_Visible);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, true);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, true);
	widget->update();
	QEvent e(QEvent::UpdateRequest);
	QGuiApplication::sendEvent(widget, &e);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, false);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, false);
}

// Stack walk code is inspired by http://www.codeproject.com/Articles/11132/Walking-the-callstack

static const int StackEntryMaxNameLength = MAX_SYM_NAME + 1;

typedef BOOL(FAR STDAPICALLTYPE *t_SymCleanup)(
	_In_ HANDLE hProcess
);
t_SymCleanup symCleanup = 0;

typedef PVOID (FAR STDAPICALLTYPE *t_SymFunctionTableAccess64)(
    _In_ HANDLE hProcess,
    _In_ DWORD64 AddrBase
);
t_SymFunctionTableAccess64 symFunctionTableAccess64 = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_SymGetLineFromAddr64)(
    _In_ HANDLE hProcess,
    _In_ DWORD64 dwAddr,
    _Out_ PDWORD pdwDisplacement,
    _Out_ PIMAGEHLP_LINEW64 Line
);
t_SymGetLineFromAddr64 symGetLineFromAddr64 = 0;

typedef DWORD64 (FAR STDAPICALLTYPE *t_SymGetModuleBase64)(
    _In_ HANDLE hProcess,
    _In_ DWORD64 qwAddr
);
t_SymGetModuleBase64 symGetModuleBase64 = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_SymGetModuleInfo64)(
    _In_ HANDLE hProcess,
    _In_ DWORD64 qwAddr,
    _Out_ PIMAGEHLP_MODULEW64 ModuleInfo
);
t_SymGetModuleInfo64 symGetModuleInfo64 = 0;

typedef DWORD (FAR STDAPICALLTYPE *t_SymGetOptions)(
	VOID
);
t_SymGetOptions symGetOptions = 0;

typedef DWORD (FAR STDAPICALLTYPE *t_SymSetOptions)(
    _In_ DWORD SymOptions
);
t_SymSetOptions symSetOptions = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_SymGetSymFromAddr64)(
	IN HANDLE hProcess,
	IN DWORD64 dwAddr,
	OUT PDWORD64 pdwDisplacement,
	OUT PIMAGEHLP_SYMBOL64 Symbol
);
t_SymGetSymFromAddr64 symGetSymFromAddr64 = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_SymInitialize)(
	_In_ HANDLE hProcess,
	_In_opt_ PCWSTR UserSearchPath,
	_In_ BOOL fInvadeProcess
);
t_SymInitialize symInitialize = 0;

typedef DWORD64 (FAR STDAPICALLTYPE *t_SymLoadModule64)(
	_In_ HANDLE hProcess,
	_In_opt_ HANDLE hFile,
	_In_opt_ PCSTR ImageName,
	_In_opt_ PCSTR ModuleName,
	_In_ DWORD64 BaseOfDll,
	_In_ DWORD SizeOfDll
);
t_SymLoadModule64 symLoadModule64;

typedef BOOL (FAR STDAPICALLTYPE *t_StackWalk64)(
	_In_ DWORD MachineType,
	_In_ HANDLE hProcess,
	_In_ HANDLE hThread,
	_Inout_ LPSTACKFRAME64 StackFrame,
	_Inout_ PVOID ContextRecord,
	_In_opt_ PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
	_In_opt_ PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
	_In_opt_ PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
	_In_opt_ PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress
);
t_StackWalk64 stackWalk64 = 0;

typedef DWORD (FAR STDAPICALLTYPE *t_UnDecorateSymbolName)(
	PCSTR DecoratedName,
	PSTR UnDecoratedName,
	DWORD UndecoratedLength,
	DWORD Flags
);
t_UnDecorateSymbolName unDecorateSymbolName = 0;

typedef BOOL(FAR STDAPICALLTYPE *t_SymGetSearchPath)(
	_In_ HANDLE hProcess,
	_Out_writes_(SearchPathLength) PWSTR SearchPath,
	_In_ DWORD SearchPathLength
);
t_SymGetSearchPath symGetSearchPath = 0;

BOOL __stdcall ReadProcessMemoryRoutine64(
	_In_ HANDLE hProcess,
	_In_ DWORD64 qwBaseAddress,
	_Out_writes_bytes_(nSize) PVOID lpBuffer,
	_In_ DWORD nSize,
	_Out_ LPDWORD lpNumberOfBytesRead
) {
	SIZE_T st;
	BOOL bRet = ReadProcessMemory(hProcess, (LPVOID)qwBaseAddress, lpBuffer, nSize, &st);
	*lpNumberOfBytesRead = (DWORD)st;

	return bRet;
}

// **************************************** ToolHelp32 ************************
#define MAX_MODULE_NAME32 255
#define TH32CS_SNAPMODULE   0x00000008
#pragma pack( push, 8 )
typedef struct tagMODULEENTRY32
{
	DWORD   dwSize;
	DWORD   th32ModuleID;       // This module
	DWORD   th32ProcessID;      // owning process
	DWORD   GlblcntUsage;       // Global usage count on the module
	DWORD   ProccntUsage;       // Module usage count in th32ProcessID's context
	BYTE  * modBaseAddr;        // Base address of module in th32ProcessID's context
	DWORD   modBaseSize;        // Size in bytes of module starting at modBaseAddr
	HMODULE hModule;            // The hModule of this module in th32ProcessID's context
	char    szModule[MAX_MODULE_NAME32 + 1];
	char    szExePath[MAX_PATH];
} MODULEENTRY32;
typedef MODULEENTRY32 *PMODULEENTRY32;
typedef MODULEENTRY32 *LPMODULEENTRY32;
#pragma pack( pop )

typedef HANDLE (FAR STDAPICALLTYPE *t_CreateToolhelp32Snapshot)(DWORD dwFlags, DWORD th32ProcessID);
t_CreateToolhelp32Snapshot createToolhelp32Snapshot = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_Module32First)(HANDLE hSnapshot, LPMODULEENTRY32 lpme);
t_Module32First module32First = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_Module32Next)(HANDLE hSnapshot, LPMODULEENTRY32 lpme);
t_Module32Next module32Next = 0;

bool LoadDbgHelp(bool extended = false) {
	if (stackWalk64 && (!extended || symInitialize)) return true;

	HMODULE hDll = 0;

	WCHAR szTemp[4096];
	if (GetModuleFileName(NULL, szTemp, 4096) > 0) {
		wcscat_s(szTemp, L".local");
		if (GetFileAttributes(szTemp) == INVALID_FILE_ATTRIBUTES) {
			// ".local" file does not exist, so we can try to load the dbghelp.dll from the "Debugging Tools for Windows"
			if (GetEnvironmentVariable(L"ProgramFiles", szTemp, 4096) > 0) {
				wcscat_s(szTemp, L"\\Debugging Tools for Windows\\dbghelp.dll");
				// now check if the file exists:
				if (GetFileAttributes(szTemp) != INVALID_FILE_ATTRIBUTES) {
					hDll = LoadLibrary(szTemp);
				}
			}
			// Still not found? Then try to load the 64-Bit version:
			if (!hDll && (GetEnvironmentVariable(L"ProgramFiles", szTemp, 4096) > 0)) {
				wcscat_s(szTemp, L"\\Debugging Tools for Windows 64-Bit\\dbghelp.dll");
				if (GetFileAttributes(szTemp) != INVALID_FILE_ATTRIBUTES) {
					hDll = LoadLibrary(szTemp);
				}
			}
		}
	}
	if (!hDll) {
		hDll = LoadLibrary(L"DBGHELP.DLL");
	}

	if (!hDll) return false;

	stackWalk64 = (t_StackWalk64)GetProcAddress(hDll, "StackWalk64");
	symFunctionTableAccess64 = (t_SymFunctionTableAccess64)GetProcAddress(hDll, "SymFunctionTableAccess64");
	symGetModuleBase64 = (t_SymGetModuleBase64)GetProcAddress(hDll, "SymGetModuleBase64");

	if (!stackWalk64 ||
		!symFunctionTableAccess64 ||
		!symGetModuleBase64) {
		stackWalk64 = 0;
		return false;
	}

	if (extended) {
		HANDLE hProcess = GetCurrentProcess();
		DWORD dwProcessId = GetCurrentProcessId();

		symGetLineFromAddr64 = (t_SymGetLineFromAddr64)GetProcAddress(hDll, "SymGetLineFromAddrW64");
		symGetModuleInfo64 = (t_SymGetModuleInfo64)GetProcAddress(hDll, "SymGetModuleInfoW64");
		symGetSymFromAddr64 = (t_SymGetSymFromAddr64)GetProcAddress(hDll, "SymGetSymFromAddr64");
		unDecorateSymbolName = (t_UnDecorateSymbolName)GetProcAddress(hDll, "UnDecorateSymbolName");
		symInitialize = (t_SymInitialize)GetProcAddress(hDll, "SymInitializeW");
		symCleanup = (t_SymCleanup)GetProcAddress(hDll, "SymCleanup");
		symGetSearchPath = (t_SymGetSearchPath)GetProcAddress(hDll, "SymGetSearchPathW");
		symGetOptions = (t_SymGetOptions)GetProcAddress(hDll, "SymGetOptions");
		symSetOptions = (t_SymSetOptions)GetProcAddress(hDll, "SymSetOptions");
		symLoadModule64 = (t_SymLoadModule64)GetProcAddress(hDll, "SymLoadModule64");
		if (!symGetModuleInfo64 ||
			!symGetLineFromAddr64 ||
			!symGetSymFromAddr64 ||
			!unDecorateSymbolName ||
			!symInitialize ||
			!symCleanup ||
			!symGetOptions ||
			!symSetOptions ||
			!symLoadModule64) {
			symInitialize = 0;
			return false;
		}

		const size_t nSymPathLen = 10 * MAX_PATH;
		WCHAR szSymPath[nSymPathLen] = { 0 };

		wcscat_s(szSymPath, nSymPathLen, L".;..;");

		WCHAR szTemp[MAX_PATH + 1] = { 0 };
		if (GetCurrentDirectory(MAX_PATH, szTemp) > 0)	{
			wcscat_s(szSymPath, nSymPathLen, szTemp);
			wcscat_s(szSymPath, nSymPathLen, L";");
		}

		if (GetModuleFileName(NULL, szTemp, MAX_PATH) > 0) {
			for (WCHAR *p = (szTemp + wcslen(szTemp) - 1); p >= szTemp; --p) {
				if ((*p == '\\') || (*p == '/') || (*p == ':'))	{
					*p = 0;
					break;
				}
			}
			if (wcslen(szTemp) > 0)	{
				wcscat_s(szSymPath, nSymPathLen, szTemp);
				wcscat_s(szSymPath, nSymPathLen, L";");
			}
		}
		if (GetEnvironmentVariable(L"_NT_SYMBOL_PATH", szTemp, MAX_PATH) > 0) {
			wcscat_s(szSymPath, nSymPathLen, szTemp);
			wcscat_s(szSymPath, nSymPathLen, L";");
		}
		if (GetEnvironmentVariable(L"_NT_ALTERNATE_SYMBOL_PATH", szTemp, MAX_PATH) > 0) {
			wcscat_s(szSymPath, nSymPathLen, szTemp);
			wcscat_s(szSymPath, nSymPathLen, L";");
		}
		if (GetEnvironmentVariable(L"SYSTEMROOT", szTemp, MAX_PATH) > 0) {
			wcscat_s(szSymPath, nSymPathLen, szTemp);
			wcscat_s(szSymPath, nSymPathLen, L";");

			// also add the "system32"-directory:
			wcscat_s(szTemp, MAX_PATH, L"\\system32");
			wcscat_s(szSymPath, nSymPathLen, szTemp);
			wcscat_s(szSymPath, nSymPathLen, L";");
		}

		if (GetEnvironmentVariable(L"SYSTEMDRIVE", szTemp, MAX_PATH) > 0) {
			wcscat_s(szSymPath, nSymPathLen, L"SRV*");
			wcscat_s(szSymPath, nSymPathLen, szTemp);
			wcscat_s(szSymPath, nSymPathLen, L"\\websymbols*http://msdl.microsoft.com/download/symbols;");
		} else {
			wcscat_s(szSymPath, nSymPathLen, L"SRV*c:\\websymbols*http://msdl.microsoft.com/download/symbols;");
		}

		if (symInitialize(hProcess, szSymPath, FALSE) == FALSE) {
			symInitialize = 0;
			return false;
		}

		DWORD symOptions = symGetOptions();
		symOptions |= SYMOPT_LOAD_LINES;
		symOptions |= SYMOPT_FAIL_CRITICAL_ERRORS;
		symOptions = symSetOptions(symOptions);

		const WCHAR *dllname[] = { L"kernel32.dll",  L"tlhelp32.dll" };
		HINSTANCE hToolhelp = NULL;

		HANDLE hSnap;
		MODULEENTRY32 me;
		me.dwSize = sizeof(me);
		BOOL keepGoing;
		size_t i;

		for (i = 0; i < (sizeof(dllname) / sizeof(dllname[0])); i++) {
			hToolhelp = LoadLibrary(dllname[i]);
			if (!hToolhelp) continue;

			createToolhelp32Snapshot = (t_CreateToolhelp32Snapshot)GetProcAddress(hToolhelp, "CreateToolhelp32Snapshot");
			module32First = (t_Module32First)GetProcAddress(hToolhelp, "Module32First");
			module32Next = (t_Module32Next)GetProcAddress(hToolhelp, "Module32Next");
			if (createToolhelp32Snapshot && module32First && module32Next) {
				break; // found the functions!
			}
			FreeLibrary(hToolhelp);
			hToolhelp = NULL;
		}

		if (hToolhelp == NULL) {
			return false;
		}

		hSnap = createToolhelp32Snapshot(TH32CS_SNAPMODULE, dwProcessId);
		if (hSnap == (HANDLE)-1)
			return FALSE;

		keepGoing = !!module32First(hSnap, &me);
		int cnt = 0;
		while (keepGoing) {
			symLoadModule64(hProcess, 0, me.szExePath, me.szModule, (DWORD64)me.modBaseAddr, me.modBaseSize);
			++cnt;
			keepGoing = !!module32Next(hSnap, &me);
		}
		CloseHandle(hSnap);
		FreeLibrary(hToolhelp);

		return (cnt > 0);
	}

	return true;
}

struct StackEntry {
	DWORD64 offset;  // if 0, we have no valid entry
	CHAR name[StackEntryMaxNameLength];
	CHAR undName[StackEntryMaxNameLength];
	CHAR undFullName[StackEntryMaxNameLength];
	DWORD64 offsetFromSmybol;
	DWORD offsetFromLine;
	DWORD lineNumber;
	WCHAR lineFileName[StackEntryMaxNameLength];
	DWORD symType;
	LPCSTR symTypeString;
	WCHAR moduleName[StackEntryMaxNameLength];
	DWORD64 baseOfImage;
	WCHAR loadedImageName[StackEntryMaxNameLength];
};

enum StackEntryType {
	StackEntryFirst,
	StackEntryNext,
	StackEntryLast,
};

char GetModuleInfoData[2 * sizeof(IMAGEHLP_MODULEW64)];
BOOL _getModuleInfo(HANDLE hProcess, DWORD64 baseAddr, IMAGEHLP_MODULEW64 *pModuleInfo) {
	pModuleInfo->SizeOfStruct = sizeof(IMAGEHLP_MODULEW64);

	memcpy(GetModuleInfoData, pModuleInfo, sizeof(IMAGEHLP_MODULEW64));
	if (symGetModuleInfo64(hProcess, baseAddr, (IMAGEHLP_MODULEW64*)GetModuleInfoData) != FALSE) {
		// only copy as much memory as is reserved...
		memcpy(pModuleInfo, GetModuleInfoData, sizeof(IMAGEHLP_MODULEW64));
		pModuleInfo->SizeOfStruct = sizeof(IMAGEHLP_MODULEW64);
		return TRUE;
	}
	return FALSE;
}

void psWriteDump() {
}

char ImageHlpSymbol64[sizeof(IMAGEHLP_SYMBOL64) + StackEntryMaxNameLength];
QString psPrepareCrashDump(const QByteArray &crashdump, QString dumpfile) {
	if (!LoadDbgHelp(true) || cExeName().isEmpty()) {
		return qsl("ERROR: could not init dbghelp.dll!");
	}

	HANDLE hProcess = GetCurrentProcess();

	QString initial = QString::fromUtf8(crashdump), result;
	QStringList lines = initial.split('\n');
	result.reserve(initial.size());
	int32 i = 0, l = lines.size();
	QString versionstr;
	uint64 version = 0, betaversion = 0;
	for (;  i < l; ++i) {
		result.append(lines.at(i)).append('\n');
		QString line = lines.at(i).trimmed();
		if (line.startsWith(qstr("Version: "))) {
			versionstr = line.mid(qstr("Version: ").size()).trimmed();
			version = versionstr.toULongLong();
			if (versionstr.endsWith(qstr("beta"))) {
				if (version % 1000) {
					betaversion = version;
				} else {
					version /= 1000;
				}
			}
			++i;
			break;
		}
	}

	// maybe need to launch another executable
	QString tolaunch;
	if ((betaversion && betaversion != cBetaVersion()) || (!betaversion && version && version != AppVersion)) {
		QString path = cExeDir();
		QRegularExpressionMatch m = QRegularExpression("deploy/\\d+\\.\\d+/\\d+\\.\\d+\\.\\d+(/|\\.dev/|\\.alpha/|_\\d+/)(Telegram/)?$").match(path);
		if (m.hasMatch()) {
			QString base = path.mid(0, m.capturedStart()) + qstr("deploy/");
			int32 major = version / 1000000, minor = (version % 1000000) / 1000, micro = (version % 1000);
			base += qsl("%1.%2/%3.%4.%5").arg(major).arg(minor).arg(major).arg(minor).arg(micro);
			if (betaversion) {
				base += qsl("_%1").arg(betaversion);
			} else if (QDir(base + qstr(".dev")).exists()) {
				base += qstr(".dev");
			} else if (QDir(base + qstr(".alpha")).exists()) {
				base += qstr(".alpha");
			}
			if (QFile(base + qstr("/Telegram/Telegram.exe")).exists()) {
				base += qstr("/Telegram");
			}
			tolaunch = base + qstr("Telegram.exe");
		}
	}
	if (!tolaunch.isEmpty()) {
		result.append(qsl("ERROR: for this crashdump executable '%1' should be used!").arg(tolaunch));
	}

	while (i < l) {
		for (; i < l; ++i) {
			result.append(lines.at(i)).append('\n');
			QString line = lines.at(i).trimmed();
			if (line == qstr("Backtrace:")) {
				++i;
				break;
			}
		}

		IMAGEHLP_SYMBOL64 *pSym = NULL;
		IMAGEHLP_MODULEW64 Module;
		IMAGEHLP_LINEW64 Line;

		pSym = (IMAGEHLP_SYMBOL64*)ImageHlpSymbol64;
		memset(pSym, 0, sizeof(IMAGEHLP_SYMBOL64) + StackEntryMaxNameLength);
		pSym->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
		pSym->MaxNameLength = StackEntryMaxNameLength;

		memset(&Line, 0, sizeof(Line));
		Line.SizeOfStruct = sizeof(Line);

		memset(&Module, 0, sizeof(Module));
		Module.SizeOfStruct = sizeof(Module);

		StackEntry csEntry;
		for (int32 start = i; i < l; ++i) {
			QString line = lines.at(i).trimmed();
			if (line.isEmpty()) break;

			result.append(qsl("%1. ").arg(i + 1 - start));
			if (!QRegularExpression(qsl("^\\d+$")).match(line).hasMatch()) {
				if (!lines.at(i).startsWith(qstr("ERROR: "))) {
					result.append(qstr("BAD LINE: "));
				}
				result.append(line).append('\n');
				continue;
			}

			DWORD64 address = line.toULongLong();

			csEntry.offset = address;
			csEntry.name[0] = 0;
			csEntry.undName[0] = 0;
			csEntry.undFullName[0] = 0;
			csEntry.offsetFromSmybol = 0;
			csEntry.offsetFromLine = 0;
			csEntry.lineFileName[0] = 0;
			csEntry.lineNumber = 0;
			csEntry.loadedImageName[0] = 0;
			csEntry.moduleName[0] = 0;

			if (symGetSymFromAddr64(hProcess, address, &(csEntry.offsetFromSmybol), pSym) != FALSE) {
				// TODO: Mache dies sicher...!
				strcpy_s(csEntry.name, pSym->Name);

				unDecorateSymbolName(pSym->Name, csEntry.undName, StackEntryMaxNameLength, UNDNAME_NAME_ONLY);
				unDecorateSymbolName(pSym->Name, csEntry.undFullName, StackEntryMaxNameLength, UNDNAME_COMPLETE);

				if (symGetLineFromAddr64) {
					if (symGetLineFromAddr64(hProcess, address, &(csEntry.offsetFromLine), &Line) != FALSE) {
						csEntry.lineNumber = Line.LineNumber;

						// TODO: Mache dies sicher...!
						wcscpy_s(csEntry.lineFileName, Line.FileName);
					}
				}
			} else {
				result.append("ERROR: could not get Sym from Addr! for ").append(QString::number(address)).append('\n');
				continue;
			}

			if (_getModuleInfo(hProcess, address, &Module) != FALSE) {
				// TODO: Mache dies sicher...!
				wcscpy_s(csEntry.moduleName, Module.ModuleName);
			}
			if (csEntry.name[0] == 0) {
				strcpy_s(csEntry.name, "(function-name not available)");
			}
			if (csEntry.undName[0] != 0) {
				strcpy_s(csEntry.name, csEntry.undName);
			}
			if (csEntry.undFullName[0] != 0) {
				strcpy_s(csEntry.name, csEntry.undFullName);
			}
			if (csEntry.lineFileName[0] == 0) {
				if (csEntry.moduleName[0] == 0) {
					wcscpy_s(csEntry.moduleName, L"module-name not available");
				}
				result.append(csEntry.name).append(qsl(" (%1) 0x%3").arg(QString::fromWCharArray(csEntry.moduleName)).arg(address, 0, 16)).append('\n');
			} else {
				QString file = QString::fromWCharArray(csEntry.lineFileName).toLower();
				int32 index = file.indexOf(qstr("tbuild\\tdesktop\\telegram\\"));
				if (index >= 0) {
					file = file.mid(index + qstr("tbuild\\tdesktop\\telegram\\").size());
					if (file.startsWith(qstr("sourcefiles\\"))) {
						file = file.mid(qstr("sourcefiles\\").size());
					}
				}
				result.append(csEntry.name).append(qsl(" (%1 - %2) 0x%3").arg(file).arg(csEntry.lineNumber).arg(address, 0, 16)).append('\n');
			}
		}
	}

	symCleanup(hProcess);
	return result;
}

void psWriteStackTrace() {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	if (!LoadDbgHelp()) {
		SignalHandlers::dump() << "ERROR: Could not load dbghelp.dll!\n";
		return;
	}

	HANDLE hThread = GetCurrentThread(), hProcess = GetCurrentProcess();
	const CONTEXT *context = NULL;
	LPVOID pUserData = NULL;

	CONTEXT c;
	int frameNum;

	memset(&c, 0, sizeof(CONTEXT));
	c.ContextFlags = CONTEXT_FULL;
	RtlCaptureContext(&c);

	// init STACKFRAME for first call
	STACKFRAME64 s; // in/out stackframe
	memset(&s, 0, sizeof(s));
	DWORD imageType;
#ifdef _M_IX86
	// normally, call ImageNtHeader() and use machine info from PE header
	imageType = IMAGE_FILE_MACHINE_I386;
	s.AddrPC.Offset = c.Eip;
	s.AddrPC.Mode = AddrModeFlat;
	s.AddrFrame.Offset = c.Ebp;
	s.AddrFrame.Mode = AddrModeFlat;
	s.AddrStack.Offset = c.Esp;
	s.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
	imageType = IMAGE_FILE_MACHINE_AMD64;
	s.AddrPC.Offset = c.Rip;
	s.AddrPC.Mode = AddrModeFlat;
	s.AddrFrame.Offset = c.Rsp;
	s.AddrFrame.Mode = AddrModeFlat;
	s.AddrStack.Offset = c.Rsp;
	s.AddrStack.Mode = AddrModeFlat;
#elif _M_IA64
	imageType = IMAGE_FILE_MACHINE_IA64;
	s.AddrPC.Offset = c.StIIP;
	s.AddrPC.Mode = AddrModeFlat;
	s.AddrFrame.Offset = c.IntSp;
	s.AddrFrame.Mode = AddrModeFlat;
	s.AddrBStore.Offset = c.RsBSP;
	s.AddrBStore.Mode = AddrModeFlat;
	s.AddrStack.Offset = c.IntSp;
	s.AddrStack.Mode = AddrModeFlat;
#else
#error "Platform not supported!"
#endif

	for (frameNum = 0; frameNum < 1024; ++frameNum) {
		// get next stack frame (StackWalk64(), SymFunctionTableAccess64(), SymGetModuleBase64())
		// if this returns ERROR_INVALID_ADDRESS (487) or ERROR_NOACCESS (998), you can
		// assume that either you are done, or that the stack is so hosed that the next
		// deeper frame could not be found.
		// CONTEXT need not to be suplied if imageTyp is IMAGE_FILE_MACHINE_I386!
		if (!stackWalk64(imageType, hProcess, hThread, &s, &c, ReadProcessMemoryRoutine64, symFunctionTableAccess64, symGetModuleBase64, NULL)) {
			SignalHandlers::dump() << "ERROR: Call to StackWalk64() failed!\n";
			return;
		}

		if (s.AddrPC.Offset == s.AddrReturn.Offset) {
			SignalHandlers::dump() << s.AddrPC.Offset << "\n";
			SignalHandlers::dump() << "ERROR: StackWalk64() endless callstack!";
			return;
		}
		if (s.AddrPC.Offset != 0) { // we seem to have a valid PC
			SignalHandlers::dump() << s.AddrPC.Offset << "\n";
		}

		if (s.AddrReturn.Offset == 0) {
			break;
		}
	}
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
}

bool psLaunchMaps(const LocationCoords &coords) {
	return QDesktopServices::openUrl(qsl("bingmaps:?lvl=16&collection=point.%1_%2_Point").arg(coords.latAsString()).arg(coords.lonAsString()));
}
