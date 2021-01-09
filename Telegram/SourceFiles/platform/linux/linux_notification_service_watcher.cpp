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

#include <QtDBus/QDBusConnection>

namespace Platform {
namespace internal {

NotificationServiceWatcher::NotificationServiceWatcher()
: _dbusWatcher(
		qsl("org.freedesktop.Notifications"),
		QDBusConnection::sessionBus(),
		QDBusServiceWatcher::WatchForOwnerChange) {
	const auto signal = &QDBusServiceWatcher::serviceOwnerChanged;
	QObject::connect(&_dbusWatcher, signal, [=] {
		crl::on_main([=] {
			if (!Core::App().domain().started()) {
				return;
			}

			Core::App().notifications().createManager();
		});
	});
}

} // namespace internal
} // namespace Platform
