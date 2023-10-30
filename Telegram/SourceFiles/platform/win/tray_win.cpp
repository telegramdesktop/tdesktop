/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/tray_win.h"

#include "base/invoke_queued.h"
#include "base/qt_signal_producer.h"
#include "core/application.h"
#include "main/main_session.h"
#include "storage/localstorage.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_window.h"

#include <qpa/qplatformscreen.h>
#include <qpa/qplatformsystemtrayicon.h>
#include <qpa/qplatformtheme.h>
#include <private/qguiapplication_p.h>
#include <private/qhighdpiscaling_p.h>

namespace Platform {

namespace {

constexpr auto kTooltipDelay = crl::time(10000);

[[nodiscard]] std::optional<bool> IsDarkTaskbar() {
	static const auto kSystemVersion = QOperatingSystemVersion::current();
	static const auto kDarkModeAddedVersion = QOperatingSystemVersion(
		QOperatingSystemVersion::Windows,
		10,
		0,
		17763);
	static const auto kSupported = (kSystemVersion >= kDarkModeAddedVersion);
	if (!kSupported) {
		return std::nullopt;
	}

	const auto keyName = L""
		"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
	const auto valueName = L"SystemUsesLightTheme";
	auto key = HKEY();
	auto result = RegOpenKeyEx(HKEY_CURRENT_USER, keyName, 0, KEY_READ, &key);
	if (result != ERROR_SUCCESS) {
		return std::nullopt;
	}

	DWORD value = 0, type = 0, size = sizeof(value);
	result = RegQueryValueEx(key, valueName, 0, &type, (LPBYTE)&value, &size);
	RegCloseKey(key);
	if (result != ERROR_SUCCESS) {
		return std::nullopt;
	}

	return (value == 0);
}

[[nodiscard]] QImage ImageIconWithCounter(
		Window::CounterLayerArgs &&args,
		bool supportMode,
		bool smallIcon,
		bool monochrome) {
	static auto ScaledLogo = base::flat_map<int, QImage>();
	static auto ScaledLogoNoMargin = base::flat_map<int, QImage>();
	static auto ScaledLogoDark = base::flat_map<int, QImage>();
	static auto ScaledLogoLight = base::flat_map<int, QImage>();

	const auto darkMode = IsDarkTaskbar();
	auto &scaled = (monochrome && darkMode)
		? (*darkMode
			? ScaledLogoDark
			: ScaledLogoLight)
		: smallIcon
		? ScaledLogoNoMargin
		: ScaledLogo;

	auto result = [&] {
		if (const auto it = scaled.find(args.size); it != scaled.end()) {
			return it->second;
		} else {
			if (monochrome && darkMode) {
				const auto withColor = [&](QColor color) -> std::pair<QImage, int> {
					if (args.size <= 16) {
						return { st::macTrayIcon.instance(color, 100 / cIntRetinaFactor()), 3 };
					} else if (args.size <= 32) {
						return { st::macTrayIcon.instance(color, 200 / cIntRetinaFactor()), 6 };
					} else {
						return { st::macTrayIcon.instance(color, 300 / cIntRetinaFactor()), 9 };
					}
				};
				const auto result = *darkMode
					? withColor({ 255, 255, 255 })
					: withColor({ 0, 0, 0, 228 });
				auto &image = scaled.emplace(
					args.size,
					result.first.copy(
						QRect(
							QPoint(result.second, result.second),
							result.first.size()
								- QSize(result.second * 2, result.second * 2)
						)
					).scaledToWidth(args.size, Qt::SmoothTransformation)
				).first->second;
				image.setDevicePixelRatio(1);
				return image;
			} else {
				return scaled.emplace(
					args.size,
					(smallIcon
						? Window::LogoNoMargin()
						: Window::Logo()
					).scaledToWidth(args.size, Qt::SmoothTransformation)
				).first->second;
			}
		}
	}();
	if ((!monochrome || !darkMode) && supportMode) {
		Window::ConvertIconToBlack(result);
	}
	if (!args.count) {
		return result;
	} else if (smallIcon) {
		return Window::WithSmallCounter(std::move(result), std::move(args));
	}
	QPainter p(&result);
	const auto half = args.size / 2;
	args.size = half;
	p.drawPixmap(
		half,
		half,
		Ui::PixmapFromImage(Window::GenerateCounterLayer(std::move(args))));
	return result;
}

} // namespace

Tray::Tray() {
}

void Tray::createIcon() {
	if (!_icon) {
		if (const auto theme = QGuiApplicationPrivate::platformTheme()) {
			_icon.reset(theme->createPlatformSystemTrayIcon());
		}
		if (!_icon) {
			return;
		}
		_icon->init();
		updateIcon();
		_icon->updateToolTip(AppName.utf16());

		using Reason = QPlatformSystemTrayIcon::ActivationReason;
		base::qt_signal_producer(
			_icon.get(),
			&QPlatformSystemTrayIcon::activated
		) | rpl::filter(
			rpl::mappers::_1 != Reason::Context
		) | rpl::map_to(
			rpl::empty
		) | rpl::start_to_stream(_iconClicks, _lifetime);

		base::qt_signal_producer(
			_icon.get(),
			&QPlatformSystemTrayIcon::contextMenuRequested
		) | rpl::filter([=] {
			return _menu != nullptr;
		}) | rpl::start_with_next([=](
				QPoint globalNativePosition,
				const QPlatformScreen *screen) {
			_aboutToShowRequests.fire({});
			const auto position = QHighDpi::fromNativePixels(
				globalNativePosition,
				screen ? screen->screen() : nullptr);
			InvokeQueued(_menu.get(), [=] {
				_menu->popup(position);
			});
		}, _lifetime);
	} else {
		updateIcon();
	}
}

void Tray::destroyIcon() {
	_icon = nullptr;
}

void Tray::updateIcon() {
	if (!_icon) {
		return;
	}
	const auto controller = Core::App().activePrimaryWindow();
	const auto session = !controller
		? nullptr
		: !controller->sessionController()
		? nullptr
		: &controller->sessionController()->session();

	// Force Qt to use right icon size, not the larger one.
	QIcon forTrayIcon;
	forTrayIcon.addPixmap(
		Tray::IconWithCounter(
			CounterLayerArgs(
				GetSystemMetrics(SM_CXSMICON),
				Core::App().unreadBadge(),
				Core::App().unreadBadgeMuted()),
			true,
			Core::App().settings().trayIconMonochrome(),
			session && session->supportMode()));
	_icon->updateIcon(forTrayIcon);
}

void Tray::createMenu() {
	if (!_menu) {
		_menu = base::make_unique_q<Ui::PopupMenu>(nullptr);
		_menu->deleteOnHide(false);
	}
}

void Tray::destroyMenu() {
	_menu = nullptr;
	_actionsLifetime.destroy();
}

void Tray::addAction(rpl::producer<QString> text, Fn<void()> &&callback) {
	if (!_menu) {
		return;
	}

	// If we try to activate() window before the _menu is hidden,
	// then the window will be shown in semi-active state (Qt bug).
	// It will receive input events, but it will be rendered as inactive.
	auto callbackLater = crl::guard(_menu.get(), [=] {
		using namespace rpl::mappers;
		_callbackFromTrayLifetime = _menu->shownValue(
		) | rpl::filter(!_1) | rpl::take(1) | rpl::start_with_next([=] {
			callback();
		});
	});

	const auto action = _menu->addAction(QString(), std::move(callbackLater));
	std::move(
		text
	) | rpl::start_with_next([=](const QString &text) {
		action->setText(text);
	}, _actionsLifetime);
}

void Tray::showTrayMessage() const {
	if (!cSeenTrayTooltip() && _icon) {
		_icon->showMessage(
			AppName.utf16(),
			tr::lng_tray_icon_text(tr::now),
			QIcon(),
			QPlatformSystemTrayIcon::Information,
			kTooltipDelay);
		cSetSeenTrayTooltip(true);
		Local::writeSettings();
	}
}

bool Tray::hasTrayMessageSupport() const {
	return !cSeenTrayTooltip();
}

rpl::producer<> Tray::aboutToShowRequests() const {
	return _aboutToShowRequests.events();
}

rpl::producer<> Tray::showFromTrayRequests() const {
	return rpl::never<>();
}

rpl::producer<> Tray::hideToTrayRequests() const {
	return rpl::never<>();
}

rpl::producer<> Tray::iconClicks() const {
	return _iconClicks.events();
}

bool Tray::hasIcon() const {
	return _icon;
}

rpl::lifetime &Tray::lifetime() {
	return _lifetime;
}

Window::CounterLayerArgs Tray::CounterLayerArgs(
		int size,
		int counter,
		bool muted) {
	return Window::CounterLayerArgs{
		.size = size,
		.count = counter,
		.bg = muted ? st::trayCounterBgMute : st::trayCounterBg,
		.fg = st::trayCounterFg,
	};
}

QPixmap Tray::IconWithCounter(
		Window::CounterLayerArgs &&args,
		bool smallIcon,
		bool monochrome,
		bool supportMode) {
	return Ui::PixmapFromImage(ImageIconWithCounter(
		std::move(args),
		supportMode,
		smallIcon,
		monochrome));
}

bool HasMonochromeSetting() {
	return IsDarkTaskbar().has_value();
}

} // namespace Platform
