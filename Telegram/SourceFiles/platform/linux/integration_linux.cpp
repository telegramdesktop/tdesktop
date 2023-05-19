/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/integration_linux.h"

#include "platform/platform_integration.h"
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "base/platform/linux/base_linux_xdp_utilities.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "base/random.h"

#include <xdpinhibit/xdpinhibit.hpp>
#include <glibmm.h>

using namespace gi::repository;

namespace Platform {
namespace {

class LinuxIntegration final : public Integration {
public:
	LinuxIntegration();

	void init() override;

private:
	[[nodiscard]] XdpInhibit::Inhibit inhibit() {
		return _inhibitProxy;
	}

	XdpInhibit::InhibitProxy _inhibitProxy;
	base::Platform::XDP::SettingWatcher _darkModeWatcher;
};

LinuxIntegration::LinuxIntegration()
: _inhibitProxy(
	XdpInhibit::InhibitProxy::new_for_bus_sync(
		Gio::BusType::SESSION_,
		Gio::DBusProxyFlags::DO_NOT_AUTO_START_AT_CONSTRUCTION_,
		std::string(base::Platform::XDP::kService),
		std::string(base::Platform::XDP::kObjectPath),
		nullptr))
, _darkModeWatcher([](
	const Glib::ustring &group,
	const Glib::ustring &key,
	const Glib::VariantBase &value) {
	if (group == "org.freedesktop.appearance"
		&& key == "color-scheme") {
		try {
			const auto ivalue = base::Platform::GlibVariantCast<uint>(value);

			crl::on_main([=] {
				Core::App().settings().setSystemDarkMode(ivalue == 1);
			});
		} catch (...) {
		}
	}
}) {
}

void LinuxIntegration::init() {
	if (!_inhibitProxy) {
		return;
	}

	auto uniqueName = _inhibitProxy.get_connection().get_unique_name();
	uniqueName.erase(0, 1);
	uniqueName.replace(uniqueName.find('.'), 1, 1, '_');

	const auto handleToken = "tdesktop"
		+ std::to_string(base::RandomValue<uint>());

	const auto sessionHandleToken = "tdesktop"
		+ std::to_string(base::RandomValue<uint>());

	const auto sessionHandle = "/org/freedesktop/portal/desktop/session/"
		+ uniqueName
		+ '/'
		+ sessionHandleToken;

	inhibit().signal_state_changed().connect([
		mySessionHandle = sessionHandle
	](
			XdpInhibit::Inhibit,
			const std::string &sessionHandle,
			GLib::Variant state) {
		if (sessionHandle != mySessionHandle) {
			return;
		}

		Core::App().setScreenIsLocked(
			GLib::VariantDict::new_(
				state
			).lookup_value(
				"screensaver-active"
			).get_boolean()
		);
	});

	const auto options = std::array{
		GLib::Variant::new_dict_entry(
			GLib::Variant::new_string("handle_token"),
			GLib::Variant::new_variant(
				GLib::Variant::new_string(handleToken))),
		GLib::Variant::new_dict_entry(
			GLib::Variant::new_string("session_handle_token"),
			GLib::Variant::new_variant(
				GLib::Variant::new_string(sessionHandleToken))),
	};

	inhibit().call_create_monitor(
		{},
		GLib::Variant::new_array(options.data(), options.size()),
		nullptr);
}

} // namespace

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<LinuxIntegration>();
}

} // namespace Platform
