/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_xdp_open_with_dialog.h"

#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "core/application.h"
#include "window/window_controller.h"
#include "base/openssl_help.h"

#include <QtGui/QWindow>

#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <glibmm.h>
#include <giomm.h>
#include <private/qguiapplication_p.h>

namespace Platform {
namespace File {
namespace internal {
namespace {

constexpr auto kXDGDesktopPortalService = "org.freedesktop.portal.Desktop"_cs;
constexpr auto kXDGDesktopPortalObjectPath = "/org/freedesktop/portal/desktop"_cs;
constexpr auto kXDGDesktopPortalOpenURIInterface = "org.freedesktop.portal.OpenURI"_cs;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

class XDPOpenWithDialog : public QWindow {
public:
	XDPOpenWithDialog(const QString &filepath)
	: _filepath(filepath.toStdString()) {
	}

	bool exec();

private:
	Glib::ustring _filepath;
};

bool XDPOpenWithDialog::exec() {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		auto reply = connection->call_sync(
			std::string(kXDGDesktopPortalObjectPath),
			std::string(kPropertiesInterface),
			"Get",
			base::Platform::MakeGlibVariant(std::tuple{
				Glib::ustring(
					std::string(kXDGDesktopPortalOpenURIInterface)),
				Glib::ustring("version"),
			}),
			std::string(kXDGDesktopPortalService));

		const auto version = base::Platform::GlibVariantCast<uint>(
			base::Platform::GlibVariantCast<Glib::VariantBase>(
				reply.get_child(0)));

		if (version < 3) {
			return false;
		}

		const auto fd = open(
			_filepath.c_str(),
			O_RDONLY);

		if (fd == -1) {
			return false;
		}

		const auto fdGuard = gsl::finally([&] { ::close(fd); });
		auto outFdList = Glib::RefPtr<Gio::UnixFDList>();

		const auto parentWindowId = [&]() -> Glib::ustring {
			std::stringstream result;
			if (const auto activeWindow = Core::App().activeWindow()) {
				if (IsX11()) {
					result
						<< "x11:"
						<< std::hex
						<< activeWindow
							->widget()
							.get()
							->windowHandle()
							->winId();
				}
			}
			return result.str();
		}();

		const auto handleToken = Glib::ustring("tdesktop")
			+ std::to_string(openssl::RandomValue<uint>());

		auto uniqueName = connection->get_unique_name();
		uniqueName.erase(0, 1);
		uniqueName.replace(uniqueName.find('.'), 1, 1, '_');

		const auto requestPath = Glib::ustring(
				"/org/freedesktop/portal/desktop/request/")
			+ uniqueName
			+ '/'
			+ handleToken;

		QEventLoop loop;

		const auto signalId = connection->signal_subscribe(
			[&](
				const Glib::RefPtr<Gio::DBus::Connection> &connection,
				const Glib::ustring &sender_name,
				const Glib::ustring &object_path,
				const Glib::ustring &interface_name,
				const Glib::ustring &signal_name,
				const Glib::VariantContainerBase &parameters) {
				loop.quit();
			},
			std::string(kXDGDesktopPortalService),
			"org.freedesktop.portal.Request",
			"Response",
			requestPath);

		const auto signalGuard = gsl::finally([&] {
			if (signalId != 0) {
				connection->signal_unsubscribe(signalId);
			}
		});

		connection->call_sync(
			std::string(kXDGDesktopPortalObjectPath),
			std::string(kXDGDesktopPortalOpenURIInterface),
			"OpenFile",
			Glib::VariantContainerBase::create_tuple({
				Glib::Variant<Glib::ustring>::create(parentWindowId),
				Glib::wrap(g_variant_new_handle(0)),
				Glib::Variant<std::map<
					Glib::ustring,
					Glib::VariantBase
				>>::create({
					{
						"handle_token",
						Glib::Variant<Glib::ustring>::create(handleToken)
					},
					{
						"ask",
						Glib::Variant<bool>::create(true)
					},
				}),
			}),
			Glib::wrap(g_unix_fd_list_new_from_array(&fd, 1)),
			outFdList,
			std::string(kXDGDesktopPortalService));

		if (signalId != 0) {
			QGuiApplicationPrivate::showModalWindow(this);
			loop.exec();
			QGuiApplicationPrivate::hideModalWindow(this);
		}

		return true;
	} catch (...) {
	}

	return false;
}

} // namespace

bool ShowXDPOpenWithDialog(const QString &filepath) {
	return XDPOpenWithDialog(filepath).exec();
}

} // namespace internal
} // namespace File
} // namespace Platform
