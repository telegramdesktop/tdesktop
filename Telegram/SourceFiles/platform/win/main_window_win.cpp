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
#include "platform/win/integration_win.h"
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
#include "window/window_controller.h"
#include "history/history.h"

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtCore/QOperatingSystemVersion>

#include <Shobjidl.h>
#include <shellapi.h>
#include <WtsApi32.h>
#include <dwmapi.h>

#include <windows.ui.viewmanagement.h>
#include <UIViewSettingsInterop.h>

#include <Windowsx.h>
#include <VersionHelpers.h>

// Taken from qtbase/src/gui/image/qpixmap_win.cpp
HICON qt_pixmapToWinHICON(const QPixmap &);
HBITMAP qt_imageToWinHBITMAP(const QImage &, int hbitmapFormat);

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

// Taken from qtbase/src/gui/image/qpixmap_win.cpp
enum HBitmapFormat {
	HBitmapNoAlpha,
	HBitmapPremultipliedAlpha,
	HBitmapAlpha
};

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


[[nodiscard]] HICON NativeIcon(const QIcon &icon, QSize size) {
	if (!icon.isNull()) {
		const auto pixmap = icon.pixmap(icon.actualSize(size));
		if (!pixmap.isNull()) {
			return qt_pixmapToWinHICON(pixmap);
		}
	}
	return nullptr;
}

struct RealSize {
	QSize value;
	bool maximized = false;
};
[[nodiscard]] RealSize DetectRealSize(HWND hwnd) {
	auto result = RECT();
	auto placement = WINDOWPLACEMENT();
	if (!GetWindowPlacement(hwnd, &placement)) {
		return {};
	} else if (placement.flags & WPF_RESTORETOMAXIMIZED) {
		const auto monitor = MonitorFromRect(
			&placement.rcNormalPosition,
			MONITOR_DEFAULTTONULL);
		if (!monitor) {
			return {};
		}
		auto info = MONITORINFO{ .cbSize = sizeof(MONITORINFO) };
		if (!GetMonitorInfo(monitor, &info)) {
			return {};
		}
		result = info.rcWork;
	} else {
		CopyRect(&result, &placement.rcNormalPosition);
	}
	return {
		{ int(result.right - result.left), int(result.bottom - result.top) },
		((placement.flags & WPF_RESTORETOMAXIMIZED) != 0)
	};
}

[[nodiscard]] QImage PrepareLogoPreview(
		QSize size,
		QImage::Format format,
		int radius = 0) {
	auto result = QImage(size, QImage::Format_RGB32);
	result.fill(st::windowBg->c);

	const auto logo = Window::Logo();
	const auto width = size.width();
	const auto height = size.height();
	const auto side = logo.width();
	const auto skip = width / 8;
	const auto use = std::min({ width - skip, height - skip, side });
	auto p = QPainter(&result);
	if (use == side) {
		p.drawImage((width - side) / 2, (height - side) / 2, logo);
	} else {
		const auto scaled = logo.scaled(
			use,
			use,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
		p.drawImage((width - use) / 2, (height - use) / 2, scaled);
	}
	p.end();

	return radius
		? Images::Round(std::move(result), Images::CornersMask(radius))
		: result;
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

	case WM_DWMSENDICONICTHUMBNAIL: {
		if (!Core::App().passcodeLocked()) {
			return false;
		}
		const auto size = QSize(int(HIWORD(lParam)), int(LOWORD(lParam)));
		return _window->setDwmThumbnail(size);
	}

	case WM_DWMSENDICONICLIVEPREVIEWBITMAP: {
		if (!Core::App().passcodeLocked()) {
			return false;
		}
		const auto size = DetectRealSize(hWnd);
		const auto radius = size.maximized ? 0 : style::ConvertScale(8);
		return _window->setDwmPreview(size.value, radius);
	}

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

MainWindow::BitmapPointer::BitmapPointer(HBITMAP value) : _value(value) {
}

MainWindow::BitmapPointer::BitmapPointer(BitmapPointer &&other)
: _value(base::take(other._value)) {
}

MainWindow::BitmapPointer &MainWindow::BitmapPointer::operator=(
		BitmapPointer &&other) {
	if (_value != other._value) {
		reset();
		_value = base::take(other._value);
	}
	return *this;
}

MainWindow::BitmapPointer::~BitmapPointer() {
	reset();
}

HBITMAP MainWindow::BitmapPointer::get() const {
	return _value;
}

MainWindow::BitmapPointer::operator bool() const {
	return _value != nullptr;
}

void MainWindow::BitmapPointer::release() {
	_value = nullptr;
}

void MainWindow::BitmapPointer::reset(HBITMAP value) {
	if (_value != value) {
		if (const auto old = std::exchange(_value, value)) {
			DeleteObject(old);
		}
	}
}

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller)
, _private(std::make_unique<Private>(this))
, _taskbarHiderWindow(std::make_unique<QWindow>()) {
	qApp->installNativeEventFilter(&_private->filter);

	setupNativeWindowFrame();

	SetWindowPriority(this, controller->isPrimary() ? 2 : 1);

	using namespace rpl::mappers;
	Core::App().appDeactivatedValue(
	) | rpl::distinct_until_changed(
	) | rpl::filter(_1) | rpl::start_with_next([=] {
		_lastDeactivateTime = crl::now();
	}, lifetime());

	setupPreviewPasscodeLock();
}

void MainWindow::setupPreviewPasscodeLock() {
	Core::App().passcodeLockValue(
	) | rpl::start_with_next([=](bool locked) {
		// Use iconic bitmap instead of the window content if passcoded.
		BOOL fForceIconic = locked ? TRUE : FALSE;
		BOOL fHasIconicBitmap = fForceIconic;
		DwmSetWindowAttribute(
			_hWnd,
			DWMWA_FORCE_ICONIC_REPRESENTATION,
			&fForceIconic,
			sizeof(fForceIconic));
		DwmSetWindowAttribute(
			_hWnd,
			DWMWA_HAS_ICONIC_BITMAP,
			&fHasIconicBitmap,
			sizeof(fHasIconicBitmap));
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
	if (!Core::App().closeNonLastAsync(&controller())) {
		Core::Quit();
	}
}

bool MainWindow::setDwmThumbnail(QSize size) {
	validateDwmPreviewColors();
	if (size.isEmpty()) {
		return false;
	} else if (!_dwmThumbnail || _dwmThumbnailSize != size) {
		const auto result = PrepareLogoPreview(size, QImage::Format_RGB32);
		const auto bitmap = qt_imageToWinHBITMAP(result, HBitmapNoAlpha);
		if (!bitmap) {
			return false;
		}
		_dwmThumbnail.reset(bitmap);
		_dwmThumbnailSize = size;
	}
	DwmSetIconicThumbnail(_hWnd, _dwmThumbnail.get(), NULL);
	return true;
}

bool MainWindow::setDwmPreview(QSize size, int radius) {
	Expects(radius >= 0);

	validateDwmPreviewColors();
	if (size.isEmpty()) {
		return false;
	} else if (!_dwmPreview
		|| _dwmPreviewSize != size
		|| _dwmPreviewRadius != radius) {
		const auto format = (radius > 0)
			? QImage::Format_ARGB32_Premultiplied
			: QImage::Format_RGB32;
		const auto result = PrepareLogoPreview(size, format, radius);
		const auto bitmap = qt_imageToWinHBITMAP(
			result,
			(radius > 0) ? HBitmapPremultipliedAlpha : HBitmapNoAlpha);
		if (!bitmap) {
			return false;
		}
		_dwmPreview.reset(bitmap);
		_dwmPreviewRadius = radius;
		_dwmPreviewSize = size;
	}
	const auto flags = 0;
	DwmSetIconicLivePreviewBitmap(_hWnd, _dwmPreview.get(), NULL, flags);
	return true;
}

void MainWindow::validateDwmPreviewColors() {
	if (_dwmBackground == st::windowBg->c) {
		return;
	}
	_dwmBackground = st::windowBg->c;
	_dwmThumbnail.reset();
	_dwmPreview.reset();
}

void MainWindow::forceIconRefresh() {
	const auto refresher = std::make_unique<QWidget>(this);
	refresher->setWindowFlags(
		static_cast<Qt::WindowFlags>(Qt::Tool) | Qt::FramelessWindowHint);
	refresher->setGeometry(x() + 1, y() + 1, 1, 1);
	auto palette = refresher->palette();
	palette.setColor(
		QPalette::Window,
		(isActiveWindow() ? st::titleBgActive : st::titleBg)->c);
	refresher->setPalette(palette);
	refresher->show();
	refresher->raise();
	refresher->activateWindow();

	updateTaskbarAndIconCounters();
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
	if (!hasTabletView() || !screen()) {
		return false;
	}
	Ui::RpWidget::setGeometry(screen()->availableGeometry());
	return true;
}

bool MainWindow::nativeEvent(
		const QByteArray &eventType,
		void *message,
		long *result) {
	if (message) {
		const auto msg = static_cast<MSG*>(message);
		if (msg->message == WM_IME_STARTCOMPOSITION) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				imeCompositionStartReceived();
			});
		}
	}
	return false;
}

void MainWindow::updateWindowIcon() {
	updateTaskbarAndIconCounters();
}

bool MainWindow::isActiveForTrayMenu() {
	return !_lastDeactivateTime
		|| (_lastDeactivateTime + kKeepActiveForTrayIcon >= crl::now());
}

void MainWindow::unreadCounterChangedHook() {
	updateTaskbarAndIconCounters();
}

void MainWindow::updateTaskbarAndIconCounters() {
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
		false,
		supportMode);
	auto iconSmallPixmap32 = Tray::IconWithCounter(
		Tray::CounterLayerArgs(32, counter, muted),
		true,
		false,
		supportMode);
	QIcon iconSmall, iconBig;
	iconSmall.addPixmap(iconSmallPixmap16);
	iconSmall.addPixmap(iconSmallPixmap32);
	const auto integration = &Platform::WindowsIntegration::Instance();
	const auto taskbarList = integration->taskbarList();
	const auto bigCounter = taskbarList ? 0 : counter;
	iconBig.addPixmap(Tray::IconWithCounter(
		Tray::CounterLayerArgs(32, bigCounter, muted),
		false,
		false,
		supportMode));
	iconBig.addPixmap(Tray::IconWithCounter(
		Tray::CounterLayerArgs(64, bigCounter, muted),
		false,
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
	} else if (!Core::App().settings().systemDarkMode().has_value()/*
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
	destroyCachedIcons();
}

int32 ScreenNameChecksum(const QString &name) {
	constexpr int DeviceNameSize = base::array_size(MONITORINFOEX().szDevice);
	wchar_t buffer[DeviceNameSize] = { 0 };
	if (name.size() < DeviceNameSize) {
		name.toWCharArray(buffer);
	} else {
		memcpy(buffer, name.toStdWString().data(), sizeof(buffer));
	}
	return base::crc32(buffer, sizeof(buffer));
}

} // namespace Platform
