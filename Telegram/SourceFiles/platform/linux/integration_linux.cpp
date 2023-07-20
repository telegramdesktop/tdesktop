/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/integration_linux.h"

#include "platform/platform_integration.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_xdp_utilities.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "base/random.h"

#include <QtCore/QAbstractEventDispatcher>

#include <xdpinhibit/xdpinhibit.hpp>
#include <giomm.h>

typedef GApplication TDesktopApplication;
typedef GApplicationClass TDesktopApplicationClass;

G_DEFINE_TYPE(
	TDesktopApplication,
	t_desktop_application,
	G_TYPE_APPLICATION)

static void t_desktop_application_class_init(
		TDesktopApplicationClass *klass) {
	const auto application_class = G_APPLICATION_CLASS(klass);

	application_class->local_command_line = [](
			GApplication *application,
			char ***arguments,
			int *exit_status) -> gboolean {
		return false;
	};

	application_class->command_line = [](
			GApplication *application,
			GApplicationCommandLine *cmdline) {
		return 0;
	};

	application_class->before_emit = [](
			GApplication *application,
			GVariant *platformData) {
		if (Platform::IsWayland()) {
			static const auto keys = {
				"activation-token",
				"desktop-startup-id",
			};
			for (const auto &key : keys) {
				const char *token = nullptr;
				g_variant_lookup(platformData, key, "&s", &token);
				if (token) {
					qputenv("XDG_ACTIVATION_TOKEN", token);
					break;
				}
			}
		}
	};

	application_class->add_platform_data = [](
			GApplication *application,
			GVariantBuilder *builder) {
		if (Platform::IsWayland()) {
			const auto token = qgetenv("XDG_ACTIVATION_TOKEN");
			if (!token.isEmpty()) {
				g_variant_builder_add(
					builder,
					"{sv}",
					"activation-token",
					g_variant_new_string(token.constData()));
				qunsetenv("XDG_ACTIVATION_TOKEN");
			}
		}
	};
}

static void t_desktop_application_init(TDesktopApplication *application) {
}

namespace Platform {
namespace {

using namespace gi::repository;
namespace Gio = gi::repository::Gio;

class LinuxIntegration final : public Integration {
public:
	LinuxIntegration();

	void init() override;

private:
	[[nodiscard]] XdpInhibit::Inhibit inhibit() {
		return _inhibitProxy;
	}

	void initInhibit();

	static void LaunchNativeApplication();

	XdpInhibit::InhibitProxy _inhibitProxy;
	base::Platform::XDP::SettingWatcher _darkModeWatcher;
};

LinuxIntegration::LinuxIntegration()
: _inhibitProxy(
	XdpInhibit::InhibitProxy::new_for_bus_sync(
		Gio::BusType::SESSION_,
		Gio::DBusProxyFlags::DO_NOT_AUTO_START_AT_CONSTRUCTION_,
		base::Platform::XDP::kService,
		base::Platform::XDP::kObjectPath,
		nullptr))
, _darkModeWatcher([](
	const Glib::ustring &group,
	const Glib::ustring &key,
	const Glib::VariantBase &value) {
	if (group == "org.freedesktop.appearance"
		&& key == "color-scheme") {
		try {
			const auto ivalue = value.get_dynamic<uint>();

			crl::on_main([=] {
				Core::App().settings().setSystemDarkMode(ivalue == 1);
			});
		} catch (...) {
		}
	}
}) {
	LOG(("Icon theme: %1").arg(QIcon::themeName()));
	LOG(("Fallback icon theme: %1").arg(QIcon::fallbackThemeName()));

	if (!QCoreApplication::eventDispatcher()->inherits(
		"QEventDispatcherGlib")) {
		g_warning("Qt is running without GLib event loop integration, "
			"except various functionality to not to work.");
	}
}

void LinuxIntegration::init() {
	initInhibit();

	Glib::signal_idle().connect_once([] {
		LaunchNativeApplication();
	});
}

void LinuxIntegration::initInhibit() {
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

void LinuxIntegration::LaunchNativeApplication() {
	const auto appId = QGuiApplication::desktopFileName().toStdString();

	const auto app = Glib::wrap(
		G_APPLICATION(
			g_object_new(
				t_desktop_application_get_type(),
				"application-id",
				::Gio::Application::id_is_valid(appId)
					? appId.c_str()
					: nullptr,
				"flags",
				G_APPLICATION_HANDLES_OPEN,
				nullptr)));

	app->signal_startup().connect([=] {
		// GNotification
		InvokeQueued(qApp, [] {
			Core::App().notifications().createManager();
		});

		QEventLoop().exec();
		app->quit();
	}, true);

	app->signal_activate().connect([] {
		Core::Sandbox::Instance().customEnterFromEventLoop([] {
			Core::App().activate();
		});
	}, true);

	app->signal_open().connect([](
			const ::Gio::Application::type_vec_files &files,
			const Glib::ustring &hint) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			for (const auto &file : files) {
				QFileOpenEvent e(
					QUrl(QString::fromStdString(file->get_uri())));
				QGuiApplication::sendEvent(qApp, &e);
			}
		});
	}, true);

	app->add_action("quit", [] {
		Core::Sandbox::Instance().customEnterFromEventLoop([] {
			Core::Quit();
		});
	});

	using Window::Notifications::Manager;
	using NotificationId = Manager::NotificationId;
	using NotificationIdTuple = std::invoke_result_t<
		decltype(&NotificationId::toTuple),
		NotificationId*
	>;

	const auto notificationIdVariantType = [] {
		try {
			return Glib::create_variant(
				NotificationId().toTuple()
			).get_type();
		} catch (...) {
			return Glib::VariantType();
		}
	}();

	app->add_action_with_parameter(
		"notification-activate",
		notificationIdVariantType,
		[](const Glib::VariantBase &parameter) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				try {
					const auto &app = Core::App();
					app.notifications().manager().notificationActivated(
						NotificationId::FromTuple(
							parameter.get_dynamic<NotificationIdTuple>()
						)
					);
				} catch (...) {
				}
			});
		});

	app->add_action_with_parameter(
		"notification-mark-as-read",
		notificationIdVariantType,
		[](const Glib::VariantBase &parameter) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				try {
					const auto &app = Core::App();
					app.notifications().manager().notificationReplied(
						NotificationId::FromTuple(
							parameter.get_dynamic<NotificationIdTuple>()
						),
						{}
					);
				} catch (...) {
				}
			});
		});

	app->run(0, nullptr);
}

} // namespace

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<LinuxIntegration>();
}

} // namespace Platform
