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
#include "core/sandbox.h"
#include "app.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QAbstractNativeEventFilter>

namespace Platform {
namespace {

class WindowsIntegration final
	: public Integration
	, public QAbstractNativeEventFilter {
public:
	void init() override;

private:
	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) override;
	bool processEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);

};

void WindowsIntegration::init() {
	QCoreApplication::instance()->installNativeEventFilter(this);
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
	switch (msg) {
	case WM_ENDSESSION:
		App::quit();
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
		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
		break;
	}
	return false;
}

} // namespace

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<WindowsIntegration>();
}

} // namespace Platform
