/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_app_user_model_id.h"

#include "platform/win/windows_dlls.h"
#include "platform/win/windows_toast_activator.h"
#include "base/platform/win/base_windows_wrl.h"

#include <propvarutil.h>
#include <propkey.h>

using namespace Microsoft::WRL;

namespace Platform {
namespace AppUserModelId {
namespace {

const PROPERTYKEY pkey_AppUserModel_ID = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };
const PROPERTYKEY pkey_AppUserModel_StartPinOption = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 12 };
const PROPERTYKEY pkey_AppUserModel_ToastActivator = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 26 };

#ifdef OS_WIN_STORE
const WCHAR AppUserModelIdRelease[] = L"Telegram.TelegramDesktop.Store";
#else // OS_WIN_STORE
const WCHAR AppUserModelIdRelease[] = L"Telegram.TelegramDesktop";
#endif // OS_WIN_STORE
const WCHAR AppUserModelIdAlpha[] = L"Telegram.TelegramDesktop.Alpha";

} // namespace

QString pinnedPath() {
	static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		auto appData = QDir(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath()
			+ u"/Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar/"_q;
	}
	return QString();
}

void checkPinned() {
	static const int maxFileLen = MAX_PATH * 10;

	HRESULT hr = CoInitialize(0);
	if (!SUCCEEDED(hr)) return;

	QString path = pinnedPath();
	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	WCHAR src[MAX_PATH];
	GetModuleFileName(GetModuleHandle(0), src, MAX_PATH);
	BY_HANDLE_FILE_INFORMATION srcinfo = { 0 };
	HANDLE srcfile = CreateFile(
		src,
		0x00,
		0x00,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (srcfile == INVALID_HANDLE_VALUE) return;
	BOOL srcres = GetFileInformationByHandle(srcfile, &srcinfo);
	CloseHandle(srcfile);
	if (!srcres) return;
	LOG(("Checking..."));
	WIN32_FIND_DATA findData;
	HANDLE findHandle = FindFirstFileEx(
		(p + L"*").c_str(),
		FindExInfoStandard,
		&findData,
		FindExSearchNameMatch,
		0,
		0);
	if (findHandle == INVALID_HANDLE_VALUE) {
		LOG(("Init Error: could not find files in pinned folder"));
		return;
	}
	do {
		std::wstring fname = p + findData.cFileName;
		LOG(("Checking %1").arg(QString::fromStdWString(fname)));
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		} else {
			DWORD attributes = GetFileAttributes(fname.c_str());
			if (attributes >= 0xFFFFFFF) continue; // file does not exist

			ComPtr<IShellLink> shellLink;
			HRESULT hr = CoCreateInstance(
				CLSID_ShellLink,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&shellLink));
			if (!SUCCEEDED(hr)) continue;

			ComPtr<IPersistFile> persistFile;
			hr = shellLink.As(&persistFile);
			if (!SUCCEEDED(hr)) continue;

			hr = persistFile->Load(fname.c_str(), STGM_READWRITE);
			if (!SUCCEEDED(hr)) continue;

			WCHAR dst[MAX_PATH];
			hr = shellLink->GetPath(dst, MAX_PATH, 0, 0);
			if (!SUCCEEDED(hr)) continue;

			BY_HANDLE_FILE_INFORMATION dstinfo = { 0 };
			HANDLE dstfile = CreateFile(
				dst,
				0x00,
				0x00,
				NULL,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				NULL);
			if (dstfile == INVALID_HANDLE_VALUE) continue;
			BOOL dstres = GetFileInformationByHandle(dstfile, &dstinfo);
			CloseHandle(dstfile);
			if (!dstres) continue;

			if (srcinfo.dwVolumeSerialNumber == dstinfo.dwVolumeSerialNumber
				&& srcinfo.nFileIndexLow == dstinfo.nFileIndexLow
				&& srcinfo.nFileIndexHigh == dstinfo.nFileIndexHigh) {
				ComPtr<IPropertyStore> propertyStore;
				hr = shellLink.As(&propertyStore);
				if (!SUCCEEDED(hr)) return;

				PROPVARIANT appIdPropVar;
				hr = propertyStore->GetValue(getKey(), &appIdPropVar);
				if (!SUCCEEDED(hr)) return;
				LOG(("Reading..."));
				WCHAR already[MAX_PATH];
				hr = PropVariantToString(appIdPropVar, already, MAX_PATH);
				if (SUCCEEDED(hr)) {
					if (std::wstring(getId()) == already) {
						LOG(("Already!"));
						PropVariantClear(&appIdPropVar);
						return;
					}
				}
				if (appIdPropVar.vt != VT_EMPTY) {
					PropVariantClear(&appIdPropVar);
					return;
				}
				PropVariantClear(&appIdPropVar);

				hr = InitPropVariantFromString(getId(), &appIdPropVar);
				if (!SUCCEEDED(hr)) return;

				hr = propertyStore->SetValue(getKey(), appIdPropVar);
				PropVariantClear(&appIdPropVar);
				if (!SUCCEEDED(hr)) return;

				hr = propertyStore->Commit();
				if (!SUCCEEDED(hr)) return;

				if (persistFile->IsDirty() == S_OK) {
					persistFile->Save(fname.c_str(), TRUE);
				}
				return;
			}
		}
	} while (FindNextFile(findHandle, &findData));
	DWORD errorCode = GetLastError();
	if (errorCode && errorCode != ERROR_NO_MORE_FILES) {
		LOG(("Init Error: could not find some files in pinned folder"));
		return;
	}
	FindClose(findHandle);
}

QString systemShortcutPath() {
	static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		auto appData = QDir(QString::fromStdWString(std::wstring(wstrPath)));
		const auto path = appData.absolutePath();
		return path + u"/Microsoft/Windows/Start Menu/Programs/"_q;
	}
	return QString();
}

void cleanupShortcut() {
	static const int maxFileLen = MAX_PATH * 10;

	QString path = systemShortcutPath() + u"Telegram.lnk"_q;
	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	DWORD attributes = GetFileAttributes(p.c_str());
	if (attributes >= 0xFFFFFFF) return; // file does not exist

	ComPtr<IShellLink> shellLink;
	HRESULT hr = CoCreateInstance(
		CLSID_ShellLink,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&shellLink));
	if (!SUCCEEDED(hr)) return;

	ComPtr<IPersistFile> persistFile;
	hr = shellLink.As(&persistFile);
	if (!SUCCEEDED(hr)) return;

	hr = persistFile->Load(p.c_str(), STGM_READWRITE);
	if (!SUCCEEDED(hr)) return;

	WCHAR szGotPath[MAX_PATH];
	WIN32_FIND_DATA wfd;
	hr = shellLink->GetPath(
		szGotPath,
		MAX_PATH,
		(WIN32_FIND_DATA*)&wfd,
		SLGP_SHORTPATH);
	if (!SUCCEEDED(hr)) return;

	const auto full = cExeDir() + cExeName();
	if (QDir::toNativeSeparators(full).toStdWString() == szGotPath) {
		QFile().remove(path);
	}
}

bool validateShortcutAt(const QString &path) {
	static const int maxFileLen = MAX_PATH * 10;

	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	DWORD attributes = GetFileAttributes(p.c_str());
	if (attributes >= 0xFFFFFFF) return false; // file does not exist

	ComPtr<IShellLink> shellLink;
	HRESULT hr = CoCreateInstance(
		CLSID_ShellLink,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&shellLink));
	if (!SUCCEEDED(hr)) return false;

	ComPtr<IPersistFile> persistFile;
	hr = shellLink.As(&persistFile);
	if (!SUCCEEDED(hr)) return false;

	hr = persistFile->Load(p.c_str(), STGM_READWRITE);
	if (!SUCCEEDED(hr)) return false;

	ComPtr<IPropertyStore> propertyStore;
	hr = shellLink.As(&propertyStore);
	if (!SUCCEEDED(hr)) return false;

	PROPVARIANT appIdPropVar;
	PROPVARIANT toastActivatorPropVar;
	hr = propertyStore->GetValue(getKey(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->GetValue(
		pkey_AppUserModel_ToastActivator,
		&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	WCHAR already[MAX_PATH];
	hr = PropVariantToString(appIdPropVar, already, MAX_PATH);
	const auto good1 = SUCCEEDED(hr) && (std::wstring(getId()) == already);
	const auto bad1 = !good1 && (appIdPropVar.vt != VT_EMPTY);
	PropVariantClear(&appIdPropVar);

	auto clsid = CLSID();
	hr = PropVariantToCLSID(toastActivatorPropVar, &clsid);
	const auto good2 = SUCCEEDED(hr) && (clsid == __uuidof(ToastActivator));
	const auto bad2 = !good2 && (toastActivatorPropVar.vt != VT_EMPTY);
	PropVariantClear(&toastActivatorPropVar);
	if (good1 && good2) {
		LOG(("App Info: Shortcut validated at \"%1\"").arg(path));
		return true;
	} else if (bad1 || bad2) {
		return false;
	}

	hr = InitPropVariantFromString(getId(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(getKey(), appIdPropVar);
	PropVariantClear(&appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = InitPropVariantFromCLSID(
		__uuidof(ToastActivator),
		&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(
		pkey_AppUserModel_ToastActivator,
		toastActivatorPropVar);
	PropVariantClear(&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->Commit();
	if (!SUCCEEDED(hr)) return false;

	if (persistFile->IsDirty() == S_OK) {
		hr = persistFile->Save(p.c_str(), TRUE);
		if (!SUCCEEDED(hr)) return false;
	}

	LOG(("App Info: Shortcut set and validated at \"%1\"").arg(path));
	return true;
}

bool validateShortcut() {
	QString path = systemShortcutPath();
	if (path.isEmpty() || cExeName().isEmpty()) {
		return false;
	}

	if (cAlphaVersion()) {
		path += u"TelegramAlpha.lnk"_q;
		if (validateShortcutAt(path)) {
			return true;
		}
	} else {
		const auto installed = u"Telegram Desktop/Telegram.lnk"_q;
		const auto old = u"Telegram Win (Unofficial)/Telegram.lnk"_q;
		if (validateShortcutAt(path + installed)
			|| validateShortcutAt(path + old)) {
			return true;
		}

		path += u"Telegram.lnk"_q;
		if (validateShortcutAt(path)) {
			return true;
		}
	}

	ComPtr<IShellLink> shellLink;
	HRESULT hr = CoCreateInstance(
		CLSID_ShellLink,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&shellLink));
	if (!SUCCEEDED(hr)) return false;

	hr = shellLink->SetPath(
		QDir::toNativeSeparators(
			cExeDir() + cExeName()).toStdWString().c_str());
	if (!SUCCEEDED(hr)) return false;

	hr = shellLink->SetArguments(L"");
	if (!SUCCEEDED(hr)) return false;

	hr = shellLink->SetWorkingDirectory(
		QDir::toNativeSeparators(
			QDir(cWorkingDir()).absolutePath()).toStdWString().c_str());
	if (!SUCCEEDED(hr)) return false;

	ComPtr<IPropertyStore> propertyStore;
	hr = shellLink.As(&propertyStore);
	if (!SUCCEEDED(hr)) return false;

	PROPVARIANT appIdPropVar;
	hr = InitPropVariantFromString(getId(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(getKey(), appIdPropVar);
	PropVariantClear(&appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	PROPVARIANT startPinPropVar;
	hr = InitPropVariantFromUInt32(
		APPUSERMODEL_STARTPINOPTION_NOPINONINSTALL,
		&startPinPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(
		pkey_AppUserModel_StartPinOption,
		startPinPropVar);
	PropVariantClear(&startPinPropVar);
	if (!SUCCEEDED(hr)) return false;

	PROPVARIANT toastActivatorPropVar{};
	hr = InitPropVariantFromCLSID(
		__uuidof(ToastActivator),
		&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(
		pkey_AppUserModel_ToastActivator,
		toastActivatorPropVar);
	PropVariantClear(&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->Commit();
	if (!SUCCEEDED(hr)) return false;

	ComPtr<IPersistFile> persistFile;
	hr = shellLink.As(&persistFile);
	if (!SUCCEEDED(hr)) return false;

	hr = persistFile->Save(
		QDir::toNativeSeparators(path).toStdWString().c_str(),
		TRUE);
	if (!SUCCEEDED(hr)) return false;

	LOG(("App Info: Shortcut created and validated at \"%1\"").arg(path));
	return true;
}

const WCHAR *getId() {
	return cAlphaVersion() ? AppUserModelIdAlpha : AppUserModelIdRelease;
}

const PROPERTYKEY &getKey() {
	return pkey_AppUserModel_ID;
}

} // namespace AppUserModelId
} // namespace Platform
