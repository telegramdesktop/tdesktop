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
#include "platform/win/windows_dlls.h"
#include "platform/win/windows_event_filter.h"
#include "window/notifications_manager.h"
#include "mainwindow.h"
#include "base/crc32hash.h"
#include "base/platform/win/base_windows_wrl.h"
#include "core/application.h"
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
#include <qpa/qplatformnativeinterface.h>

#include <Shobjidl.h>
#include <shellapi.h>
#include <WtsApi32.h>

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

using namespace Microsoft::WRL;

HICON createHIconFromQIcon(const QIcon &icon, int xSize, int ySize) {
	if (!icon.isNull()) {
		const QPixmap pm = icon.pixmap(icon.actualSize(QSize(xSize, ySize)));
		if (!pm.isNull()) {
			return qt_pixmapToWinHICON(pm);
		}
	}
	return nullptr;
}

HWND createTaskbarHider() {
	HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);
	HWND hWnd = 0;

	QString cn = QString("TelegramTaskbarHider");
	LPCWSTR _cn = (LPCWSTR)cn.utf16();
	WNDCLASSEX wc;

	wc.cbSize = sizeof(wc);
	wc.style = 0;
	wc.lpfnWndProc = DefWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = appinst;
	wc.hIcon = 0;
	wc.hCursor = 0;
	wc.hbrBackground = 0;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = _cn;
	wc.hIconSm = 0;
	if (!RegisterClassEx(&wc)) {
		DEBUG_LOG(("Application Error: could not register taskbar hider window class, error: %1").arg(GetLastError()));
		return hWnd;
	}

	hWnd = CreateWindowEx(WS_EX_TOOLWINDOW, _cn, 0, WS_POPUP, 0, 0, 0, 0, 0, 0, appinst, 0);
	if (!hWnd) {
		DEBUG_LOG(("Application Error: could not create taskbar hider window class, error: %1").arg(GetLastError()));
		return hWnd;
	}
	return hWnd;
}

ComPtr<ITaskbarList3> taskbarList;
bool handleSessionNotification = false;
uint32 kTaskbarCreatedMsgId = 0;

} // namespace

struct MainWindow::Private {
	ComPtr<ViewManagement::IUIViewSettings> viewSettings;
};

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller)
, _private(std::make_unique<Private>())
, ps_tbHider_hWnd(createTaskbarHider()) {
	QCoreApplication::instance()->installNativeEventFilter(
		EventFilter::CreateInstance(this));

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

	using BackgroundUpdate = Window::Theme::BackgroundUpdate;
	auto themeChanges = Window::Theme::Background()->updates(
	) | rpl::filter([=](const BackgroundUpdate &update) {
		return update.type == BackgroundUpdate::Type::ApplyingTheme;
	}) | rpl::to_empty;

	auto nightMode = rpl::single(
		rpl::empty_value()
	) | rpl::then(
		std::move(themeChanges)
	) | rpl::map([=] {
		return Window::Theme::IsNightMode();
	}) | rpl::distinct_until_changed();

	rpl::combine(
		std::move(nativeFrame),
		std::move(nightMode)
	) | rpl::skip(1) | rpl::start_with_next([=](bool native, bool night) {
		validateWindowTheme(native, night);
	}, lifetime());
}

uint32 MainWindow::TaskbarCreatedMsgId() {
	return kTaskbarCreatedMsgId;
}

void MainWindow::TaskbarCreated() {
	HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&taskbarList));
	if (!SUCCEEDED(hr)) {
		taskbarList.Reset();
	}
}

void MainWindow::shadowsActivate() {
	_hasActiveFrame = true;
}

void MainWindow::shadowsDeactivate() {
	_hasActiveFrame = false;
}

void MainWindow::psShowTrayMenu() {
	trayIconMenu->popup(QCursor::pos());
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

void MainWindow::psRefreshTaskbarIcon() {
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

void MainWindow::psTrayMenuUpdated() {
}

void MainWindow::psSetupTrayIcon() {
	if (!trayIcon) {
		trayIcon = new QSystemTrayIcon(this);

		auto icon = QIcon(Ui::PixmapFromImage(Core::App().logoNoMargin()));

		trayIcon->setIcon(icon);
		connect(
			trayIcon,
			&QSystemTrayIcon::messageClicked,
			this,
			[=] { showFromTray(); });
		attachToTrayIcon(trayIcon);
	}
	updateIconCounters();

	trayIcon->show();
}

void MainWindow::showTrayTooltip() {
	if (trayIcon && !cSeenTrayTooltip()) {
		trayIcon->showMessage(
			AppName.utf16(),
			tr::lng_tray_icon_text(tr::now),
			QSystemTrayIcon::Information,
			10000);
		cSetSeenTrayTooltip(true);
		Local::writeSettings();
	}
}

void MainWindow::workmodeUpdated(Core::Settings::WorkMode mode) {
	using WorkMode = Core::Settings::WorkMode;

	switch (mode) {
	case WorkMode::WindowAndTray: {
		psSetupTrayIcon();
		HWND psOwner = (HWND)GetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT);
		if (psOwner) {
			SetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT, 0);
			psRefreshTaskbarIcon();
		}
	} break;

	case WorkMode::TrayOnly: {
		psSetupTrayIcon();
		HWND psOwner = (HWND)GetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT);
		if (!psOwner) {
			SetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT, (LONG_PTR)ps_tbHider_hWnd);
		}
	} break;

	case WorkMode::WindowOnly: {
		if (trayIcon) {
			trayIcon->setContextMenu(0);
			trayIcon->deleteLater();
		}
		trayIcon = 0;

		HWND psOwner = (HWND)GetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT);
		if (psOwner) {
			SetWindowLongPtr(ps_hWnd, GWLP_HWNDPARENT, 0);
			psRefreshTaskbarIcon();
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

bool MainWindow::initSizeFromSystem() {
	if (!hasTabletView()) {
		return false;
	}
	const auto screen = [&] {
		if (const auto result = windowHandle()->screen()) {
			return result;
		}
		return QGuiApplication::primaryScreen();
	}();
	if (!screen) {
		return false;
	}
	Ui::RpWidget::setGeometry(screen->availableGeometry());
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

	auto iconSizeSmall = QSize(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
	auto iconSizeBig = QSize(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));

	auto &bg = (muted ? st::trayCounterBgMute : st::trayCounterBg);
	auto &fg = st::trayCounterFg;
	auto iconSmallPixmap16 = Ui::PixmapFromImage(
		iconWithCounter(16, counter, bg, fg, true));
	auto iconSmallPixmap32 = Ui::PixmapFromImage(
		iconWithCounter(32, counter, bg, fg, true));
	QIcon iconSmall, iconBig;
	iconSmall.addPixmap(iconSmallPixmap16);
	iconSmall.addPixmap(iconSmallPixmap32);
	iconBig.addPixmap(Ui::PixmapFromImage(
		iconWithCounter(32, taskbarList.Get() ? 0 : counter, bg, fg, false)));
	iconBig.addPixmap(Ui::PixmapFromImage(
		iconWithCounter(64, taskbarList.Get() ? 0 : counter, bg, fg, false)));
	if (trayIcon) {
		// Force Qt to use right icon size, not the larger one.
		QIcon forTrayIcon;
		forTrayIcon.addPixmap(iconSizeSmall.width() >= 20 ? iconSmallPixmap32 : iconSmallPixmap16);
		trayIcon->setIcon(forTrayIcon);
	}

	psDestroyIcons();
	ps_iconSmall = createHIconFromQIcon(iconSmall, iconSizeSmall.width(), iconSizeSmall.height());
	ps_iconBig = createHIconFromQIcon(iconBig, iconSizeBig.width(), iconSizeBig.height());
	SendMessage(ps_hWnd, WM_SETICON, 0, (LPARAM)ps_iconSmall);
	SendMessage(ps_hWnd, WM_SETICON, 1, (LPARAM)(ps_iconBig ? ps_iconBig : ps_iconSmall));
	if (taskbarList.Get()) {
		if (counter > 0) {
			QIcon iconOverlay;
			iconOverlay.addPixmap(Ui::PixmapFromImage(
				iconWithCounter(-16, counter, bg, fg, false)));
			iconOverlay.addPixmap(Ui::PixmapFromImage(
				iconWithCounter(-32, counter, bg, fg, false)));
			ps_iconOverlay = createHIconFromQIcon(iconOverlay, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
		}
		auto description = (counter > 0) ? tr::lng_unread_bar(tr::now, lt_count, counter) : QString();
		taskbarList->SetOverlayIcon(ps_hWnd, ps_iconOverlay, description.toStdWString().c_str());
	}
	SetWindowPos(ps_hWnd, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::initHook() {
	if (const auto native = QGuiApplication::platformNativeInterface()) {
		ps_hWnd = static_cast<HWND>(native->nativeResourceForWindow(
			QByteArrayLiteral("handle"),
			windowHandle()));
	}
	if (!ps_hWnd) {
		return;
	}

	handleSessionNotification = (Dlls::WTSRegisterSessionNotification != nullptr)
		&& (Dlls::WTSUnRegisterSessionNotification != nullptr);
	if (handleSessionNotification) {
		Dlls::WTSRegisterSessionNotification(ps_hWnd, NOTIFY_FOR_THIS_SESSION);
	}

	using namespace base::Platform;
	auto factory = ComPtr<IUIViewSettingsInterop>();
	if (SupportsWRL()) {
		GetActivationFactory(
			StringReferenceWrapper(
				RuntimeClass_Windows_UI_ViewManagement_UIViewSettings).Get(),
			&factory);
		if (factory) {
			factory->GetForWindow(
				ps_hWnd,
				IID_PPV_ARGS(&_private->viewSettings));
		}
	}

	validateWindowTheme(
		Core::App().settings().nativeWindowFrame(),
		Window::Theme::IsNightMode());
}

void MainWindow::validateWindowTheme(bool native, bool night) {
	if (!Dlls::SetWindowTheme) {
		return;
	} else if (!IsWindows8OrGreater()) {
		const auto empty = native ? nullptr : L" ";
		Dlls::SetWindowTheme(ps_hWnd, empty, empty);
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
		Dlls::SetWindowTheme(ps_hWnd, nullptr, nullptr);
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
			Dlls::SetWindowCompositionAttribute(ps_hWnd, &data);
		} else if (kSystemVersion.microVersion() >= 17763 && Dlls::DwmSetWindowAttribute) {
			static const auto DWMWA_USE_IMMERSIVE_DARK_MODE = (kSystemVersion.microVersion() >= 18985)
				? DWORD(20)
				: DWORD(19);
			Dlls::DwmSetWindowAttribute(
				ps_hWnd,
				DWMWA_USE_IMMERSIVE_DARK_MODE,
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
	//		return Dlls::SetWindowTheme(ps_hWnd, name, nullptr);
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
	//	Dlls::AllowDarkModeForWindow(ps_hWnd, TRUE);
	//	updateWindowTheme();
	//	updateStyle();
	//	Dlls::FlushMenuThemes();
	//	Dlls::RefreshImmersiveColorPolicyState();
	//} else {
	//	updateWindowTheme();
	//	Dlls::AllowDarkModeForWindow(ps_hWnd, FALSE);
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
	SendMessage(ps_hWnd, WM_NCACTIVATE, _hasActiveFrame ? 0 : 1, 0);
	SendMessage(ps_hWnd, WM_NCACTIVATE, _hasActiveFrame ? 1 : 0, 0);
}

void MainWindow::showFromTrayMenu() {
	// If we try to activate() window before the trayIconMenu is hidden,
	// then the window will be shown in semi-active state (Qt bug).
	// It will receive input events, but it will be rendered as inactive.
	using namespace rpl::mappers;
	_showFromTrayLifetime = trayIconMenu->shownValue(
	) | rpl::filter(_1) | rpl::take(1) | rpl::start_with_next([=] {
		showFromTray();
	});
}

HWND MainWindow::psHwnd() const {
	return ps_hWnd;
}

void MainWindow::psDestroyIcons() {
	if (ps_iconBig) {
		DestroyIcon(ps_iconBig);
		ps_iconBig = 0;
	}
	if (ps_iconSmall) {
		DestroyIcon(ps_iconSmall);
		ps_iconSmall = 0;
	}
	if (ps_iconOverlay) {
		DestroyIcon(ps_iconOverlay);
		ps_iconOverlay = 0;
	}
}

MainWindow::~MainWindow() {
	if (handleSessionNotification) {
		Dlls::WTSUnRegisterSessionNotification(ps_hWnd);
	}
	_private->viewSettings.Reset();
	if (taskbarList) {
		taskbarList.Reset();
	}

	psDestroyIcons();
	if (ps_tbHider_hWnd) DestroyWindow(ps_tbHider_hWnd);

	EventFilter::Destroy();
}

} // namespace Platform
