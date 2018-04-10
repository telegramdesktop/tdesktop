/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_event_filter.h"

#include "mainwindow.h"
#include "auth_session.h"

namespace Platform {
namespace {

EventFilter *instance = nullptr;

int menuShown = 0, menuHidden = 0;

} // namespace

EventFilter *EventFilter::createInstance() {
	destroy();
	instance = new EventFilter();
	return getInstance();
}

EventFilter *EventFilter::getInstance() {
	return instance;
}

void EventFilter::destroy() {
	delete instance;
	instance = nullptr;
}

bool EventFilter::nativeEventFilter(const QByteArray &eventType, void *message, long *result) {
	auto wnd = App::wnd();
	if (!wnd) return false;

	MSG *msg = (MSG*)message;
	if (msg->message == WM_ENDSESSION) {
		App::quit();
		return false;
	}
	if (msg->hwnd == wnd->psHwnd() || msg->hwnd && !wnd->psHwnd()) {
		return mainWindowEvent(msg->hwnd, msg->message, msg->wParam, msg->lParam, (LRESULT*)result);
	}
	return false;
}

bool EventFilter::mainWindowEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *result) {
	using ShadowsChange = MainWindow::ShadowsChange;

	if (auto tbCreatedMsgId = Platform::MainWindow::TaskbarCreatedMsgId()) {
		if (msg == tbCreatedMsgId) {
			Platform::MainWindow::TaskbarCreated();
		}
	}

	switch (msg) {

	case WM_TIMECHANGE: {
		if (AuthSession::Exists()) {
			Auth().checkAutoLockIn(100);
		}
	} return false;

	case WM_WTSSESSION_CHANGE: {
		if (wParam == WTS_SESSION_LOGOFF || wParam == WTS_SESSION_LOCK) {
			setSessionLoggedOff(true);
		} else if (wParam == WTS_SESSION_LOGON || wParam == WTS_SESSION_UNLOCK) {
			setSessionLoggedOff(false);
		}
	} return false;

	case WM_DESTROY: {
		App::quit();
	} return false;

	case WM_ACTIVATE: {
		if (LOWORD(wParam) == WA_CLICKACTIVE) {
			App::wnd()->setInactivePress(true);
		}
		if (LOWORD(wParam) != WA_INACTIVE) {
			App::wnd()->shadowsActivate();
		} else {
			App::wnd()->shadowsDeactivate();
		}
		if (Global::started()) {
			App::wnd()->update();
		}
	} return false;

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
				}
			}
		}
		if (result) *result = 0;
		return true;
	}

	case WM_NCACTIVATE: {
		auto res = DefWindowProc(hWnd, msg, wParam, -1);
		if (result) *result = res;
	} return true;

	case WM_WINDOWPOSCHANGING:
	case WM_WINDOWPOSCHANGED: {
		WINDOWPLACEMENT wp;
		wp.length = sizeof(WINDOWPLACEMENT);
		if (GetWindowPlacement(hWnd, &wp) && (wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_SHOWMINIMIZED)) {
			App::wnd()->shadowsUpdate(ShadowsChange::Hidden);
		} else {
			App::wnd()->shadowsUpdate(ShadowsChange::Moved | ShadowsChange::Resized, (WINDOWPOS*)lParam);
		}
	} return false;

	case WM_SIZE: {
		if (App::wnd()) {
			if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED || wParam == SIZE_MINIMIZED) {
				if (wParam != SIZE_RESTORED || App::wnd()->windowState() != Qt::WindowNoState) {
					Qt::WindowState state = Qt::WindowNoState;
					if (wParam == SIZE_MAXIMIZED) {
						state = Qt::WindowMaximized;
					} else if (wParam == SIZE_MINIMIZED) {
						state = Qt::WindowMinimized;
					}
					emit App::wnd()->windowHandle()->windowStateChanged(state);
				} else {
					App::wnd()->positionUpdated();
				}
				App::wnd()->psUpdateMargins();
				MainWindow::ShadowsChanges changes = (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED) ? ShadowsChange::Hidden : (ShadowsChange::Resized | ShadowsChange::Shown);
				App::wnd()->shadowsUpdate(changes);
			}
		}
	} return false;

	case WM_SHOWWINDOW: {
		LONG style = GetWindowLong(hWnd, GWL_STYLE);
		auto changes = ShadowsChange::Resized | ((wParam && !(style & (WS_MAXIMIZE | WS_MINIMIZE))) ? ShadowsChange::Shown : ShadowsChange::Hidden);
		App::wnd()->shadowsUpdate(changes);
	} return false;

	case WM_MOVE: {
		App::wnd()->shadowsUpdate(ShadowsChange::Moved);
		App::wnd()->positionUpdated();
	} return false;

	case WM_NCHITTEST: {
		if (!result) return false;

		POINTS p = MAKEPOINTS(lParam);
		RECT r;
		GetWindowRect(hWnd, &r);
		auto res = App::wnd()->hitTest(QPoint(p.x - r.left + App::wnd()->deltaLeft(), p.y - r.top + App::wnd()->deltaTop()));
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

	case WM_SYSCOMMAND: {
		if (wParam == SC_MOUSEMENU) {
			POINTS p = MAKEPOINTS(lParam);
			App::wnd()->updateSystemMenu(App::wnd()->windowHandle()->windowState());
			TrackPopupMenu(App::wnd()->psMenu(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, p.x, p.y, 0, hWnd, 0);
		}
	} return false;

	case WM_COMMAND: {
		if (HIWORD(wParam)) return false;
		int cmd = LOWORD(wParam);
		switch (cmd) {
		case SC_CLOSE: App::wnd()->close(); return true;
		case SC_MINIMIZE: App::wnd()->setWindowState(Qt::WindowMinimized); return true;
		case SC_MAXIMIZE: App::wnd()->setWindowState(Qt::WindowMaximized); return true;
		case SC_RESTORE: App::wnd()->setWindowState(Qt::WindowNoState); return true;
		}
	} return true;

	}
	return false;
}

} // namespace Platform
