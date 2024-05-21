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
#include "window/notifications_manager.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "base/random.h"

#include <QtCore/QAbstractEventDispatcher>

#include <gio/gio.hpp>
#include <xdpinhibit/xdpinhibit.hpp>

namespace Platform {
namespace {

using namespace gi::repository;
namespace GObject = gi::repository::GObject;

std::vector<std::any> AnyVectorFromVariant(GLib::Variant value) {
	std::vector<std::any> result;

	GLib::VariantIter iter;
	iter.allocate_();
	iter.init(value);

	const auto uint64Type = GLib::VariantType::new_("t");
	const auto int64Type = GLib::VariantType::new_("x");

	while (auto value = iter.next_value()) {
		value = value.get_variant();
		if (value.is_of_type(uint64Type)) {
			result.push_back(std::make_any<uint64>(value.get_uint64()));
		} else if (value.is_of_type(int64Type)) {
			result.push_back(std::make_any<int64>(value.get_int64()));
		} else if (value.is_container()) {
			result.push_back(
				std::make_any<std::vector<std::any>>(
					AnyVectorFromVariant(value)));
		}
	}

	return result;
}

class Application : public Gio::impl::ApplicationImpl {
public:
	Application();

	void before_emit_(GLib::Variant platformData) noexcept override {
		if (Platform::IsWayland()) {
			static const auto keys = {
				"activation-token",
				"desktop-startup-id",
			};
			for (const auto &key : keys) {
				if (auto token = platformData.lookup_value(key)) {
					qputenv(
						"XDG_ACTIVATION_TOKEN",
						token.get_string(nullptr).c_str());
					break;
				}
			}
		}
	}

	void activate_() noexcept override {
		Core::Sandbox::Instance().customEnterFromEventLoop([] {
			Core::App().activate();
		});
	}

	void open_(
			gi::Collection<gi::DSpan, ::GFile*, gi::transfer_none_t> files,
			const gi::cstring_v hint) noexcept override {
		for (auto file : files) {
			QFileOpenEvent e(QUrl(QString::fromStdString(file.get_uri())));
			QGuiApplication::sendEvent(qApp, &e);
		}
	}

	void add_platform_data_(
			GLib::VariantBuilder_Ref builder) noexcept override {
		if (Platform::IsWayland()) {
			const auto token = qgetenv("XDG_ACTIVATION_TOKEN");
			if (!token.isEmpty()) {
				builder.add_value(
					GLib::Variant::new_dict_entry(
						GLib::Variant::new_string("activation-token"),
						GLib::Variant::new_variant(
							GLib::Variant::new_string(token.toStdString()))));
				qunsetenv("XDG_ACTIVATION_TOKEN");
			}
		}
	}
};

Application::Application()
: Gio::impl::ApplicationImpl(this) {
	const auto appId = QGuiApplication::desktopFileName().toStdString();
	if (Gio::Application::id_is_valid(appId)) {
		set_application_id(appId);
	}
	set_flags(Gio::ApplicationFlags::HANDLES_OPEN_);

	auto actionMap = Gio::ActionMap(*this);

	auto quitAction = Gio::SimpleAction::new_("quit");
	quitAction.signal_activate().connect([](
			Gio::SimpleAction,
			GLib::Variant parameter) {
		Core::Sandbox::Instance().customEnterFromEventLoop([] {
			Core::Quit();
		});
	});
	actionMap.add_action(quitAction);

	using Window::Notifications::Manager;
	using NotificationId = Manager::NotificationId;

	const auto notificationIdVariantType = GLib::VariantType::new_("av");

	auto notificationActivateAction = Gio::SimpleAction::new_(
		"notification-activate",
		notificationIdVariantType);

	notificationActivateAction.signal_activate().connect([](
			Gio::SimpleAction,
			GLib::Variant parameter) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			Core::App().notifications().manager().notificationActivated(
				NotificationId::FromAnyVector(
					AnyVectorFromVariant(parameter)));
		});
	});

	actionMap.add_action(notificationActivateAction);

	auto notificationMarkAsReadAction = Gio::SimpleAction::new_(
		"notification-mark-as-read",
		notificationIdVariantType);

	notificationMarkAsReadAction.signal_activate().connect([](
			Gio::SimpleAction,
			GLib::Variant parameter) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			Core::App().notifications().manager().notificationReplied(
				NotificationId::FromAnyVector(
					AnyVectorFromVariant(parameter)),
				{});
		});
	});

	actionMap.add_action(notificationMarkAsReadAction);
}

gi::ref_ptr<Application> MakeApplication() {
	const auto result = gi::make_ref<Application>();
	if (const auto registered = result->register_(); !registered) {
		LOG(("App Error: Failed to register: %1").arg(
			registered.error().message_().c_str()));
		return nullptr;
	}
	return result;
}

class LinuxIntegration final : public Integration, public base::has_weak_ptr {
public:
	LinuxIntegration();

	void init() override;

private:
	[[nodiscard]] XdpInhibit::Inhibit inhibit() {
		return _inhibitProxy;
	}

	void initInhibit();

	const gi::ref_ptr<Application> _application;
	XdpInhibit::InhibitProxy _inhibitProxy;
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
	base::Platform::XDP::SettingWatcher _darkModeWatcher;
#endif // Qt < 6.5.0
};

LinuxIntegration::LinuxIntegration()
: _application(MakeApplication())
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
, _darkModeWatcher(
	"org.freedesktop.appearance",
	"color-scheme",
	[](GLib::Variant value) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			Core::App().settings().setSystemDarkMode(value.get_uint32() == 1);
		});
})
#endif // Qt < 6.5.0
{
	LOG(("Icon theme: %1").arg(QIcon::themeName()));
	LOG(("Fallback icon theme: %1").arg(QIcon::fallbackThemeName()));

	if (!QCoreApplication::eventDispatcher()->inherits(
		"QEventDispatcherGlib")) {
		g_warning("Qt is running without GLib event loop integration, "
			"expect various functionality to not to work.");
	}
}

void LinuxIntegration::init() {
	XdpInhibit::InhibitProxy::new_for_bus(
		Gio::BusType::SESSION_,
		Gio::DBusProxyFlags::NONE_,
		base::Platform::XDP::kService,
		base::Platform::XDP::kObjectPath,
		crl::guard(this, [=](GObject::Object, Gio::AsyncResult res) {
			_inhibitProxy = XdpInhibit::InhibitProxy::new_for_bus_finish(
				res,
				nullptr);

			initInhibit();
		}));
}

void LinuxIntegration::initInhibit() {
	if (!_inhibitProxy) {
		return;
	}

	std::string uniqueName = _inhibitProxy.get_connection().get_unique_name();
	uniqueName.erase(0, 1);
	uniqueName.replace(uniqueName.find('.'), 1, 1, '_');

	const auto handleToken = "tdesktop"
		+ std::to_string(base::RandomValue<uint>());

	const auto sessionHandleToken = "tdesktop"
		+ std::to_string(base::RandomValue<uint>());

	const auto sessionHandle = base::Platform::XDP::kObjectPath
		+ std::string("/session/")
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

	inhibit().call_create_monitor(
		"",
		GLib::Variant::new_array({
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("handle_token"),
				GLib::Variant::new_variant(
					GLib::Variant::new_string(handleToken))),
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("session_handle_token"),
				GLib::Variant::new_variant(
					GLib::Variant::new_string(sessionHandleToken))),
		}),
		nullptr);
}

} // namespace

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<LinuxIntegration>();
}

} // namespace Platform
