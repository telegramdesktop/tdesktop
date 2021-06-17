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
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"

#include <glibmm.h>
#include <giomm.h>

namespace Platform {
namespace internal {
namespace {

constexpr auto kService = "org.freedesktop.Notifications"_cs;

auto Activatable() {
	static const auto Result = []() -> std::optional<bool> {
		try {
			const auto connection = Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::BUS_TYPE_SESSION);

			return ranges::contains(
				base::Platform::DBus::ListActivatableNames(connection),
				Glib::ustring(std::string(kService)));
		} catch (...) {
		}

		return std::nullopt;
	}();

	return Result;
}

} // namespace

class NotificationServiceWatcher::Private {
public:
	Glib::RefPtr<Gio::DBus::Connection> dbusConnection;
	uint signalId = 0;
};

NotificationServiceWatcher::NotificationServiceWatcher()
: _private(std::make_unique<Private>()) {
	try {
		_private->dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		_private->signalId = base::Platform::DBus::RegisterServiceWatcher(
			_private->dbusConnection,
			std::string(kService),
			[](
				const Glib::ustring &service,
				const Glib::ustring &oldOwner,
				const Glib::ustring &newOwner) {
				if (!Core::App().domain().started()
					|| (Activatable().value_or(true) && newOwner.empty())) {
					return;
				}

				crl::on_main([] {
					Core::App().notifications().createManager();
				});
			});
	} catch (...) {
	}
}

NotificationServiceWatcher::~NotificationServiceWatcher() {
	if (_private->dbusConnection && _private->signalId != 0) {
		_private->dbusConnection->signal_unsubscribe(_private->signalId);
	}
}

} // namespace internal
} // namespace Platform
