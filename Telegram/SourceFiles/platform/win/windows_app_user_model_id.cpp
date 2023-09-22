/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_app_user_model_id.h"

#include "platform/win/windows_dlls.h"
#include "platform/win/windows_toast_activator.h"
#include "base/platform/win/base_windows_winrt.h"
#include "core/launcher.h"

#include <propvarutil.h>
#include <propkey.h>

namespace Platform {
namespace AppUserModelId {
namespace {

constexpr auto kMaxFileLen = MAX_PATH * 2;

const PROPERTYKEY pkey_AppUserModel_ID = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };
const PROPERTYKEY pkey_AppUserModel_StartPinOption = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 12 };
const PROPERTYKEY pkey_AppUserModel_ToastActivator = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 26 };

#ifdef OS_WIN_STORE
const WCHAR AppUserModelIdBase[] = L"Telegram.TelegramDesktop.Store";
#else // OS_WIN_STORE
const WCHAR AppUserModelIdBase[] = L"Telegram.TelegramDesktop";
#endif // OS_WIN_STORE

[[nodiscard]] QString PinnedIconsPath() {
	WCHAR wstrPath[kMaxFileLen] = {};
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, kMaxFileLen)) {
		auto appData = QDir(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath()
			+ u"/Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar/"_q;
	}
	return QString();
}

} // namespace

const std::wstring &MyExecutablePath() {
	static const auto Path = [&] {
		auto result = std::wstring(kMaxFileLen, 0);
		const auto length = GetModuleFileName(
			GetModuleHandle(nullptr),
			result.data(),
			kMaxFileLen);
		if (!length || length == kMaxFileLen) {
			result.clear();
		} else {
			result.resize(length + 1);
		}
		return result;
	}();
	return Path;
}

UniqueFileId MyExecutablePathId() {
	return GetUniqueFileId(MyExecutablePath().c_str());
}

UniqueFileId GetUniqueFileId(LPCWSTR path) {
	auto info = BY_HANDLE_FILE_INFORMATION{};
	const auto file = CreateFile(
		path,
		0,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}
	const auto result = GetFileInformationByHandle(file, &info);
	CloseHandle(file);
	if (!result) {
		return {};
	}
	return {
		.part1 = info.dwVolumeSerialNumber,
		.part2 = ((std::uint64_t(info.nFileIndexLow) << 32)
			| std::uint64_t(info.nFileIndexHigh)),
	};
}

void CheckPinned() {
	if (!SUCCEEDED(CoInitialize(0))) {
		return;
	}
	const auto coGuard = gsl::finally([] {
		CoUninitialize();
	});

	const auto path = PinnedIconsPath();
	const auto native = QDir::toNativeSeparators(path).toStdWString();

	const auto srcid = MyExecutablePathId();
	if (!srcid) {
		return;
	}

	LOG(("Checking..."));
	WIN32_FIND_DATA findData;
	HANDLE findHandle = FindFirstFileEx(
		(native + L"*").c_str(),
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
		std::wstring fname = native + findData.cFileName;
		LOG(("Checking %1").arg(QString::fromStdWString(fname)));
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		} else {
			DWORD attributes = GetFileAttributes(fname.c_str());
			if (attributes >= 0xFFFFFFF) {
				continue; // file does not exist
			}

			auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
				CLSID_ShellLink);
			if (!shellLink) {
				continue;
			}

			auto persistFile = shellLink.try_as<IPersistFile>();
			if (!persistFile) {
				continue;
			}

			auto hr = persistFile->Load(fname.c_str(), STGM_READWRITE);
			if (!SUCCEEDED(hr)) continue;

			WCHAR dst[MAX_PATH] = { 0 };
			hr = shellLink->GetPath(dst, MAX_PATH, nullptr, 0);
			if (!SUCCEEDED(hr)) continue;

			if (GetUniqueFileId(dst) == srcid) {
				auto propertyStore = shellLink.try_as<IPropertyStore>();
				if (!propertyStore) {
					return;
				}

				PROPVARIANT appIdPropVar;
				hr = propertyStore->GetValue(Key(), &appIdPropVar);
				if (!SUCCEEDED(hr)) return;
				LOG(("Reading..."));
				WCHAR already[MAX_PATH];
				hr = PropVariantToString(appIdPropVar, already, MAX_PATH);
				if (SUCCEEDED(hr)) {
					if (Id() == already) {
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

				hr = InitPropVariantFromString(Id().c_str(), &appIdPropVar);
				if (!SUCCEEDED(hr)) return;

				hr = propertyStore->SetValue(Key(), appIdPropVar);
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
	WCHAR wstrPath[kMaxFileLen] = {};
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, kMaxFileLen)) {
		auto appData = QDir(QString::fromStdWString(std::wstring(wstrPath)));
		const auto path = appData.absolutePath();
		return path + u"/Microsoft/Windows/Start Menu/Programs/"_q;
	}
	return QString();
}

void CleanupShortcut() {
	const auto myid = MyExecutablePathId();
	if (!myid) {
		return;
	}

	QString path = systemShortcutPath() + u"Telegram.lnk"_q;
	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	DWORD attributes = GetFileAttributes(p.c_str());
	if (attributes >= 0xFFFFFFF) return; // file does not exist

	auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return;
	}

	auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile) {
		return;
	}

	auto hr = persistFile->Load(p.c_str(), STGM_READWRITE);
	if (!SUCCEEDED(hr)) return;

	WCHAR szGotPath[MAX_PATH];
	hr = shellLink->GetPath(szGotPath, MAX_PATH, nullptr, 0);
	if (!SUCCEEDED(hr)) return;

	if (GetUniqueFileId(szGotPath) == myid) {
		QFile().remove(path);
	}
}

bool validateShortcutAt(const QString &path) {
	const auto native = QDir::toNativeSeparators(path).toStdWString();

	DWORD attributes = GetFileAttributes(native.c_str());
	if (attributes >= 0xFFFFFFF) {
		return false; // file does not exist
	}

	auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return false;
	}

	auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile) {
		return false;
	}

	auto hr = persistFile->Load(native.c_str(), STGM_READWRITE);
	if (!SUCCEEDED(hr)) return false;

	WCHAR szGotPath[kMaxFileLen] = { 0 };
	hr = shellLink->GetPath(szGotPath, kMaxFileLen, nullptr, 0);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	if (GetUniqueFileId(szGotPath) != MyExecutablePathId()) {
		return false;
	}

	auto propertyStore = shellLink.try_as<IPropertyStore>();
	if (!propertyStore) {
		return false;
	}

	PROPVARIANT appIdPropVar;
	PROPVARIANT toastActivatorPropVar;
	hr = propertyStore->GetValue(Key(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->GetValue(
		pkey_AppUserModel_ToastActivator,
		&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	WCHAR already[MAX_PATH];
	hr = PropVariantToString(appIdPropVar, already, MAX_PATH);
	const auto good1 = SUCCEEDED(hr) && (Id() == already);
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

	hr = InitPropVariantFromString(Id().c_str(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(Key(), appIdPropVar);
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
		hr = persistFile->Save(native.c_str(), TRUE);
		if (!SUCCEEDED(hr)) return false;
	}

	LOG(("App Info: Shortcut set and validated at \"%1\"").arg(path));
	return true;
}

bool checkInstalled(QString path = {}) {
	if (path.isEmpty()) {
		path = systemShortcutPath();
		if (path.isEmpty()) {
			return false;
		}
	}

	const auto installed = u"Telegram Desktop/Telegram.lnk"_q;
	const auto old = u"Telegram Win (Unofficial)/Telegram.lnk"_q;
	return validateShortcutAt(path + installed)
		|| validateShortcutAt(path + old);
}

bool ValidateShortcut() {
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
		if (checkInstalled(path)) {
			return true;
		}

		path += u"Telegram.lnk"_q;
		if (validateShortcutAt(path)) {
			return true;
		}
	}

	auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return false;
	}

	auto hr = shellLink->SetPath(MyExecutablePath().c_str());
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = shellLink->SetArguments(L"");
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = shellLink->SetWorkingDirectory(
		QDir::toNativeSeparators(
			QDir(cWorkingDir()).absolutePath()).toStdWString().c_str());
	if (!SUCCEEDED(hr)) {
		return false;
	}

	auto propertyStore = shellLink.try_as<IPropertyStore>();
	if (!propertyStore) {
		return false;
	}

	PROPVARIANT appIdPropVar;
	hr = InitPropVariantFromString(Id().c_str(), &appIdPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = propertyStore->SetValue(Key(), appIdPropVar);
	PropVariantClear(&appIdPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	PROPVARIANT startPinPropVar;
	hr = InitPropVariantFromUInt32(
		APPUSERMODEL_STARTPINOPTION_NOPINONINSTALL,
		&startPinPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = propertyStore->SetValue(
		pkey_AppUserModel_StartPinOption,
		startPinPropVar);
	PropVariantClear(&startPinPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	PROPVARIANT toastActivatorPropVar{};
	hr = InitPropVariantFromCLSID(
		__uuidof(ToastActivator),
		&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = propertyStore->SetValue(
		pkey_AppUserModel_ToastActivator,
		toastActivatorPropVar);
	PropVariantClear(&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = propertyStore->Commit();
	if (!SUCCEEDED(hr)) {
		return false;
	}

	auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile) {
		return false;
	}

	hr = persistFile->Save(
		QDir::toNativeSeparators(path).toStdWString().c_str(),
		TRUE);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	LOG(("App Info: Shortcut created and validated at \"%1\"").arg(path));
	return true;
}

const std::wstring &Id() {
	static const auto BaseId = std::wstring(AppUserModelIdBase);
	static auto CheckingInstalled = false;
	if (CheckingInstalled) {
		return BaseId;
	}
	static const auto Installed = [] {
#ifdef OS_WIN_STORE
		return true;
#else // OS_WIN_STORE
		CheckingInstalled = true;
		const auto guard = gsl::finally([] {
			CheckingInstalled = false;
		});
		if (!SUCCEEDED(CoInitialize(nullptr))) {
			return false;
		}
		const auto coGuard = gsl::finally([] {
			CoUninitialize();
		});
		return checkInstalled();
#endif
	}();
	if (Installed) {
		return BaseId;
	}
	static const auto PortableId = [] {
		std::string h(32, 0);
		if (Core::Launcher::Instance().customWorkingDir()) {
			const auto d = QFile::encodeName(QDir(cWorkingDir()).absolutePath());
			hashMd5Hex(d.constData(), d.size(), h.data());
		} else {
			const auto exePath = QFile::encodeName(cExeDir() + cExeName());
			hashMd5Hex(exePath.constData(), exePath.size(), h.data());
		}
		return BaseId + L'.' + std::wstring(h.begin(), h.end());
	}();
	return PortableId;
}

const PROPERTYKEY &Key() {
	return pkey_AppUserModel_ID;
}

} // namespace AppUserModelId
} // namespace Platform
