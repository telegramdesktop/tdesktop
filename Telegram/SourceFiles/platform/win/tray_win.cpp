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

#include <QtWidgets/QSystemTrayIcon>

namespace Platform {

namespace {

constexpr auto kTooltipDelay = crl::time(10000);

[[nodiscard]] QImage ImageIconWithCounter(
		Window::CounterLayerArgs &&args,
		bool supportMode,
		bool smallIcon) {
	static constexpr auto kCount = 3;
	static auto ScaledLogo = std::array<QImage, kCount>();
	static auto ScaledLogoNoMargin = std::array<QImage, kCount>();

	struct Dimensions {
		int index = 0;
		int size = 0;
	};
	const auto d = [&]() -> Dimensions {
		switch (args.size) {
		case 16:
			return {
				.index = 0,
				.size = 16,
			};
		case 32:
			return {
				.index = 1,
				.size = 32,
			};
		default:
			return {
				.index = 2,
				.size = 64,
			};
		}
	}();
	Assert(d.index < kCount);

	auto &scaled = smallIcon ? ScaledLogoNoMargin : ScaledLogo;
	auto result = [&] {
		auto &image = scaled[d.index];
		if (image.isNull()) {
			image = (smallIcon
				? Window::LogoNoMargin()
				: Window::Logo()).scaledToWidth(
					d.size,
					Qt::SmoothTransformation);
		}
		return image;
	}();
	if (supportMode) {
		Window::ConvertIconToBlack(result);
	}
	if (!args.count) {
		return result;
	} else if (smallIcon) {
		return Window::WithSmallCounter(std::move(result), std::move(args));
	}
	QPainter p(&result);
	const auto half = d.size / 2;
	args.size = half;
	p.drawPixmap(
		half,
		half,
		Ui::PixmapFromImage(Window::GenerateCounterLayer(std::move(args))));
	return result;
}

[[nodiscard]] QWidget *Parent() {
	Expects(Core::App().primaryWindow() != nullptr);
	return Core::App().primaryWindow()->widget();
}

} // namespace

Tray::Tray() {
}

void Tray::createIcon() {
	if (!_icon) {
		_icon = base::make_unique_q<QSystemTrayIcon>(Parent());
		updateIcon();
		_icon->setToolTip(AppName.utf16());
		using Reason = QSystemTrayIcon::ActivationReason;
		base::qt_signal_producer(
			_icon.get(),
			&QSystemTrayIcon::activated
		) | rpl::start_with_next([=](Reason reason) {
			if (reason == QSystemTrayIcon::Context && _menu) {
				_aboutToShowRequests.fire({});
				InvokeQueued(_menu.get(), [=] {
					_menu->popup(QCursor::pos());
				});
			} else {
				_iconClicks.fire({});
			}
		}, _lifetime);
	} else {
		updateIcon();
	}

	_icon->show();
}

void Tray::destroyIcon() {
	_icon = nullptr;
}

void Tray::updateIcon() {
	if (!_icon) {
		return;
	}
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();
	const auto controller = Core::App().primaryWindow();
	const auto session = !controller
		? nullptr
		: !controller->sessionController()
		? nullptr
		: &controller->sessionController()->session();
	const auto supportMode = session && session->supportMode();
	const auto iconSizeWidth = GetSystemMetrics(SM_CXSMICON);

	auto iconSmallPixmap16 = Tray::IconWithCounter(
		CounterLayerArgs(16, counter, muted),
		true,
		supportMode);
	auto iconSmallPixmap32 = Tray::IconWithCounter(
		CounterLayerArgs(32, counter, muted),
		true,
		supportMode);
	auto iconSmall = QIcon();
	iconSmall.addPixmap(iconSmallPixmap16);
	iconSmall.addPixmap(iconSmallPixmap32);
	// Force Qt to use right icon size, not the larger one.
	QIcon forTrayIcon;
	forTrayIcon.addPixmap(iconSizeWidth >= 20
		? iconSmallPixmap32
		: iconSmallPixmap16);
	_icon->setIcon(forTrayIcon);
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
			QSystemTrayIcon::Information,
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
		bool supportMode) {
	return Ui::PixmapFromImage(
		ImageIconWithCounter(std::move(args), supportMode, smallIcon));
}

} // namespace Platform
