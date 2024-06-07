/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/file_utilities_linux.h"

#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_xdp_utilities.h"
#include "base/platform/linux/base_linux_xdg_activation_token.h"
#include "base/random.h"

#include <fcntl.h>
#include <xdpopenuri/xdpopenuri.hpp>
#include <xdprequest/xdprequest.hpp>

namespace Platform {
namespace File {
namespace {

using namespace gi::repository;
using base::Platform::XdgActivationToken;

} // namespace

bool UnsafeShowOpenWith(const QString &filepath) {
	auto proxy = XdpOpenURI::OpenURIProxy::new_for_bus_sync(
		Gio::BusType::SESSION_,
		Gio::DBusProxyFlags::NONE_,
		base::Platform::XDP::kService,
		base::Platform::XDP::kObjectPath,
		nullptr);

	if (!proxy) {
		return false;
	}

	auto interface = XdpOpenURI::OpenURI(proxy);
	if (interface.get_version() < 3) {
		return false;
	}

	const auto fd = open(
		QFile::encodeName(filepath).constData(),
		O_RDONLY | O_CLOEXEC);

	if (fd == -1) {
		return false;
	}

	const auto handleToken = "tdesktop"
		+ std::to_string(base::RandomValue<uint>());

	std::string uniqueName = proxy.get_connection().get_unique_name();
	uniqueName.erase(0, 1);
	uniqueName.replace(uniqueName.find('.'), 1, 1, '_');

	auto request = XdpRequest::Request(
		XdpRequest::RequestProxy::new_sync(
			proxy.get_connection(),
			Gio::DBusProxyFlags::NONE_,
			base::Platform::XDP::kService,
			base::Platform::XDP::kObjectPath
				+ std::string("/request/")
				+ uniqueName
				+ '/'
				+ handleToken,
			nullptr,
			nullptr));

	if (!request) {
		close(fd);
		return false;
	}

	auto loop = GLib::MainLoop::new_();

	const auto signalId = request.signal_response().connect([=](
			XdpRequest::Request,
			guint,
			GLib::Variant) mutable {
		loop.quit();
	});

	const auto signalGuard = gsl::finally([&] {
		request.disconnect(signalId);
	});

	auto result = interface.call_open_file_sync(
		base::Platform::XDP::ParentWindowID(),
		GLib::Variant::new_handle(0),
		GLib::Variant::new_array({
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("handle_token"),
				GLib::Variant::new_variant(
					GLib::Variant::new_string(handleToken))),
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("activation_token"),
				GLib::Variant::new_variant(
					GLib::Variant::new_string(
						XdgActivationToken().toStdString()))),
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("ask"),
				GLib::Variant::new_variant(
					GLib::Variant::new_boolean(true))),
		}),
		Gio::UnixFDList::new_from_array(&fd, 1),
		nullptr);

	if (!result) {
		return false;
	}

	QWidget window;
	window.setAttribute(Qt::WA_DontShowOnScreen);
	window.setWindowModality(Qt::ApplicationModal);
	window.show();
	loop.run();

	return true;
}

} // namespace File
} // namespace Platform
