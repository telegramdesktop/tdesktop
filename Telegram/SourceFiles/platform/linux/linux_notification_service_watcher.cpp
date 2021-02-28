/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_notification_service_watcher.h"

#include "core/application.h"
#include "main/main_domain.h"
#include "window/notifications_manager.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"

#include <QtDBus/QDBusConnection>

namespace Platform {
namespace internal {
namespace {

constexpr auto kNotificationService = "org.freedesktop.Notifications"_cs;

bool IsNotificationServiceActivatable() {
	static const auto Result = [] {
		try {
			const auto connection = Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::BUS_TYPE_SESSION);

			return ranges::contains(
				base::Platform::DBus::ListActivatableNames(connection),
				Glib::ustring(std::string(kNotificationService)));
		} catch (...) {
		}

		return false;
	}();

	return Result;
}

} // namespace

NotificationServiceWatcher::NotificationServiceWatcher()
: _dbusWatcher(
		kNotificationService.utf16(),
		QDBusConnection::sessionBus(),
		QDBusServiceWatcher::WatchForOwnerChange) {
	const auto signal = &QDBusServiceWatcher::serviceOwnerChanged;
	QObject::connect(&_dbusWatcher, signal, [=](
			const QString &service,
			const QString &oldOwner,
			const QString &newOwner) {
		crl::on_main([=] {
			if (!Core::App().domain().started()) {
				return;
			} else if (IsNotificationServiceActivatable()
				&& newOwner.isEmpty()) {
				return;
			}

			Core::App().notifications().createManager();
		});
	});
}

} // namespace internal
} // namespace Platform
