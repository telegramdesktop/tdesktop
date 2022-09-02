/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tray.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "platform/platform_specific.h"

#include <QtWidgets/QApplication>

namespace Core {

Tray::Tray() {
}

void Tray::create() {
	rebuildMenu();
	using WorkMode = Settings::WorkMode;
	if (Core::App().settings().workMode() != WorkMode::WindowOnly) {
		_tray.createIcon();
	}

	Core::App().settings().workModeValue(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](WorkMode previous, WorkMode state) {
		const auto wasHasIcon = (previous != WorkMode::WindowOnly);
		const auto nowHasIcon = (state != WorkMode::WindowOnly);
		if (wasHasIcon != nowHasIcon) {
			if (nowHasIcon) {
				_tray.createIcon();
			} else {
				_tray.destroyIcon();
			}
		}
	}, _tray.lifetime());

	Core::App().passcodeLockChanges(
	) | rpl::start_with_next([=] {
		rebuildMenu();
	}, _tray.lifetime());

	_tray.iconClicks(
	) | rpl::start_with_next([=] {
		const auto skipTrayClick = (_lastTrayClickTime > 0)
			&& (crl::now() - _lastTrayClickTime
				< QApplication::doubleClickInterval());
		if (!skipTrayClick) {
			_activeForTrayIconAction = Core::App().isActiveForTrayMenu();
			_minimizeMenuItemClicks.fire({});
			_lastTrayClickTime = crl::now();
		}
	}, _tray.lifetime());
}

void Tray::rebuildMenu() {
	_tray.destroyMenu();
	_tray.createMenu();

	{
		auto minimizeText = _textUpdates.events(
		) | rpl::map([=] {
			_activeForTrayIconAction = Core::App().isActiveForTrayMenu();
			return _activeForTrayIconAction
				? tr::lng_minimize_to_tray(tr::now)
				: tr::lng_open_from_tray(tr::now);
		});

		_tray.addAction(
			std::move(minimizeText),
			[=] { _minimizeMenuItemClicks.fire({}); });
	}

	if (!Core::App().passcodeLocked()) {
		auto notificationsText = _textUpdates.events(
		) | rpl::map([=] {
			return Core::App().settings().desktopNotify()
				? tr::lng_disable_notifications_from_tray(tr::now)
				: tr::lng_enable_notifications_from_tray(tr::now);
		});

		_tray.addAction(
			std::move(notificationsText),
			[=] { toggleSoundNotifications(); });
	}

	_tray.addAction(tr::lng_quit_from_tray(), [] { Core::Quit(); });

	updateMenuText();
}

void Tray::updateMenuText() {
	_textUpdates.fire({});
}

void Tray::updateIconCounters() {
	_tray.updateIcon();
}

rpl::producer<> Tray::aboutToShowRequests() const {
	return _tray.aboutToShowRequests();
}

rpl::producer<> Tray::showFromTrayRequests() const {
	return rpl::merge(
		_tray.showFromTrayRequests(),
		_minimizeMenuItemClicks.events() | rpl::filter([=] {
			return !_activeForTrayIconAction;
		})
	);
}

rpl::producer<> Tray::hideToTrayRequests() const {
	auto triggers = rpl::merge(
		_tray.hideToTrayRequests(),
		_minimizeMenuItemClicks.events() | rpl::filter([=] {
			return _activeForTrayIconAction;
		})
	);
	if (_tray.hasTrayMessageSupport()) {
		return std::move(triggers) | rpl::map([=]() -> rpl::empty_value {
			_tray.showTrayMessage();
			return {};
		});
	} else {
		return triggers;
	}
}

void Tray::toggleSoundNotifications() {
	auto soundNotifyChanged = false;
	auto flashBounceNotifyChanged = false;
	auto &settings = Core::App().settings();
	settings.setDesktopNotify(!settings.desktopNotify());
	if (settings.desktopNotify()) {
		if (settings.rememberedSoundNotifyFromTray()
			&& !settings.soundNotify()) {
			settings.setSoundNotify(true);
			settings.setRememberedSoundNotifyFromTray(false);
			soundNotifyChanged = true;
		}
		if (settings.rememberedFlashBounceNotifyFromTray()
			&& !settings.flashBounceNotify()) {
			settings.setFlashBounceNotify(true);
			settings.setRememberedFlashBounceNotifyFromTray(false);
			flashBounceNotifyChanged = true;
		}
	} else {
		if (settings.soundNotify()) {
			settings.setSoundNotify(false);
			settings.setRememberedSoundNotifyFromTray(true);
			soundNotifyChanged = true;
		} else {
			settings.setRememberedSoundNotifyFromTray(false);
		}
		if (settings.flashBounceNotify()) {
			settings.setFlashBounceNotify(false);
			settings.setRememberedFlashBounceNotifyFromTray(true);
			flashBounceNotifyChanged = true;
		} else {
			settings.setRememberedFlashBounceNotifyFromTray(false);
		}
	}
	Core::App().saveSettingsDelayed();
	using Change = Window::Notifications::ChangeType;
	auto &notifications = Core::App().notifications();
	notifications.notifySettingsChanged(Change::DesktopEnabled);
	if (soundNotifyChanged) {
		notifications.notifySettingsChanged(Change::SoundEnabled);
	}
	if (flashBounceNotifyChanged) {
		notifications.notifySettingsChanged(Change::FlashBounceEnabled);
	}
}

bool Tray::has() const {
	return _tray.hasIcon() && Platform::TrayIconSupported();
}

} // namespace Core
