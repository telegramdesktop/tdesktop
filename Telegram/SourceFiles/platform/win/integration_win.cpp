/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/integration_win.h"

#include "base/platform/win/base_windows_winrt.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/sandbox.h"
#include "lang/lang_keys.h"
#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/tray_win.h"
#include "platform/platform_integration.h"
#include "platform/platform_specific.h"
#include "tray.h"
#include "styles/style_window.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QAbstractNativeEventFilter>

#include <propvarutil.h>
#include <propkey.h>

namespace Platform {

void WindowsIntegration::init() {
	QCoreApplication::instance()->installNativeEventFilter(this);
	_taskbarCreatedMsgId = RegisterWindowMessage(L"TaskbarButtonCreated");
}

ITaskbarList3 *WindowsIntegration::taskbarList() const {
	return _taskbarList.get();
}

WindowsIntegration &WindowsIntegration::Instance() {
	return static_cast<WindowsIntegration&>(Integration::Instance());
}

bool WindowsIntegration::nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) {
	return Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		const auto msg = static_cast<MSG*>(message);
		return processEvent(
			msg->hwnd,
			msg->message,
			msg->wParam,
			msg->lParam,
			(LRESULT*)result);
	});
}

void WindowsIntegration::createCustomJumpList() {
	_jumpList = base::WinRT::TryCreateInstance<ICustomDestinationList>(
		CLSID_DestinationList);
	if (_jumpList) {
		refreshCustomJumpList();
	}
}

void WindowsIntegration::refreshCustomJumpList() {
	auto added = false;
	auto maxSlots = UINT();
	auto removed = (IObjectArray*)nullptr;
	auto hr = _jumpList->BeginList(&maxSlots, IID_PPV_ARGS(&removed));
	if (!SUCCEEDED(hr)) {
		return;
	}
	const auto guard = gsl::finally([&] {
		if (added) {
			_jumpList->CommitList();
		} else {
			_jumpList->AbortList();
		}
	});

	auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return;
	}

	// Set the path to your application and the command-line argument for quitting
	const auto exe = QDir::toNativeSeparators(cExeDir() + cExeName());
	const auto dir = QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath());
	const auto icon = Tray::QuitJumpListIconPath();
	shellLink->SetArguments(L"-quit");
	shellLink->SetPath(exe.toStdWString().c_str());
	shellLink->SetWorkingDirectory(dir.toStdWString().c_str());
	shellLink->SetIconLocation(icon.toStdWString().c_str(), 0);

	if (const auto propertyStore = shellLink.try_as<IPropertyStore>()) {
		auto appIdPropVar = PROPVARIANT();
		hr = InitPropVariantFromString(
			AppUserModelId::Id().c_str(),
			&appIdPropVar);
		if (SUCCEEDED(hr)) {
			hr = propertyStore->SetValue(
				AppUserModelId::Key(),
				appIdPropVar);
			PropVariantClear(&appIdPropVar);
		}
		auto titlePropVar = PROPVARIANT();
		hr = InitPropVariantFromString(
			tr::lng_quit_from_tray(tr::now).toStdWString().c_str(),
			&titlePropVar);
		if (SUCCEEDED(hr)) {
			hr = propertyStore->SetValue(PKEY_Title, titlePropVar);
			PropVariantClear(&titlePropVar);
		}
		propertyStore->Commit();
	}

	auto collection = base::WinRT::TryCreateInstance<IObjectCollection>(
		CLSID_EnumerableObjectCollection);
	if (!collection) {
		return;
	}
	collection->AddObject(shellLink.get());

	_jumpList->AddUserTasks(collection.get());
	added = true;
}

bool WindowsIntegration::processEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) {
	if (msg && msg == _taskbarCreatedMsgId && !_taskbarList) {
		_taskbarList = base::WinRT::TryCreateInstance<ITaskbarList3>(
			CLSID_TaskbarList,
			CLSCTX_ALL);
		if (_taskbarList) {
			createCustomJumpList();
		}
	}

	switch (msg) {
	case WM_ENDSESSION:
		Core::Quit();
		break;

	case WM_TIMECHANGE:
		Core::App().checkAutoLockIn(100);
		break;

	case WM_WTSSESSION_CHANGE:
		if (wParam == WTS_SESSION_LOGOFF
			|| wParam == WTS_SESSION_LOCK) {
			Core::App().setScreenIsLocked(true);
		} else if (wParam == WTS_SESSION_LOGON
			|| wParam == WTS_SESSION_UNLOCK) {
			Core::App().setScreenIsLocked(false);
		}
		break;

	case WM_SETTINGCHANGE:
		RefreshTaskbarThemeValue();
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
#endif // Qt < 6.5.0
		Core::App().tray().updateIconCounters();
		if (_jumpList) {
			refreshCustomJumpList();
		}
		break;
	}
	return false;
}

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<WindowsIntegration>();
}

} // namespace Platform
