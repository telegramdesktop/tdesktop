/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gsd_media_keys.h"

#include "core/sandbox.h"
#include "media/player/media_player_instance.h"
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"

#include <glibmm.h>
#include <giomm.h>

namespace Platform {
namespace internal {
namespace {

constexpr auto kService = "org.gnome.SettingsDaemon.MediaKeys"_cs;
constexpr auto kOldService = "org.gnome.SettingsDaemon"_cs;
constexpr auto kMATEService = "org.mate.SettingsDaemon"_cs;
constexpr auto kObjectPath = "/org/gnome/SettingsDaemon/MediaKeys"_cs;
constexpr auto kMATEObjectPath = "/org/mate/SettingsDaemon/MediaKeys"_cs;
constexpr auto kInterface = kService;
constexpr auto kMATEInterface = "org.mate.SettingsDaemon.MediaKeys"_cs;

} // namespace

class GSDMediaKeys::Private : public sigc::trackable {
public:
	Glib::RefPtr<Gio::DBus::Connection> dbusConnection;

	Glib::ustring service;
	Glib::ustring objectPath;
	Glib::ustring interface;
	uint signalId = 0;
	bool grabbed = false;

	void keyPressed(
		const Glib::RefPtr<Gio::DBus::Connection> &connection,
		const Glib::ustring &sender_name,
		const Glib::ustring &object_path,
		const Glib::ustring &interface_name,
		const Glib::ustring &signal_name,
		const Glib::VariantContainerBase &parameters);
};

void GSDMediaKeys::Private::keyPressed(
		const Glib::RefPtr<Gio::DBus::Connection> &connection,
		const Glib::ustring &sender_name,
		const Glib::ustring &object_path,
		const Glib::ustring &interface_name,
		const Glib::ustring &signal_name,
		const Glib::VariantContainerBase &parameters) {
	try {
		auto parametersCopy = parameters;

		const auto app = base::Platform::GlibVariantCast<Glib::ustring>(
			parametersCopy.get_child(0));

		const auto key = base::Platform::GlibVariantCast<Glib::ustring>(
			parametersCopy.get_child(1));

		if (app != QCoreApplication::applicationName().toStdString()) {
			return;
		}

		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			if (key == "Play") {
				Media::Player::instance()->playPause();
			} else if (key == "Stop") {
				Media::Player::instance()->stop();
			} else if (key == "Next") {
				Media::Player::instance()->next();
			} else if (key == "Previous") {
				Media::Player::instance()->previous();
			}
		});
	} catch (...) {
	}
}

GSDMediaKeys::GSDMediaKeys()
: _private(std::make_unique<Private>()) {
	try {
		_private->dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		if (base::Platform::DBus::NameHasOwner(
			_private->dbusConnection,
			std::string(kService))) {
			_private->service = std::string(kService);
			_private->objectPath = std::string(kObjectPath);
			_private->interface = std::string(kInterface);
		} else if (base::Platform::DBus::NameHasOwner(
			_private->dbusConnection,
			std::string(kOldService))) {
			_private->service = std::string(kOldService);
			_private->objectPath = std::string(kObjectPath);
			_private->interface = std::string(kInterface);
		} else if (base::Platform::DBus::NameHasOwner(
			_private->dbusConnection,
			std::string(kMATEService))) {
			_private->service = std::string(kMATEService);
			_private->objectPath = std::string(kMATEObjectPath);
			_private->interface = std::string(kMATEInterface);
		} else {
			return;
		}

		_private->dbusConnection->call_sync(
			_private->objectPath,
			_private->interface,
			"GrabMediaPlayerKeys",
			base::Platform::MakeGlibVariant(std::tuple{
				Glib::ustring(
					QCoreApplication::applicationName()
						.toStdString()),
				uint(0),
			}),
			_private->service);

		_private->grabbed = true;

		_private->signalId = _private->dbusConnection->signal_subscribe(
			sigc::mem_fun(_private.get(), &Private::keyPressed),
			_private->service,
			_private->interface,
			"MediaPlayerKeyPressed",
			_private->objectPath);
	} catch (...) {
	}
}

GSDMediaKeys::~GSDMediaKeys() {
	if (_private->dbusConnection) {
		if (_private->signalId != 0) {
			_private->dbusConnection->signal_unsubscribe(_private->signalId);
		}

		if (_private->grabbed) {
			try {
				_private->dbusConnection->call_sync(
					_private->objectPath,
					_private->interface,
					"ReleaseMediaPlayerKeys",
					base::Platform::MakeGlibVariant(std::tuple{
						Glib::ustring(
							QCoreApplication::applicationName()
								.toStdString())
					}),
					_private->service);

				_private->grabbed = false;
			} catch (...) {
			}
		}
	}
}

} // namespace internal
} // namespace Platform
