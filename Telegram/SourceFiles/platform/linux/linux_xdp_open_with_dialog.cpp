/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_xdp_open_with_dialog.h"

#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_xdp_utilities.h"
#include "base/platform/linux/base_linux_xdg_activation_token.h"
#include "base/random.h"

#include <fcntl.h>
#include <glibmm.h>
#include <giomm.h>

namespace Platform {
namespace File {
namespace internal {
namespace {

constexpr auto kXDPOpenURIInterface = "org.freedesktop.portal.OpenURI";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";
using base::Platform::XdgActivationToken;

} // namespace

bool ShowXDPOpenWithDialog(const QString &filepath) {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);

		const auto version = connection->call_sync(
			base::Platform::XDP::kObjectPath,
			kPropertiesInterface,
			"Get",
			Glib::create_variant(std::tuple{
				Glib::ustring(kXDPOpenURIInterface),
				Glib::ustring("version"),
			}),
			base::Platform::XDP::kService
		).get_child(0).get_dynamic<Glib::Variant<uint>>().get();

		if (version < 3) {
			return false;
		}

		const auto filepathUtf8 = filepath.toUtf8();

		const auto fd = open(
			filepathUtf8.constData(),
			O_RDONLY);

		if (fd == -1) {
			return false;
		}

		const auto fdGuard = gsl::finally([&] { ::close(fd); });

		const auto handleToken = Glib::ustring("tdesktop")
			+ std::to_string(base::RandomValue<uint>());

		auto uniqueName = connection->get_unique_name();
		uniqueName.erase(0, 1);
		uniqueName.replace(uniqueName.find('.'), 1, 1, '_');

		const auto requestPath = Glib::ustring(
				"/org/freedesktop/portal/desktop/request/")
			+ uniqueName
			+ '/'
			+ handleToken;

		const auto loop = Glib::MainLoop::create();

		const auto signalId = connection->signal_subscribe(
			[&](
				const Glib::RefPtr<Gio::DBus::Connection> &connection,
				const Glib::ustring &sender_name,
				const Glib::ustring &object_path,
				const Glib::ustring &interface_name,
				const Glib::ustring &signal_name,
				const Glib::VariantContainerBase &parameters) {
				loop->quit();
			},
			base::Platform::XDP::kService,
			base::Platform::XDP::kRequestInterface,
			"Response",
			requestPath);

		const auto signalGuard = gsl::finally([&] {
			if (signalId != 0) {
				connection->signal_unsubscribe(signalId);
			}
		});

		auto outFdList = Glib::RefPtr<Gio::UnixFDList>();

		connection->call_sync(
			base::Platform::XDP::kObjectPath,
			kXDPOpenURIInterface,
			"OpenFile",
			Glib::create_variant(std::tuple{
				base::Platform::XDP::ParentWindowID(),
				Glib::DBusHandle(),
				std::map<Glib::ustring, Glib::VariantBase>{
					{
						"handle_token",
						Glib::create_variant(handleToken)
					},
					{
						"activation_token",
						Glib::create_variant(
							Glib::ustring(XdgActivationToken().toStdString()))
					},
					{
						"ask",
						Glib::create_variant(true)
					},
				},
			}),
			Gio::UnixFDList::create(std::vector<int>{ fd }),
			outFdList,
			base::Platform::XDP::kService);

		if (signalId != 0) {
			QWidget window;
			window.setAttribute(Qt::WA_DontShowOnScreen);
			window.setWindowModality(Qt::ApplicationModal);
			window.show();
			loop->run();
		}

		return true;
	} catch (...) {
	}

	return false;
}

} // namespace internal
} // namespace File
} // namespace Platform
