/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/main_window_win.h"

#include "styles/style_window.h"
#include "platform/platform_specific.h"
#include "platform/platform_notifications_manager.h"
#include "platform/win/tray_win.h"
#include "platform/win/windows_dlls.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "base/crc32hash.h"
#include "base/platform/win/base_windows_wrl.h"
#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "ui/widgets/popup_menu.h"
#include "ui/ui_utility.h"
#include "window/themes/window_theme.h"
#include "history/history.h"

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <QtGui/QScreen>

#include <Shobjidl.h>
#include <shellapi.h>
#include <WtsApi32.h>
#include <dwmapi.h>

#include <windows.ui.viewmanagement.h>
#include <UIViewSettingsInterop.h>

#include <Windowsx.h>
#include <VersionHelpers.h>

HICON qt_pixmapToWinHICON(const QPixmap &);

namespace ViewManagement = ABI::Windows::UI::ViewManagement;

namespace Platform {
namespace {

// Mouse down on tray icon deactivates the application.
// So there is no way to know for sure if the tray icon was clicked from
// active application or from inactive application. So we assume that
// if the application was deactivated less than 0.5s ago, then the tray
// icon click (both left or right button) was made from the active app.
constexpr auto kKeepActiveForTrayIcon = crl::time(500);

class EventFilter final : public QAbstractNativeEventFilter {
public:
	explicit EventFilter(not_null<MainWindow*> window);

private:
	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) override;

	bool mainWindowEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);

	const not_null<MainWindow*> _window;

};

using namespace Microsoft::WRL;

ComPtr<ITaskbarList3> taskbarList;
bool handleSessionNotification = false;
uint32 kTaskbarCreatedMsgId = 0;

[[nodiscard]] HICON NativeIcon(const QIcon &icon, QSize size) {
	if (!icon.isNull()) {
		const auto pixmap = icon.pixmap(icon.actualSize(size));
		if (!pixmap.isNull()) {
			return qt_pixmapToWinHICON(pixmap);
		}
	}
	return nullptr;
}

EventFilter::EventFilter(not_null<MainWindow*> window) : _window(window) {
}

bool EventFilter::nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) {
	return Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		const auto msg = static_cast<MSG*>(message);
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
	if (const auto tbCreatedMsgId = kTaskbarCreatedMsgId) {
		if (msg == tbCreatedMsgId) {
			HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&taskbarList));
			if (!SUCCEEDED(hr)) {
				taskbarList.Reset();
			}
		}
	}

	switch (msg) {

	case WM_DESTROY: {
		_window->destroyedFromSystem();
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

	}
	return false;
}

} // namespace

struct MainWindow::Private {
	explicit Private(not_null<MainWindow*> window) : filter(window) {
	}

	EventFilter filter;
	ComPtr<ViewManagement::IUIViewSettings> viewSettings;
};

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller)
, _private(std::make_unique<Private>(this))
, _taskbarHiderWindow(std::make_unique<QWindow>()) {
	qApp->installNativeEventFilter(&_private->filter);

	if (!kTaskbarCreatedMsgId) {
		kTaskbarCreatedMsgId = RegisterWindowMessage(L"TaskbarButtonCreated");
	}
	setupNativeWindowFrame();

	using namespace rpl::mappers;
	Core::App().appDeactivatedValue(
	) | rpl::distinct_until_changed(
	) | rpl::filter(_1) | rpl::start_with_next([=] {
		_lastDeactivateTime = crl::now();
	}, lifetime());
}

void MainWindow::setupNativeWindowFrame() {
	auto nativeFrame = rpl::single(
		Core::App().settings().nativeWindowFrame()
	) | rpl::then(
		Core::App().settings().nativeWindowFrameChanges()
	);

	rpl::combine(
		std::move(nativeFrame),
		Window::Theme::IsNightModeValue()
	) | rpl::skip(1) | rpl::start_with_next([=](bool native, bool night) {
		validateWindowTheme(native, night);
	}, lifetime());
}

void MainWindow::shadowsActivate() {
	_hasActiveFrame = true;
}

void MainWindow::shadowsDeactivate() {
	_hasActiveFrame = false;
}

void MainWindow::destroyedFromSystem() {
	if (isPrimary()) {
		Core::Quit();
	} else {
		crl::on_main(this, [=] {
			Core::App().closeWindow(&controller());
		});
	}
}

int32 MainWindow::screenNameChecksum(const QString &name) const {
	constexpr int DeviceNameSize = base::array_size(MONITORINFOEX().szDevice);
	wchar_t buffer[DeviceNameSize] = { 0 };
	if (name.size() < DeviceNameSize) {
		name.toWCharArray(buffer);
	} else {
		memcpy(buffer, name.toStdWString().data(), sizeof(buffer));
	}
	return base::crc32(buffer, sizeof(buffer));
}

void MainWindow::forceIconRefresh() {
	const auto refresher = std::make_unique<QWidget>(this);
	refresher->setWindowFlags(static_cast<Qt::WindowFlags>(Qt::Tool) | Qt::FramelessWindowHint);
	refresher->setGeometry(x() + 1, y() + 1, 1, 1);
	auto palette = refresher->palette();
	palette.setColor(QPalette::Window, (isActiveWindow() ? st::titleBgActive : st::titleBg)->c);
	refresher->setPalette(palette);
	refresher->show();
	refresher->raise();
	refresher->activateWindow();

	updateIconCounters();
}

void MainWindow::workmodeUpdated(Core::Settings::WorkMode mode) {
	using WorkMode = Core::Settings::WorkMode;

	switch (mode) {
	case WorkMode::WindowAndTray: {
		HWND psOwner = (HWND)GetWindowLongPtr(_hWnd, GWLP_HWNDPARENT);
		if (psOwner) {
			SetWindowLongPtr(_hWnd, GWLP_HWNDPARENT, 0);
			windowHandle()->setTransientParent(nullptr);
			forceIconRefresh();
		}
	} break;

	case WorkMode::TrayOnly: {
		HWND psOwner = (HWND)GetWindowLongPtr(_hWnd, GWLP_HWNDPARENT);
		if (!psOwner) {
			const auto hwnd = _taskbarHiderWindow->winId();
			SetWindowLongPtr(_hWnd, GWLP_HWNDPARENT, (LONG_PTR)hwnd);
			windowHandle()->setTransientParent(_taskbarHiderWindow.get());
		}
	} break;

	case WorkMode::WindowOnly: {
		HWND psOwner = (HWND)GetWindowLongPtr(_hWnd, GWLP_HWNDPARENT);
		if (psOwner) {
			SetWindowLongPtr(_hWnd, GWLP_HWNDPARENT, 0);
			windowHandle()->setTransientParent(nullptr);
			forceIconRefresh();
		}
	} break;
	}
}

bool MainWindow::hasTabletView() const {
	if (!_private->viewSettings) {
		return false;
	}
	auto mode = ViewManagement::UserInteractionMode();
	_private->viewSettings->get_UserInteractionMode(&mode);
	return (mode == ViewManagement::UserInteractionMode_Touch);
}

bool MainWindow::initGeometryFromSystem() {
	if (!hasTabletView()) {
		return false;
	}
	if (!screen()) {
		return false;
	}
	Ui::RpWidget::setGeometry(screen()->availableGeometry());
	return true;
}

QRect MainWindow::computeDesktopRect() const {
	const auto flags = MONITOR_DEFAULTTONEAREST;
	if (const auto monitor = MonitorFromWindow(psHwnd(), flags)) {
		MONITORINFOEX info;
		info.cbSize = sizeof(info);
		GetMonitorInfo(monitor, &info);
		return QRect(
			info.rcWork.left,
			info.rcWork.top,
			info.rcWork.right - info.rcWork.left,
			info.rcWork.bottom - info.rcWork.top);
	}
	return Window::MainWindow::computeDesktopRect();
}

void MainWindow::updateWindowIcon() {
	updateIconCounters();
}

bool MainWindow::isActiveForTrayMenu() {
	return !_lastDeactivateTime
		|| (_lastDeactivateTime + kKeepActiveForTrayIcon >= crl::now());
}

void MainWindow::unreadCounterChangedHook() {
	updateIconCounters();
}

void MainWindow::updateIconCounters() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();
	const auto controller = sessionController();
	const auto session = controller ? &controller->session() : nullptr;

	const auto iconSizeSmall = QSize(
		GetSystemMetrics(SM_CXSMICON),
		GetSystemMetrics(SM_CYSMICON));
	const auto iconSizeBig = QSize(
		GetSystemMetrics(SM_CXICON),
		GetSystemMetrics(SM_CYICON));
	const auto supportMode = session && session->supportMode();

	auto iconSmallPixmap16 = Tray::IconWithCounter(
		Tray::CounterLayerArgs(16, counter, muted),
		true,
		supportMode);
	auto iconSmallPixmap32 = Tray::IconWithCounter(
		Tray::CounterLayerArgs(32, counter, muted),
		true,
		supportMode);
	QIcon iconSmall, iconBig;
	iconSmall.addPixmap(iconSmallPixmap16);
	iconSmall.addPixmap(iconSmallPixmap32);
	const auto bigCounter = taskbarList.Get() ? 0 : counter;
	iconBig.addPixmap(Tray::IconWithCounter(
		Tray::CounterLayerArgs(32, bigCounter, muted),
		false,
		supportMode));
	iconBig.addPixmap(Tray::IconWithCounter(
		Tray::CounterLayerArgs(64, bigCounter, muted),
		false,
		supportMode));

	destroyCachedIcons();
	_iconSmall = NativeIcon(iconSmall, iconSizeSmall);
	_iconBig = NativeIcon(iconBig, iconSizeBig);
	SendMessage(_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)_iconSmall);
	SendMessage(_hWnd, WM_SETICON, ICON_BIG, (LPARAM)(_iconBig ? _iconBig : _iconSmall));
	if (taskbarList) {
		if (counter > 0) {
			const auto pixmap = [&](int size) {
				return Ui::PixmapFromImage(Window::GenerateCounterLayer(
					Tray::CounterLayerArgs(size, counter, muted)));
			};
			QIcon iconOverlay;
			iconOverlay.addPixmap(pixmap(16));
			iconOverlay.addPixmap(pixmap(32));
			_iconOverlay = NativeIcon(iconOverlay, iconSizeSmall);
		}
		const auto description = (counter > 0)
			? tr::lng_unread_bar(tr::now, lt_count, counter).toStdWString()
			: std::wstring();
		taskbarList->SetOverlayIcon(_hWnd, _iconOverlay, description.c_str());
	}
	SetWindowPos(_hWnd, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::initHook() {
	_hWnd = reinterpret_cast<HWND>(winId());
	if (!_hWnd) {
		return;
	}

	WTSRegisterSessionNotification(_hWnd, NOTIFY_FOR_THIS_SESSION);

	using namespace base::Platform;
	auto factory = ComPtr<IUIViewSettingsInterop>();
	if (SupportsWRL()) {
		ABI::Windows::Foundation::GetActivationFactory(
			StringReferenceWrapper(
				RuntimeClass_Windows_UI_ViewManagement_UIViewSettings).Get(),
			&factory);
		if (factory) {
			// NB! No such method (or IUIViewSettingsInterop) in C++/WinRT :(
			factory->GetForWindow(
				_hWnd,
				IID_PPV_ARGS(&_private->viewSettings));
		}
	}

	validateWindowTheme(
		Core::App().settings().nativeWindowFrame(),
		Window::Theme::IsNightMode());
}

void MainWindow::validateWindowTheme(bool native, bool night) {
	if (!IsWindows8OrGreater()) {
		const auto empty = native ? nullptr : L" ";
		SetWindowTheme(_hWnd, empty, empty);
		QApplication::setStyle(QStyleFactory::create(u"Windows"_q));
#if 0
	} else if (!Platform::IsDarkModeSupported()/*
		|| (!Dlls::AllowDarkModeForApp && !Dlls::SetPreferredAppMode)
		|| !Dlls::AllowDarkModeForWindow
		|| !Dlls::RefreshImmersiveColorPolicyState
		|| !Dlls::FlushMenuThemes*/) {
		return;
#endif
	} else if (!native) {
		SetWindowTheme(_hWnd, nullptr, nullptr);
		return;
	}

	// See "https://github.com/microsoft/terminal/blob/"
	// "eb480b6bbbd83a2aafbe62992d360838e0ab9da5/"
	// "src/interactivity/win32/windowtheme.cpp#L43-L63"

	auto darkValue = BOOL(night ? TRUE : FALSE);

	const auto updateStyle = [&] {
		static const auto kSystemVersion = QOperatingSystemVersion::current();
		if (kSystemVersion.microVersion() >= 18875 && Dlls::SetWindowCompositionAttribute) {
			Dlls::WINDOWCOMPOSITIONATTRIBDATA data = {
				Dlls::WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS,
				&darkValue,
				sizeof(darkValue)
			};
			Dlls::SetWindowCompositionAttribute(_hWnd, &data);
		} else if (kSystemVersion.microVersion() >= 17763) {
			static const auto kDWMWA_USE_IMMERSIVE_DARK_MODE = (kSystemVersion.microVersion() >= 18985)
				? DWORD(20)
				: DWORD(19);
			DwmSetWindowAttribute(
				_hWnd,
				kDWMWA_USE_IMMERSIVE_DARK_MODE,
				&darkValue,
				sizeof(darkValue));
		}
	};

	updateStyle();

	// See "https://osdn.net/projects/tortoisesvn/scm/svn/blobs/28812/"
	// "trunk/src/TortoiseIDiff/MainWindow.cpp"
	//
	// But for now it works event with a small part of that.
	//

	//const auto updateWindowTheme = [&] {
	//	const auto set = [&](LPCWSTR name) {
	//		return SetWindowTheme(_hWnd, name, nullptr);
	//	};
	//	if (!night || FAILED(set(L"DarkMode_Explorer"))) {
	//		set(L"Explorer");
	//	}
	//};
	//
	//if (night) {
	//	if (Dlls::SetPreferredAppMode) {
	//		Dlls::SetPreferredAppMode(Dlls::PreferredAppMode::AllowDark);
	//	} else {
	//		Dlls::AllowDarkModeForApp(TRUE);
	//	}
	//	Dlls::AllowDarkModeForWindow(_hWnd, TRUE);
	//	updateWindowTheme();
	//	updateStyle();
	//	Dlls::FlushMenuThemes();
	//	Dlls::RefreshImmersiveColorPolicyState();
	//} else {
	//	updateWindowTheme();
	//	Dlls::AllowDarkModeForWindow(_hWnd, FALSE);
	//	updateStyle();
	//	Dlls::FlushMenuThemes();
	//	Dlls::RefreshImmersiveColorPolicyState();
	//	if (Dlls::SetPreferredAppMode) {
	//		Dlls::SetPreferredAppMode(Dlls::PreferredAppMode::Default);
	//	} else {
	//		Dlls::AllowDarkModeForApp(FALSE);
	//	}
	//}

	// Didn't find any other way to definitely repaint with the new style.
	SendMessage(_hWnd, WM_NCACTIVATE, _hasActiveFrame ? 0 : 1, 0);
	SendMessage(_hWnd, WM_NCACTIVATE, _hasActiveFrame ? 1 : 0, 0);
}

HWND MainWindow::psHwnd() const {
	return _hWnd;
}

void MainWindow::destroyCachedIcons() {
	const auto destroy = [](HICON &icon) {
		if (icon) {
			DestroyIcon(icon);
			icon = nullptr;
		}
	};
	destroy(_iconBig);
	destroy(_iconSmall);
	destroy(_iconOverlay);
}

MainWindow::~MainWindow() {
	WTSUnRegisterSessionNotification(_hWnd);
	_private->viewSettings.Reset();
	if (taskbarList) {
		taskbarList.Reset();
	}
	destroyCachedIcons();
}

} // namespace Platform
