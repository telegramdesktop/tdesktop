/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_event_filter.h"

#include "platform/win/windows_dlls.h"
#include "platform/win/specific_win.h"
#include "core/sandbox.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "ui/inactive_press.h"
#include "mainwindow.h"
#include "app.h"

#include <QtGui/QWindow>

namespace Platform {
namespace {

EventFilter *instance = nullptr;

int menuShown = 0, menuHidden = 0;

bool IsCompositionEnabled() {
	if (!Dlls::DwmIsCompositionEnabled) {
		return false;
	}
	auto result = BOOL(FALSE);
	const auto success = (Dlls::DwmIsCompositionEnabled(&result) == S_OK);
	return success && result;
}

HWND FindTaskbarWindow(LPRECT rcMon = nullptr) {
	HWND hTaskbar = nullptr;
	RECT rcTaskbar, rcMatch;

	while ((hTaskbar = FindWindowEx(
		nullptr,
		hTaskbar,
		L"Shell_TrayWnd",
		nullptr)) != nullptr) {
		if (!rcMon) {
			break; // OK, return first found
		}
		if (GetWindowRect(hTaskbar, &rcTaskbar)
			&& IntersectRect(&rcMatch, &rcTaskbar, rcMon)) {
			break; // OK, taskbar match monitor
		}
	}

	return hTaskbar;
}

bool IsTaskbarAutoHidden(LPRECT rcMon = nullptr, PUINT pEdge = nullptr) {
	HWND hTaskbar = FindTaskbarWindow(rcMon);
	if (!hTaskbar) {
		if (pEdge) {
			*pEdge = (UINT)-1;
		}
		return false;
	}

	APPBARDATA state = {sizeof(state), hTaskbar};
	APPBARDATA pos = {sizeof(pos), hTaskbar};

	LRESULT lState = SHAppBarMessage(ABM_GETSTATE, &state);
	bool bAutoHidden = (lState & ABS_AUTOHIDE);

	if (SHAppBarMessage(ABM_GETTASKBARPOS, &pos)) {
		if (pEdge) {
			*pEdge = pos.uEdge;
		}
	} else {
		LOG(("Failed to get taskbar pos"));
		if (pEdge) {
			*pEdge = ABE_BOTTOM;
		}
	}

	return bAutoHidden;
}

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

bool EventFilter::customWindowFrameEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) {
	switch (msg) {
	case WM_NCPAINT: {
		if (QSysInfo::WindowsVersion >= QSysInfo::WV_WINDOWS8) return false;
		if (result) *result = 0;
	} return true;

	case WM_NCCALCSIZE: {
		WINDOWPLACEMENT wp;
		wp.length = sizeof(WINDOWPLACEMENT);
		if (GetWindowPlacement(hWnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) {
			LPNCCALCSIZE_PARAMS params = (LPNCCALCSIZE_PARAMS)lParam;
			LPRECT r = (wParam == TRUE) ? &params->rgrc[0] : (LPRECT)lParam;
			HMONITOR hMonitor = MonitorFromPoint({ (r->left + r->right) / 2, (r->top + r->bottom) / 2 }, MONITOR_DEFAULTTONEAREST);
			if (hMonitor) {
				MONITORINFO mi;
				mi.cbSize = sizeof(mi);
				if (GetMonitorInfo(hMonitor, &mi)) {
					*r = mi.rcWork;
					UINT uEdge = (UINT)-1;
					if (IsTaskbarAutoHidden(&mi.rcMonitor, &uEdge)) {
						switch (uEdge) {
						case ABE_LEFT: r->left += 1; break;
						case ABE_RIGHT: r->right -= 1; break;
						case ABE_TOP: r->top += 1; break;
						case ABE_BOTTOM: r->bottom -= 1; break;
						}
					}
				}
			}
		}
		if (result) *result = 0;
		return true;
	}

	case WM_NCACTIVATE: {
		if (IsCompositionEnabled()) {
			const auto res = DefWindowProc(hWnd, msg, wParam, -1);
			if (result) *result = res;
		} else {
			// Thanks https://github.com/melak47/BorderlessWindow
			if (result) *result = 1;
		}
	} return true;

	case WM_NCHITTEST: {
		if (!result) return false;

		POINTS p = MAKEPOINTS(lParam);
		RECT r;
		GetWindowRect(hWnd, &r);
		auto res = _window->hitTest(QPoint(p.x - r.left + _window->deltaLeft(), p.y - r.top + _window->deltaTop()));
		switch (res) {
		case Window::HitTestResult::Client:
		case Window::HitTestResult::SysButton:   *result = HTCLIENT; break;
		case Window::HitTestResult::Caption:     *result = HTCAPTION; break;
		case Window::HitTestResult::Top:         *result = HTTOP; break;
		case Window::HitTestResult::TopRight:    *result = HTTOPRIGHT; break;
		case Window::HitTestResult::Right:       *result = HTRIGHT; break;
		case Window::HitTestResult::BottomRight: *result = HTBOTTOMRIGHT; break;
		case Window::HitTestResult::Bottom:      *result = HTBOTTOM; break;
		case Window::HitTestResult::BottomLeft:  *result = HTBOTTOMLEFT; break;
		case Window::HitTestResult::Left:        *result = HTLEFT; break;
		case Window::HitTestResult::TopLeft:     *result = HTTOPLEFT; break;
		case Window::HitTestResult::None:
		default:                                 *result = HTTRANSPARENT; break;
		};
	} return true;

	case WM_NCRBUTTONUP: {
		SendMessage(hWnd, WM_SYSCOMMAND, SC_MOUSEMENU, lParam);
	} return true;
	}

	return false;
}

bool EventFilter::mainWindowEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) {
	using Change = Ui::Platform::WindowShadow::Change;

	if (const auto tbCreatedMsgId = Platform::MainWindow::TaskbarCreatedMsgId()) {
		if (msg == tbCreatedMsgId) {
			Platform::MainWindow::TaskbarCreated();
		}
	}

	if (!Core::App().settings().nativeWindowFrame()) {
		if (customWindowFrameEvent(hWnd, msg, wParam, lParam, result)) {
			return true;
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
		if (LOWORD(wParam) == WA_CLICKACTIVE) {
			Ui::MarkInactivePress(_window, true);
		}
		if (LOWORD(wParam) != WA_INACTIVE) {
			_window->shadowsActivate();
		} else {
			_window->shadowsDeactivate();
		}
		_window->update();
	} return false;

	case WM_WINDOWPOSCHANGING:
	case WM_WINDOWPOSCHANGED: {
		WINDOWPLACEMENT wp;
		wp.length = sizeof(WINDOWPLACEMENT);
		if (_window->hasTabletView()
			|| (GetWindowPlacement(hWnd, &wp)
				&& (wp.showCmd == SW_SHOWMAXIMIZED
					|| wp.showCmd == SW_SHOWMINIMIZED))) {
			_window->shadowsUpdate(Change::Hidden);
		} else {
			_window->shadowsUpdate(Change::Moved | Change::Resized, (WINDOWPOS*)lParam);
		}
	} return false;

	case WM_SIZE: {
		if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED || wParam == SIZE_MINIMIZED) {
			if (wParam != SIZE_RESTORED || _window->windowState() != Qt::WindowNoState) {
				Qt::WindowState state = Qt::WindowNoState;
				if (wParam == SIZE_MAXIMIZED) {
					state = Qt::WindowMaximized;
				} else if (wParam == SIZE_MINIMIZED) {
					state = Qt::WindowMinimized;
				}
				_window->windowHandle()->windowStateChanged(state);
			} else {
				_window->positionUpdated();
			}
			_window->updateCustomMargins();
			const auto changes = (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED) ? Change::Hidden : (Change::Resized | Change::Shown);
			_window->shadowsUpdate(changes);
		}
	} return false;

	case WM_SHOWWINDOW: {
		LONG style = GetWindowLongPtr(hWnd, GWL_STYLE);
		const auto changes = Change::Resized | ((wParam && !(style & (WS_MAXIMIZE | WS_MINIMIZE))) ? Change::Shown : Change::Hidden);
		_window->shadowsUpdate(changes);
	} return false;

	case WM_MOVE: {
		_window->shadowsUpdate(Change::Moved);
		_window->positionUpdated();
	} return false;

	case WM_SYSCOMMAND: {
		if (wParam == SC_MOUSEMENU) {
			POINTS p = MAKEPOINTS(lParam);
			_window->updateSystemMenu(_window->windowHandle()->windowState());
			TrackPopupMenu(_window->psMenu(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, p.x, p.y, 0, hWnd, 0);
		}
	} return false;

	case WM_COMMAND: {
		if (HIWORD(wParam)) {
			return false;
		}
		int cmd = LOWORD(wParam);
		switch (cmd) {
		case SC_CLOSE:
			_window->close();
			return true;
		case SC_MINIMIZE:
			_window->setWindowState(
				_window->windowState() | Qt::WindowMinimized);
			return true;
		case SC_MAXIMIZE:
			_window->setWindowState(Qt::WindowMaximized);
			return true;
		case SC_RESTORE:
			_window->setWindowState(Qt::WindowNoState);
			return true;
		}
	} return true;

	case WM_SETTINGCHANGE: {
		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
	} return false;

	}
	return false;
}

} // namespace Platform
