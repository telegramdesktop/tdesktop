/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/integration_win.h"

#include "platform/platform_integration.h"
#include "platform/platform_specific.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/sandbox.h"
#include "tray.h"
#include "base/platform/win/base_windows_winrt.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QAbstractNativeEventFilter>

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
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
#endif // Qt < 6.5.0
		Core::App().tray().updateIconCounters();
		break;
	}
	return false;
}

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<WindowsIntegration>();
}

} // namespace Platform
