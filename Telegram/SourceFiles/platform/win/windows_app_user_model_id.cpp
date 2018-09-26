/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_app_user_model_id.h"

#include "platform/win/windows_dlls.h"
#include <propvarutil.h>
#include <propkey.h>

#include <roapi.h>
#include <wrl\client.h>
#include <wrl\implements.h>
#include <windows.ui.notifications.h>

using namespace Microsoft::WRL;

namespace Platform {
namespace AppUserModelId {
namespace {

const PROPERTYKEY pkey_AppUserModel_ID = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };
const PROPERTYKEY pkey_AppUserModel_StartPinOption = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 12 };

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
		QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath() + qsl("/Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar/");
	}
	return QString();
}

void checkPinned() {
	if (!Dlls::PropVariantToString) return;

	static const int maxFileLen = MAX_PATH * 10;

	HRESULT hr = CoInitialize(0);
	if (!SUCCEEDED(hr)) return;

	QString path = pinnedPath();
	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	WCHAR src[MAX_PATH];
	GetModuleFileName(GetModuleHandle(0), src, MAX_PATH);
	BY_HANDLE_FILE_INFORMATION srcinfo = { 0 };
	HANDLE srcfile = CreateFile(src, 0x00, 0x00, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (srcfile == INVALID_HANDLE_VALUE) return;
	BOOL srcres = GetFileInformationByHandle(srcfile, &srcinfo);
	CloseHandle(srcfile);
	if (!srcres) return;
	LOG(("Checking..."));
	WIN32_FIND_DATA findData;
	HANDLE findHandle = FindFirstFileEx((p + L"*").c_str(), FindExInfoStandard, &findData, FindExSearchNameMatch, 0, 0);
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
			HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
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
			HANDLE dstfile = CreateFile(dst, 0x00, 0x00, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (dstfile == INVALID_HANDLE_VALUE) continue;
			BOOL dstres = GetFileInformationByHandle(dstfile, &dstinfo);
			CloseHandle(dstfile);
			if (!dstres) continue;

			if (srcinfo.dwVolumeSerialNumber == dstinfo.dwVolumeSerialNumber && srcinfo.nFileIndexLow == dstinfo.nFileIndexLow && srcinfo.nFileIndexHigh == dstinfo.nFileIndexHigh) {
				ComPtr<IPropertyStore> propertyStore;
				hr = shellLink.As(&propertyStore);
				if (!SUCCEEDED(hr)) return;

				PROPVARIANT appIdPropVar;
				hr = propertyStore->GetValue(getKey(), &appIdPropVar);
				if (!SUCCEEDED(hr)) return;
				LOG(("Reading..."));
				WCHAR already[MAX_PATH];
				hr = Dlls::PropVariantToString(appIdPropVar, already, MAX_PATH);
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
	if (errorCode && errorCode != ERROR_NO_MORE_FILES) { // everything is found
		LOG(("Init Error: could not find some files in pinned folder"));
		return;
	}
	FindClose(findHandle);
}

QString systemShortcutPath() {
	static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath() + qsl("/Microsoft/Windows/Start Menu/Programs/");
	}
	return QString();
}

void cleanupShortcut() {
	static const int maxFileLen = MAX_PATH * 10;

	QString path = systemShortcutPath() + qsl("Telegram.lnk");
	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	DWORD attributes = GetFileAttributes(p.c_str());
	if (attributes >= 0xFFFFFFF) return; // file does not exist

	ComPtr<IShellLink> shellLink;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
	if (!SUCCEEDED(hr)) return;

	ComPtr<IPersistFile> persistFile;
	hr = shellLink.As(&persistFile);
	if (!SUCCEEDED(hr)) return;

	hr = persistFile->Load(p.c_str(), STGM_READWRITE);
	if (!SUCCEEDED(hr)) return;

	WCHAR szGotPath[MAX_PATH];
	WIN32_FIND_DATA wfd;
	hr = shellLink->GetPath(szGotPath, MAX_PATH, (WIN32_FIND_DATA*)&wfd, SLGP_SHORTPATH);
	if (!SUCCEEDED(hr)) return;

	if (QDir::toNativeSeparators(cExeDir() + cExeName()).toStdWString() == szGotPath) {
		QFile().remove(path);
	}
}

bool validateShortcutAt(const QString &path) {
	static const int maxFileLen = MAX_PATH * 10;

	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	DWORD attributes = GetFileAttributes(p.c_str());
	if (attributes >= 0xFFFFFFF) return false; // file does not exist

	ComPtr<IShellLink> shellLink;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
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
	hr = propertyStore->GetValue(getKey(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	WCHAR already[MAX_PATH];
	hr = Dlls::PropVariantToString(appIdPropVar, already, MAX_PATH);
	if (SUCCEEDED(hr)) {
		if (std::wstring(getId()) == already) {
			PropVariantClear(&appIdPropVar);
			return true;
		}
	}
	if (appIdPropVar.vt != VT_EMPTY) {
		PropVariantClear(&appIdPropVar);
		return false;
	}
	PropVariantClear(&appIdPropVar);

	hr = InitPropVariantFromString(getId(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(getKey(), appIdPropVar);
	PropVariantClear(&appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->Commit();
	if (!SUCCEEDED(hr)) return false;

	if (persistFile->IsDirty() == S_OK) {
		persistFile->Save(p.c_str(), TRUE);
	}

	return true;
}

bool validateShortcut() {
	QString path = systemShortcutPath();
	if (path.isEmpty() || cExeName().isEmpty()) return false;

	if (cAlphaVersion()) {
		path += qsl("TelegramAlpha.lnk");
		if (validateShortcutAt(path)) return true;
	} else {
		if (validateShortcutAt(path + qsl("Telegram Desktop/Telegram.lnk"))) return true;
		if (validateShortcutAt(path + qsl("Telegram Win (Unofficial)/Telegram.lnk"))) return true;

		path += qsl("Telegram.lnk");
		if (validateShortcutAt(path)) return true;
	}

	ComPtr<IShellLink> shellLink;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
	if (!SUCCEEDED(hr)) return false;

	hr = shellLink->SetPath(QDir::toNativeSeparators(cExeDir() + cExeName()).toStdWString().c_str());
	if (!SUCCEEDED(hr)) return false;

	hr = shellLink->SetArguments(L"");
	if (!SUCCEEDED(hr)) return false;

	hr = shellLink->SetWorkingDirectory(QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath()).toStdWString().c_str());
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
	hr = InitPropVariantFromUInt32(APPUSERMODEL_STARTPINOPTION_NOPINONINSTALL, &startPinPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(pkey_AppUserModel_StartPinOption, startPinPropVar);
	PropVariantClear(&startPinPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->Commit();
	if (!SUCCEEDED(hr)) return false;

	ComPtr<IPersistFile> persistFile;
	hr = shellLink.As(&persistFile);
	if (!SUCCEEDED(hr)) return false;

	hr = persistFile->Save(QDir::toNativeSeparators(path).toStdWString().c_str(), TRUE);
	if (!SUCCEEDED(hr)) return false;

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
