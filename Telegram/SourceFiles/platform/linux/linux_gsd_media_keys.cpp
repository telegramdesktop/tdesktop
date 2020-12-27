/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gsd_media_keys.h"

#include "core/sandbox.h"
#include "media/player/media_player_instance.h"

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusConnectionInterface>

extern "C" {
#undef signals
#include <gio/gio.h>
#define signals public
} // extern "C"

namespace Platform {
namespace internal {
namespace {

constexpr auto kDBusTimeout = 30000;
constexpr auto kService = "org.gnome.SettingsDaemon.MediaKeys"_cs;
constexpr auto kOldService = "org.gnome.SettingsDaemon"_cs;
constexpr auto kMATEService = "org.mate.SettingsDaemon"_cs;
constexpr auto kObjectPath = "/org/gnome/SettingsDaemon/MediaKeys"_cs;
constexpr auto kMATEObjectPath = "/org/mate/SettingsDaemon/MediaKeys"_cs;
constexpr auto kInterface = kService;
constexpr auto kMATEInterface = "org.mate.SettingsDaemon.MediaKeys"_cs;

void KeyPressed(
		GDBusConnection *connection,
		const gchar *sender_name,
		const gchar *object_path,
		const gchar *interface_name,
		const gchar *signal_name,
		GVariant *parameters,
		gpointer user_data) {
	gchar *appUtf8;
	gchar *keyUtf8;
	g_variant_get(parameters, "(ss)", &appUtf8, &keyUtf8);
	const auto app = QString::fromUtf8(appUtf8);
	const auto key = QString::fromUtf8(keyUtf8);
	g_free(keyUtf8);
	g_free(appUtf8);

	if (app != QCoreApplication::applicationName()) {
		return;
	}

	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		if (key == qstr("Play")) {
			Media::Player::instance()->playPause();
		} else if (key == qstr("Stop")) {
			Media::Player::instance()->stop();
		} else if (key == qstr("Next")) {
			Media::Player::instance()->next();
		} else if (key == qstr("Previous")) {
			Media::Player::instance()->previous();
		}
	});
}

} // namespace

GSDMediaKeys::GSDMediaKeys() {
	GError *error = nullptr;
	const auto interface = QDBusConnection::sessionBus().interface();

	if (!interface) {
		return;
	}

	if (interface->isServiceRegistered(kService.utf16())) {
		_service = kService.utf16();
		_objectPath = kObjectPath.utf16();
		_interface = kInterface.utf16();
	} else if (interface->isServiceRegistered(kOldService.utf16())) {
		_service = kOldService.utf16();
		_objectPath = kObjectPath.utf16();
		_interface = kInterface.utf16();
	} else if (interface->isServiceRegistered(kMATEService.utf16())) {
		_service = kMATEService.utf16();
		_objectPath = kMATEObjectPath.utf16();
		_interface = kMATEInterface.utf16();
	} else {
		return;
	}

	_dbusConnection = g_bus_get_sync(
		G_BUS_TYPE_SESSION,
		nullptr,
		&error);

	if (error) {
		LOG(("GSD Media Keys Error: %1").arg(error->message));
		g_error_free(error);
		return;
	}

	auto reply = g_dbus_connection_call_sync(
		_dbusConnection,
		_service.toUtf8(),
		_objectPath.toUtf8(),
		_interface.toUtf8(),
		"GrabMediaPlayerKeys",
		g_variant_new(
			"(su)",
			QCoreApplication::applicationName().toUtf8().constData(),
			0),
		nullptr,
		G_DBUS_CALL_FLAGS_NONE,
		kDBusTimeout,
		nullptr,
		&error);

	if (!error) {
		_grabbed = true;
		g_variant_unref(reply);
	} else {
		LOG(("GSD Media Keys Error: %1").arg(error->message));
		g_error_free(error);
	}

	_signalId = g_dbus_connection_signal_subscribe(
		_dbusConnection,
		_service.toUtf8(),
		_interface.toUtf8(),
		"MediaPlayerKeyPressed",
		_objectPath.toUtf8(),
		nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE,
		KeyPressed,
		nullptr,
		nullptr);
}

GSDMediaKeys::~GSDMediaKeys() {
	GError *error = nullptr;

	if (_signalId != 0) {
		g_dbus_connection_signal_unsubscribe(
			_dbusConnection,
			_signalId);
	}

	if (_grabbed) {
		auto reply = g_dbus_connection_call_sync(
			_dbusConnection,
			_service.toUtf8(),
			_objectPath.toUtf8(),
			_interface.toUtf8(),
			"ReleaseMediaPlayerKeys",
			g_variant_new(
				"(s)",
				QCoreApplication::applicationName().toUtf8().constData()),
			nullptr,
			G_DBUS_CALL_FLAGS_NONE,
			kDBusTimeout,
			nullptr,
			&error);

		if (!error) {
			_grabbed = false;
			g_variant_unref(reply);
		} else {
			LOG(("GSD Media Keys Error: %1").arg(error->message));
			g_error_free(error);
		}
	}

	if (_dbusConnection) {
		g_object_unref(_dbusConnection);
	}
}

} // namespace internal
} // namespace Platform
