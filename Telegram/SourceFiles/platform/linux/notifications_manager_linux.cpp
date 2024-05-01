
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/notifications_manager_linux.h"

#include "base/options.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "core/core_settings.h"
#include "data/data_forum_topic.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "base/weak_ptr.h"
#include "window/notifications_utilities.h"

#include <QtCore/QBuffer>
#include <QtCore/QVersionNumber>
#include <QtGui/QGuiApplication>

#include <xdgnotifications/xdgnotifications.hpp>

#include <dlfcn.h>

namespace Platform {
namespace Notifications {
namespace {

using namespace gi::repository;
namespace GObject = gi::repository::GObject;

constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kObjectPath = "/org/freedesktop/Notifications";

struct ServerInformation {
	std::string name;
	std::string vendor;
	QVersionNumber version;
	QVersionNumber specVersion;
};

bool ServiceRegistered = false;
ServerInformation CurrentServerInformation;
std::vector<std::string> CurrentCapabilities;

[[nodiscard]] bool HasCapability(const char *value) {
	return ranges::contains(CurrentCapabilities, value);
}

std::unique_ptr<base::Platform::DBus::ServiceWatcher> CreateServiceWatcher() {
	auto connection = Gio::bus_get_sync(Gio::BusType::SESSION_, nullptr);
	if (!connection) {
		return nullptr;
	}

	const auto activatable = [&] {
		const auto names = base::Platform::DBus::ListActivatableNames(
			connection.gobj_());

		if (!names) {
			// avoid service restart loop in sandboxed environments
			return true;
		}

		return ranges::contains(*names, kService);
	}();

	return std::make_unique<base::Platform::DBus::ServiceWatcher>(
		connection.gobj_(),
		kService,
		[=](
			const std::string &service,
			const std::string &oldOwner,
			const std::string &newOwner) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				if (activatable && newOwner.empty()) {
					Core::App().notifications().clearAll();
				} else {
					Core::App().notifications().createManager();
				}
			});
		});
}

void StartServiceAsync(Gio::DBusConnection connection, Fn<void()> callback) {
	namespace DBus = base::Platform::DBus;
	DBus::StartServiceByNameAsync(
		connection.gobj_(),
		kService,
		[=](Fn<DBus::Result<DBus::StartReply>()> result) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				// get the error if any
				if (const auto ret = result(); !ret) {
					const auto &error = *static_cast<GLib::Error*>(
						ret.error().get());

					if (error.gobj_()->domain != G_DBUS_ERROR
							|| error.code_()
								!= G_DBUS_ERROR_SERVICE_UNKNOWN) {
						Gio::DBusErrorNS_::strip_remote_error(error);
						LOG(("Native Notification Error: %1").arg(
							error.message_().c_str()));
					}
				}

				callback();
			});
		});
}

std::string GetImageKey() {
	const auto &specVersion = CurrentServerInformation.specVersion;
	if (specVersion >= QVersionNumber(1, 2)) {
		return "image-data";
	} else if (specVersion == QVersionNumber(1, 1)) {
		return "image_data";
	}
	return "icon_data";
}

bool UseGNotification() {
	if (!Gio::Application::get_default()) {
		return false;
	}

	if (Window::Notifications::OptionGNotification.value()) {
		return true;
	}

	return KSandbox::isFlatpak() && !ServiceRegistered;
}

GLib::Variant AnyVectorToVariant(const std::vector<std::any> &value) {
	return GLib::Variant::new_array(
		value | ranges::views::transform([](const std::any &value) {
			try {
				return GLib::Variant::new_variant(
					GLib::Variant::new_uint64(std::any_cast<uint64>(value)));
			} catch (...) {
			}

			try {
				return GLib::Variant::new_variant(
					GLib::Variant::new_int64(std::any_cast<int64>(value)));
			} catch (...) {
			}

			try {
				return GLib::Variant::new_variant(
					AnyVectorToVariant(
						std::any_cast<std::vector<std::any>>(value)));
			} catch (...) {
			}

			return GLib::Variant(nullptr);
		}) | ranges::to_vector);
}

class NotificationData final : public base::has_weak_ptr {
public:
	using NotificationId = Window::Notifications::Manager::NotificationId;

	NotificationData(
		not_null<Manager*> manager,
		XdgNotifications::NotificationsProxy proxy,
		NotificationId id);

	[[nodiscard]] bool init(
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		Window::Notifications::Manager::DisplayOptions options);

	NotificationData(const NotificationData &other) = delete;
	NotificationData &operator=(const NotificationData &other) = delete;
	NotificationData(NotificationData &&other) = delete;
	NotificationData &operator=(NotificationData &&other) = delete;

	~NotificationData();

	void show();
	void close();
	void setImage(QImage image);

private:
	const not_null<Manager*> _manager;
	NotificationId _id;

	Gio::Application _application;
	Gio::Notification _notification;
	const std::string _guid;

	XdgNotifications::NotificationsProxy _proxy;
	XdgNotifications::Notifications _interface;
	std::string _title;
	std::string _body;
	std::vector<std::string> _actions;
	GLib::VariantDict _hints;
	std::string _imageKey;

	uint _notificationId = 0;
	ulong _actionInvokedSignalId = 0;
	ulong _activationTokenSignalId = 0;
	ulong _notificationRepliedSignalId = 0;
	ulong _notificationClosedSignalId = 0;

};

using Notification = std::unique_ptr<NotificationData>;

NotificationData::NotificationData(
	not_null<Manager*> manager,
	XdgNotifications::NotificationsProxy proxy,
	NotificationId id)
: _manager(manager)
, _id(id)
, _application(UseGNotification()
		? Gio::Application::get_default()
		: nullptr)
, _guid(_application ? std::string(Gio::dbus_generate_guid()) : std::string())
, _proxy(proxy)
, _interface(proxy)
, _hints(GLib::VariantDict::new_())
, _imageKey(GetImageKey()) {
}

bool NotificationData::init(
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		Window::Notifications::Manager::DisplayOptions options) {
	if (_application) {
		_notification = Gio::Notification::new_(
			subtitle.isEmpty()
				? title.toStdString()
				: subtitle.toStdString() + " (" + title.toStdString() + ')');

		_notification.set_body(msg.toStdString());

		_notification.set_icon(
			Gio::ThemedIcon::new_(base::IconName().toStdString()));

		// for chat messages, according to
		// https://docs.gtk.org/gio/enum.NotificationPriority.html
		_notification.set_priority(Gio::NotificationPriority::HIGH_);

		// glib 2.70+, we keep glib 2.56+ compatibility
		static const auto set_category = [] {
			// reset dlerror after dlsym call
			const auto guard = gsl::finally([] { dlerror(); });
			return reinterpret_cast<void(*)(GNotification*, const gchar*)>(
				dlsym(RTLD_DEFAULT, "g_notification_set_category"));
		}();

		if (set_category) {
			set_category(_notification.gobj_(), "im.received");
		}

		const auto idVariant = AnyVectorToVariant(_id.toAnyVector());

		_notification.set_default_action_and_target(
			"app.notification-activate",
			idVariant);

		if (!options.hideMarkAsRead) {
			_notification.add_button_with_target(
				tr::lng_context_mark_read(tr::now).toStdString(),
				"app.notification-mark-as-read",
				idVariant);
		}

		return true;
	}

	if (!_interface) {
		return false;
	}

	if (HasCapability("body-markup")) {
		_title = title.toStdString();

		_body = subtitle.isEmpty()
			? msg.toHtmlEscaped().toStdString()
			: u"<b>%1</b>\n%2"_q.arg(
				subtitle.toHtmlEscaped(),
				msg.toHtmlEscaped()).toStdString();
	} else {
		_title = subtitle.isEmpty()
			? title.toStdString()
			: subtitle.toStdString() + " (" + title.toStdString() + ')';

		_body = msg.toStdString();
	}

	if (HasCapability("actions")) {
		_actions.push_back("default");
		_actions.push_back(tr::lng_open_link(tr::now).toStdString());

		if (!options.hideMarkAsRead) {
			// icon name according to https://specifications.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html
			_actions.push_back("mail-mark-read");
			_actions.push_back(
				tr::lng_context_mark_read(tr::now).toStdString());
		}

		if (HasCapability("inline-reply")
				&& !options.hideReplyButton) {
			_actions.push_back("inline-reply");
			_actions.push_back(
				tr::lng_notification_reply(tr::now).toStdString());

			_notificationRepliedSignalId
				= _interface.signal_notification_replied().connect([=](
						XdgNotifications::Notifications,
						uint id,
						std::string text) {
					Core::Sandbox::Instance().customEnterFromEventLoop([&] {
						if (id == _notificationId) {
							_manager->notificationReplied(
								_id,
								{ QString::fromStdString(text), {} });
						}
					});
				});
		}

		_actionInvokedSignalId = _interface.signal_action_invoked().connect(
			[=](
					XdgNotifications::Notifications,
					uint id,
					std::string actionName) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					if (id == _notificationId) {
						if (actionName == "default") {
							_manager->notificationActivated(_id);
						} else if (actionName == "mail-mark-read") {
							_manager->notificationReplied(_id, {});
						}
					}
				});
			});

		_activationTokenSignalId
			= _interface.signal_activation_token().connect([=](
					XdgNotifications::Notifications,
					uint id,
					std::string token) {
				if (id == _notificationId) {
					GLib::setenv("XDG_ACTIVATION_TOKEN", token, true);
				}
			});
	}

	if (HasCapability("action-icons")) {
		_hints.insert_value("action-icons", GLib::Variant::new_boolean(true));
	}

	// suppress system sound if telegram sound activated,
	// otherwise use system sound
	if (HasCapability("sound")) {
		if (Core::App().settings().soundNotify()) {
			_hints.insert_value(
				"suppress-sound",
				GLib::Variant::new_boolean(true));
		} else {
			// sound name according to http://0pointer.de/public/sound-naming-spec.html
			_hints.insert_value(
				"sound-name",
				GLib::Variant::new_string("message-new-instant"));
		}
	}

	if (HasCapability("x-canonical-append")) {
		_hints.insert_value(
			"x-canonical-append",
			GLib::Variant::new_string("true"));
	}

	_hints.insert_value("category", GLib::Variant::new_string("im.received"));

	_hints.insert_value("desktop-entry", GLib::Variant::new_string(
		QGuiApplication::desktopFileName().toStdString()));

	_notificationClosedSignalId =
		_interface.signal_notification_closed().connect([=](
				XdgNotifications::Notifications,
				uint id,
				uint reason) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				/*
				* From: https://specifications.freedesktop.org/notification-spec/latest/ar01s09.html
				* The reason the notification was closed
				* 1 - The notification expired.
				* 2 - The notification was dismissed by the user.
				* 3 - The notification was closed by a call to CloseNotification.
				* 4 - Undefined/reserved reasons.
				*
				* If the notification was dismissed by the user (reason == 2), the notification is not kept in notification history.
				* We do not need to send a "CloseNotification" call later to clear it from history.
				* Therefore we can drop the notification reference now.
				* In all other cases we keep the notification reference so that we may clear the notification later from history,
				* if the message for that notification is read (e.g. chat is opened or read from another device).
				*/
				if (id == _notificationId && reason == 2) {
					_manager->clearNotification(_id);
				}
			});
		});

	return true;
}

NotificationData::~NotificationData() {
	if (_interface) {
		if (_actionInvokedSignalId != 0) {
			_interface.disconnect(_actionInvokedSignalId);
		}

		if (_activationTokenSignalId != 0) {
			_interface.disconnect(_activationTokenSignalId);
		}

		if (_notificationRepliedSignalId != 0) {
			_interface.disconnect(_notificationRepliedSignalId);
		}

		if (_notificationClosedSignalId != 0) {
			_interface.disconnect(_notificationClosedSignalId);
		}
	}
}

void NotificationData::show() {
	if (_application && _notification) {
		_application.send_notification(_guid, _notification);
		return;
	}

	// a hack for snap's activation restriction
	const auto weak = base::make_weak(this);
	StartServiceAsync(_proxy.get_connection(), crl::guard(weak, [=] {
		const auto iconName = _imageKey.empty()
			|| !_hints.lookup_value(_imageKey)
				? base::IconName().toStdString()
				: std::string();

		auto actions = _actions
			| ranges::views::transform(&std::string::c_str)
			| ranges::to_vector;
		actions.push_back(nullptr);

		const auto callbackWrap = gi::unwrap(
			Gio::AsyncReadyCallback(
				crl::guard(weak, [=](GObject::Object, Gio::AsyncResult res) {
					Core::Sandbox::Instance().customEnterFromEventLoop([&] {
						const auto result = _interface.call_notify_finish(
							res);

						if (!result) {
							Gio::DBusErrorNS_::strip_remote_error(
								result.error());
							LOG(("Native Notification Error: %1").arg(
								result.error().message_().c_str()));
							_manager->clearNotification(_id);
							return;
						}

						_notificationId = std::get<1>(*result);
					});
				})),
			gi::scope_async);

		xdg_notifications_notifications_call_notify(
			_interface.gobj_(),
			AppName.data(),
			0,
			iconName.c_str(),
			_title.c_str(),
			_body.c_str(),
			actions.data(),
			_hints.end().gobj_(),
			-1,
			nullptr,
			&callbackWrap->wrapper,
			callbackWrap);
	}));
}

void NotificationData::close() {
	if (_application) {
		_application.withdraw_notification(_guid);
	} else {
		_interface.call_close_notification(_notificationId, nullptr);
	}
	_manager->clearNotification(_id);
}

void NotificationData::setImage(QImage image) {
	if (_notification) {
		const auto imageData = std::make_shared<QByteArray>();
		QBuffer buffer(imageData.get());
		buffer.open(QIODevice::WriteOnly);
		image.save(&buffer, "PNG");

		_notification.set_icon(
			Gio::BytesIcon::new_(
				GLib::Bytes::new_with_free_func(
					reinterpret_cast<const uchar*>(imageData->constData()),
					imageData->size(),
					[imageData] {})));

		return;
	}

	if (_imageKey.empty()) {
		return;
	}

	if (image.hasAlphaChannel()) {
		image.convertTo(QImage::Format_RGBA8888);
	} else {
		image.convertTo(QImage::Format_RGB888);
	}

	_hints.insert_value(_imageKey, GLib::Variant::new_tuple({
		GLib::Variant::new_int32(image.width()),
		GLib::Variant::new_int32(image.height()),
		GLib::Variant::new_int32(image.bytesPerLine()),
		GLib::Variant::new_boolean(image.hasAlphaChannel()),
		GLib::Variant::new_int32(8),
		GLib::Variant::new_int32(image.hasAlphaChannel() ? 4 : 3),
		GLib::Variant::new_from_data(
			GLib::VariantType::new_("ay"),
			reinterpret_cast<const uchar*>(image.constBits()),
			image.sizeInBytes(),
			true,
			[image] {}),
	}));
}

} // namespace

class Manager::Private : public base::has_weak_ptr {
public:
	explicit Private(not_null<Manager*> manager);

	void init(XdgNotifications::NotificationsProxy proxy);

	void showNotification(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		Ui::PeerUserpicView &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		DisplayOptions options);
	void clearAll();
	void clearFromItem(not_null<HistoryItem*> item);
	void clearFromTopic(not_null<Data::ForumTopic*> topic);
	void clearFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
	void clearNotification(NotificationId id);
	void invokeIfNotInhibited(Fn<void()> callback);

	~Private();

private:
	const not_null<Manager*> _manager;

	base::flat_map<
		ContextId,
		base::flat_map<MsgId, Notification>> _notifications;

	XdgNotifications::NotificationsProxy _proxy;
	XdgNotifications::Notifications _interface;

};

bool SkipToastForCustom() {
	return false;
}

void MaybePlaySoundForCustom(Fn<void()> playSound) {
	playSound();
}

void MaybeFlashBounceForCustom(Fn<void()> flashBounce) {
	flashBounce();
}

bool WaitForInputForCustom() {
	return true;
}

bool Supported() {
	return ServiceRegistered || UseGNotification();
}

bool Enforced() {
	// Wayland doesn't support positioning
	// and custom notifications don't work here
	return IsWayland()
		|| (Gio::Application::get_default()
			&& Window::Notifications::OptionGNotification.value());
}

bool ByDefault() {
	// The capabilities are static, equivalent to 'body' and 'actions' only
	if (UseGNotification()) {
		return false;
	}

	// A list of capabilities that offer feature parity
	// with custom notifications
	return ranges::all_of(std::array{
		// To show message content
		"body",
		// To have buttons on notifications
		"actions",
		// To have quick reply
		"inline-reply",
		// To not to play sound with Don't Disturb activated
		// (no, using sound capability is not a way)
		"inhibitions",
	}, HasCapability);
}

void Create(Window::Notifications::System *system) {
	static const auto ServiceWatcher = CreateServiceWatcher();

	const auto managerSetter = [=](
			XdgNotifications::NotificationsProxy proxy) {
		using ManagerType = Window::Notifications::ManagerType;
		if ((Core::App().settings().nativeNotifications() || Enforced())
			&& Supported()) {
			if (system->manager().type() != ManagerType::Native) {
				auto manager = std::make_unique<Manager>(system);
				manager->_private->init(proxy);
				system->setManager(std::move(manager));
			}
		} else if (Enforced()) {
			if (system->manager().type() != ManagerType::Dummy) {
				using DummyManager = Window::Notifications::DummyManager;
				system->setManager(std::make_unique<DummyManager>(system));
			}
		} else if (system->manager().type() != ManagerType::Default) {
			system->setManager(nullptr);
		}
	};

	const auto counter = std::make_shared<int>(2);
	const auto oneReady = [=](XdgNotifications::NotificationsProxy proxy) {
		if (!--*counter) {
			managerSetter(proxy);
		}
	};

	XdgNotifications::NotificationsProxy::new_for_bus(
		Gio::BusType::SESSION_,
		Gio::DBusProxyFlags::NONE_,
		kService,
		kObjectPath,
		[=](GObject::Object, Gio::AsyncResult res) {
			auto proxy =
				XdgNotifications::NotificationsProxy::new_for_bus_finish(
					res,
					nullptr);

			if (!proxy) {
				ServiceRegistered = false;
				CurrentServerInformation = {};
				CurrentCapabilities = {};
				managerSetter(nullptr);
				return;
			}

			ServiceRegistered = bool(proxy.get_name_owner());
			if (!ServiceRegistered) {
				CurrentServerInformation = {};
				CurrentCapabilities = {};
				managerSetter(proxy);
				return;
			}

			auto interface = XdgNotifications::Notifications(proxy);

			interface.call_get_server_information([=](
					GObject::Object,
					Gio::AsyncResult res) mutable {
				const auto result =
					interface.call_get_server_information_finish(res);
				if (result) {
					CurrentServerInformation = {
						std::get<1>(*result),
						std::get<2>(*result),
						QVersionNumber::fromString(
							QString::fromStdString(std::get<3>(*result))
						).normalized(),
						QVersionNumber::fromString(
							QString::fromStdString(std::get<4>(*result))
						).normalized(),
					};
				} else {
					Gio::DBusErrorNS_::strip_remote_error(result.error());
					LOG(("Native Notification Error: %1").arg(
						result.error().message_().c_str()));
					CurrentServerInformation = {};
				}
				oneReady(proxy);
			});

			interface.call_get_capabilities([=](
					GObject::Object,
					Gio::AsyncResult res) mutable {
				const auto result = interface.call_get_capabilities_finish(
					res);
				if (result) {
					CurrentCapabilities = std::get<1>(*result)
						| ranges::to<std::vector<std::string>>;
				} else {
					Gio::DBusErrorNS_::strip_remote_error(result.error());
					LOG(("Native Notification Error: %1").arg(
						result.error().message_().c_str()));
					CurrentCapabilities = {};
				}
				oneReady(proxy);
			});
		});
}

Manager::Private::Private(not_null<Manager*> manager)
: _manager(manager) {
	const auto &serverInformation = CurrentServerInformation;

	if (!serverInformation.name.empty()) {
		LOG(("Notification daemon product name: %1")
			.arg(serverInformation.name.c_str()));
	}

	if (!serverInformation.vendor.empty()) {
		LOG(("Notification daemon vendor name: %1")
			.arg(serverInformation.vendor.c_str()));
	}

	if (!serverInformation.version.isNull()) {
		LOG(("Notification daemon version: %1")
			.arg(serverInformation.version.toString()));
	}

	if (!serverInformation.specVersion.isNull()) {
		LOG(("Notification daemon specification version: %1")
			.arg(serverInformation.specVersion.toString()));
	}

	if (!CurrentCapabilities.empty()) {
		LOG(("Notification daemon capabilities: %1").arg(
			ranges::fold_left(
				CurrentCapabilities,
				"",
				[](const std::string &a, const std::string &b) {
					return a + (a.empty() ? "" : ", ") + b;
				}).c_str()));
	}
}

void Manager::Private::init(XdgNotifications::NotificationsProxy proxy) {
	_proxy = proxy;
	_interface = proxy;
}

void Manager::Private::showNotification(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		Ui::PeerUserpicView &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		DisplayOptions options) {
	const auto key = ContextId{
		.sessionId = peer->session().uniqueId(),
		.peerId = peer->id,
		.topicRootId = topicRootId,
	};
	const auto notificationId = NotificationId{
		.contextId = key,
		.msgId = msgId,
	};
	auto notification = std::make_unique<NotificationData>(
		_manager,
		_proxy,
		notificationId);
	const auto inited = notification->init(
		title,
		subtitle,
		msg,
		options);
	if (!inited) {
		return;
	}

	if (!options.hideNameAndPhoto) {
		notification->setImage(
			Window::Notifications::GenerateUserpic(peer, userpicView));
	}

	auto i = _notifications.find(key);
	if (i != end(_notifications)) {
		auto j = i->second.find(msgId);
		if (j != end(i->second)) {
			auto oldNotification = std::move(j->second);
			i->second.erase(j);
			oldNotification->close();
			i = _notifications.find(key);
		}
	}
	if (i == end(_notifications)) {
		i = _notifications.emplace(
			key,
			base::flat_map<MsgId, Notification>()).first;
	}
	const auto j = i->second.emplace(
		msgId,
		std::move(notification)).first;
	j->second->show();
}

void Manager::Private::clearAll() {
	for (const auto &[key, notifications] : base::take(_notifications)) {
		for (const auto &[msgId, notification] : notifications) {
			notification->close();
		}
	}
}

void Manager::Private::clearFromItem(not_null<HistoryItem*> item) {
	const auto key = ContextId{
		.sessionId = item->history()->session().uniqueId(),
		.peerId = item->history()->peer->id,
		.topicRootId = item->topicRootId(),
	};
	const auto i = _notifications.find(key);
	if (i == _notifications.cend()) {
		return;
	}
	const auto j = i->second.find(item->id);
	if (j == i->second.end()) {
		return;
	}
	const auto taken = base::take(j->second);
	i->second.erase(j);
	if (i->second.empty()) {
		_notifications.erase(i);
	}
	taken->close();
}

void Manager::Private::clearFromTopic(not_null<Data::ForumTopic*> topic) {
	const auto key = ContextId{
		.sessionId = topic->session().uniqueId(),
		.peerId = topic->history()->peer->id
	};
	const auto i = _notifications.find(key);
	if (i != _notifications.cend()) {
		const auto temp = base::take(i->second);
		_notifications.erase(i);

		for (const auto &[msgId, notification] : temp) {
			notification->close();
		}
	}
}

void Manager::Private::clearFromHistory(not_null<History*> history) {
	const auto sessionId = history->session().uniqueId();
	const auto peerId = history->peer->id;
	auto i = _notifications.lower_bound(ContextId{
		.sessionId = sessionId,
		.peerId = peerId,
	});
	while (i != _notifications.cend()
		&& i->first.sessionId == sessionId
		&& i->first.peerId == peerId) {
		const auto temp = base::take(i->second);
		i = _notifications.erase(i);

		for (const auto &[msgId, notification] : temp) {
			notification->close();
		}
	}
}

void Manager::Private::clearFromSession(not_null<Main::Session*> session) {
	const auto sessionId = session->uniqueId();
	auto i = _notifications.lower_bound(ContextId{
		.sessionId = sessionId,
	});
	while (i != _notifications.cend() && i->first.sessionId == sessionId) {
		const auto temp = base::take(i->second);
		i = _notifications.erase(i);

		for (const auto &[msgId, notification] : temp) {
			notification->close();
		}
	}
}

void Manager::Private::clearNotification(NotificationId id) {
	auto i = _notifications.find(id.contextId);
	if (i != _notifications.cend()) {
		if (i->second.remove(id.msgId) && i->second.empty()) {
			_notifications.erase(i);
		}
	}
}

void Manager::Private::invokeIfNotInhibited(Fn<void()> callback) {
	if (!_interface.get_inhibited()) {
		callback();
	}
}

Manager::Private::~Private() {
	clearAll();
}

Manager::Manager(not_null<Window::Notifications::System*> system)
: NativeManager(system)
, _private(std::make_unique<Private>(this)) {
}

void Manager::clearNotification(NotificationId id) {
	_private->clearNotification(id);
}

Manager::~Manager() = default;

void Manager::doShowNativeNotification(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		Ui::PeerUserpicView &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		DisplayOptions options) {
	_private->showNotification(
		peer,
		topicRootId,
		userpicView,
		msgId,
		title,
		subtitle,
		msg,
		options);
}

void Manager::doClearAllFast() {
	_private->clearAll();
}

void Manager::doClearFromItem(not_null<HistoryItem*> item) {
	_private->clearFromItem(item);
}

void Manager::doClearFromTopic(not_null<Data::ForumTopic*> topic) {
	_private->clearFromTopic(topic);
}

void Manager::doClearFromHistory(not_null<History*> history) {
	_private->clearFromHistory(history);
}

void Manager::doClearFromSession(not_null<Main::Session*> session) {
	_private->clearFromSession(session);
}

bool Manager::doSkipToast() const {
	return false;
}

void Manager::doMaybePlaySound(Fn<void()> playSound) {
	_private->invokeIfNotInhibited(std::move(playSound));
}

void Manager::doMaybeFlashBounce(Fn<void()> flashBounce) {
	_private->invokeIfNotInhibited(std::move(flashBounce));
}

} // namespace Notifications
} // namespace Platform
