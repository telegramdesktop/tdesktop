/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_event_filter.h"

#include "platform/win/specific_win.h"
#include "core/sandbox.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "mainwindow.h"
#include "app.h"

#include <QtGui/QWindow>

namespace Platform {
namespace {

EventFilter *instance = nullptr;

} // namespace

EventFilter *EventFilter::CreateInstance(not_null<MainWindow*> window) {
	Expects(instance == nullptr);

	return (instance = new EventFilter(window));
}

void EventFilter::Destroy() {
	Expects(instance != nullptr);

	delete instance;
	instance = nullptr;
}

EventFilter::EventFilter(not_null<MainWindow*> window) : _window(window) {
}

bool EventFilter::nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) {
	return Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		const auto msg = static_cast<MSG*>(message);
		if (msg->message == WM_ENDSESSION) {
			App::quit();
			return false;
		}
		if (msg->hwnd == _window->psHwnd()
			|| msg->hwnd && !_window->psHwnd()) {
			return mainWindowEvent(
				msg->hwnd,
				msg->message,
				msg->wParam,
				msg->lParam,
				(LRESULT*)result);
		}
		return false;
	});
}

bool EventFilter::mainWindowEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) {
	if (const auto tbCreatedMsgId = Platform::MainWindow::TaskbarCreatedMsgId()) {
		if (msg == tbCreatedMsgId) {
			Platform::MainWindow::TaskbarCreated();
		}
	}

	switch (msg) {

	case WM_TIMECHANGE: {
		Core::App().checkAutoLockIn(100);
	} return false;

	case WM_WTSSESSION_CHANGE: {
		if (wParam == WTS_SESSION_LOGOFF || wParam == WTS_SESSION_LOCK) {
			Core::App().setScreenIsLocked(true);
		} else if (wParam == WTS_SESSION_LOGON || wParam == WTS_SESSION_UNLOCK) {
			Core::App().setScreenIsLocked(false);
		}
	} return false;

	case WM_DESTROY: {
		App::quit();
	} return false;

	case WM_ACTIVATE: {
		if (LOWORD(wParam) != WA_INACTIVE) {
			_window->shadowsActivate();
		} else {
			_window->shadowsDeactivate();
		}
	} return false;

	case WM_SIZE: {
		if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED || wParam == SIZE_MINIMIZED) {
			if (wParam == SIZE_RESTORED && _window->windowState() == Qt::WindowNoState) {
				_window->positionUpdated();
			}
		}
	} return false;

	case WM_MOVE: {
		_window->positionUpdated();
	} return false;

	case WM_SETTINGCHANGE: {
		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
	} return false;

	}
	return false;
}

} // namespace Platform
