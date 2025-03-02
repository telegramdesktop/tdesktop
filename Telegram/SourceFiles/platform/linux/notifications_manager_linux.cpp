
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
#include "media/audio/media_audio_local_cache.h"
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

} // namespace

class Manager::Private : public base::has_weak_ptr {
public:
	explicit Private(not_null<Manager*> manager);

	void init(XdgNotifications::NotificationsProxy proxy);

	void showNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView);
	void clearAll();
	void clearFromItem(not_null<HistoryItem*> item);
	void clearFromTopic(not_null<Data::ForumTopic*> topic);
	void clearFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
	void clearNotification(NotificationId id);
	void invokeIfNotInhibited(Fn<void()> callback);

	~Private();

private:
	struct NotificationData : public base::has_weak_ptr {
		uint id = 0;
	};
	using Notification = std::unique_ptr<NotificationData>;

	const not_null<Manager*> _manager;

	base::flat_map<
		ContextId,
		base::flat_map<MsgId,
			std::variant<Notification, std::string>>> _notifications;

	Gio::Application _application;
	XdgNotifications::NotificationsProxy _proxy;
	XdgNotifications::Notifications _interface;
	Media::Audio::LocalDiskCache _sounds;
	rpl::lifetime _lifetime;

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
	}, HasCapability) && ranges::any_of(std::array{
		// To not to play sound with Don't Disturb activated
		"sound",
		"inhibitions",
	}, HasCapability);
}

void Create(Window::Notifications::System *system) {
	static const auto ServiceWatcher = CreateServiceWatcher();

	const auto managerSetter = [=](
			XdgNotifications::NotificationsProxy proxy) {
		system->setManager([=] {
			auto manager = std::make_unique<Manager>(system);
			manager->_private->init(proxy);
			return manager;
		});
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
: _manager(manager)
, _application(UseGNotification()
		? Gio::Application::get_default()
		: nullptr)
, _sounds(cWorkingDir() + u"tdata/audio_cache"_q) {
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

	if (auto actionMap = Gio::ActionMap(_application)) {
		const auto dictToNotificationId = [](GLib::VariantDict dict) {
			return NotificationId{
				.contextId = ContextId{
					.sessionId = dict.lookup_value("session").get_uint64(),
					.peerId = PeerId(dict.lookup_value("peer").get_uint64()),
					.topicRootId = dict.lookup_value("topic").get_int64(),
				},
				.msgId = dict.lookup_value("msgid").get_int64(),
			};
		};

		auto activate = gi::wrap(
			G_SIMPLE_ACTION(
				actionMap.lookup_action("notification-activate").gobj_()),
			gi::transfer_none);

		const auto activateSig = activate.signal_activate().connect([=](
				Gio::SimpleAction,
				GLib::Variant parameter) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				_manager->notificationActivated(
					dictToNotificationId(GLib::VariantDict::new_(parameter)));
			});
		});

		_lifetime.add([=]() mutable {
			activate.disconnect(activateSig);
		});

		auto markAsRead = gi::wrap(
			G_SIMPLE_ACTION(
				actionMap.lookup_action("notification-mark-as-read").gobj_()),
			gi::transfer_none);

		const auto markAsReadSig = markAsRead.signal_activate().connect([=](
				Gio::SimpleAction,
				GLib::Variant parameter) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				_manager->notificationReplied(
					dictToNotificationId(GLib::VariantDict::new_(parameter)),
					{});
			});
		});

		_lifetime.add([=]() mutable {
			markAsRead.disconnect(markAsReadSig);
		});
	}
}

void Manager::Private::init(XdgNotifications::NotificationsProxy proxy) {
	_proxy = proxy;
	_interface = proxy;

	if (_application || !_interface) {
		return;
	}

	const auto actionInvoked = _interface.signal_action_invoked().connect([=](
			XdgNotifications::Notifications,
			uint id,
			std::string actionName) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			for (const auto &[key, notifications] : _notifications) {
				for (const auto &[msgId, notification] : notifications) {
					if (id == v::get<Notification>(notification)->id) {
						if (actionName == "default") {
							_manager->notificationActivated({ key, msgId });
						} else if (actionName == "mail-mark-read") {
							_manager->notificationReplied({ key, msgId }, {});
						}
						return;
					}
				}
			}
		});
	});

	_lifetime.add([=] {
		_interface.disconnect(actionInvoked);
	});

	const auto replied = _interface.signal_notification_replied().connect([=](
			XdgNotifications::Notifications,
			uint id,
			std::string text) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			for (const auto &[key, notifications] : _notifications) {
				for (const auto &[msgId, notification] : notifications) {
					if (id == v::get<Notification>(notification)->id) {
						_manager->notificationReplied(
							{ key, msgId },
							{ QString::fromStdString(text), {} });
						return;
					}
				}
			}
		});
	});

	_lifetime.add([=] {
		_interface.disconnect(replied);
	});

	const auto tokenSignal = _interface.signal_activation_token().connect([=](
			XdgNotifications::Notifications,
			uint id,
			std::string token) {
		for (const auto &[key, notifications] : _notifications) {
			for (const auto &[msgId, notification] : notifications) {
				if (id == v::get<Notification>(notification)->id) {
					GLib::setenv("XDG_ACTIVATION_TOKEN", token, true);
					return;
				}
			}
		}
	});

	_lifetime.add([=] {
		_interface.disconnect(tokenSignal);
	});

	const auto closed = _interface.signal_notification_closed().connect([=](
			XdgNotifications::Notifications,
			uint id,
			uint reason) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			for (const auto &[key, notifications] : _notifications) {
				for (const auto &[msgId, notification] : notifications) {
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
					if (id == v::get<Notification>(notification)->id
							&& reason == 2) {
						clearNotification({ key, msgId });
						return;
					}
				}
			}
		});
	});

	_lifetime.add([=] {
		_interface.disconnect(closed);
	});
}

void Manager::Private::showNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) {
	const auto peer = info.peer;
	const auto options = info.options;
	const auto key = ContextId{
		.sessionId = peer->session().uniqueId(),
		.peerId = peer->id,
		.topicRootId = info.topicRootId,
	};
	const auto notificationId = NotificationId{
		.contextId = key,
		.msgId = info.itemId,
	};
	auto notification = _application
		? std::variant<Notification, Gio::Notification>(
			Gio::Notification::new_(
				info.subtitle.isEmpty()
					? info.title.toStdString()
					: info.subtitle.toStdString()
						+ " (" + info.title.toStdString() + ')'))
		: std::variant<Notification, Gio::Notification>(
			std::make_unique<NotificationData>());

	std::vector<gi::cstring> actions;
	auto hints = GLib::VariantDict::new_();
	v::match(notification, [&](Gio::Notification &notification) {
		notification.set_body(info.message.toStdString());

		notification.set_icon(
			Gio::ThemedIcon::new_(base::IconName().toStdString()));

		// for chat messages, according to
		// https://docs.gtk.org/gio/enum.NotificationPriority.html
		notification.set_priority(Gio::NotificationPriority::HIGH_);

		// glib 2.70+, we keep glib 2.56+ compatibility
		static const auto set_category = [] {
			// reset dlerror after dlsym call
			const auto guard = gsl::finally([] { dlerror(); });
			return reinterpret_cast<void(*)(GNotification*, const gchar*)>(
				dlsym(RTLD_DEFAULT, "g_notification_set_category"));
		}();

		if (set_category) {
			set_category(notification.gobj_(), "im.received");
		}

		const auto notificationVariant = GLib::Variant::new_array({
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("session"),
				GLib::Variant::new_variant(
					GLib::Variant::new_uint64(peer->session().uniqueId()))),
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("peer"),
				GLib::Variant::new_variant(
					GLib::Variant::new_uint64(peer->id.value))),
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("peer"),
				GLib::Variant::new_variant(
					GLib::Variant::new_uint64(peer->id.value))),
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("topic"),
				GLib::Variant::new_variant(
					GLib::Variant::new_int64(info.topicRootId.bare))),
			GLib::Variant::new_dict_entry(
				GLib::Variant::new_string("msgid"),
				GLib::Variant::new_variant(
					GLib::Variant::new_int64(info.itemId.bare))),
		});

		notification.set_default_action_and_target(
			"app.notification-activate",
			notificationVariant);

		if (!options.hideMarkAsRead) {
			notification.add_button_with_target(
				tr::lng_context_mark_read(tr::now).toStdString(),
				"app.notification-mark-as-read",
				notificationVariant);
		}
	}, [&](const Notification &notification) {
		if (HasCapability("actions")) {
			actions.push_back("default");
			actions.push_back(tr::lng_open_link(tr::now).toStdString());

			if (!options.hideMarkAsRead) {
				// icon name according to https://specifications.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html
				actions.push_back("mail-mark-read");
				actions.push_back(
					tr::lng_context_mark_read(tr::now).toStdString());
			}

			if (HasCapability("inline-reply")
					&& !options.hideReplyButton) {
				actions.push_back("inline-reply");
				actions.push_back(
					tr::lng_notification_reply(tr::now).toStdString());
			}

			actions.push_back({});
		}

		if (HasCapability("action-icons")) {
			hints.insert_value(
				"action-icons",
				GLib::Variant::new_boolean(true));
		}

		if (HasCapability("sound")) {
			const auto sound = info.sound
				? info.sound()
				: Media::Audio::LocalSound();

			const auto path = sound
				? _sounds.path(sound).toStdString()
				: std::string();

			if (!path.empty()) {
				hints.insert_value(
					"sound-file",
					GLib::Variant::new_string(path));
			} else {
				hints.insert_value(
					"suppress-sound",
					GLib::Variant::new_boolean(true));
			}
		}

		if (HasCapability("x-canonical-append")) {
			hints.insert_value(
				"x-canonical-append",
				GLib::Variant::new_string("true"));
		}

		hints.insert_value(
			"category",
			GLib::Variant::new_string("im.received"));

		hints.insert_value("desktop-entry", GLib::Variant::new_string(
			QGuiApplication::desktopFileName().toStdString()));
	});

	const auto imageKey = GetImageKey();
	if (!options.hideNameAndPhoto) {
		v::match(notification, [&](Gio::Notification &notification) {
			QByteArray imageData;
			QBuffer buffer(&imageData);
			buffer.open(QIODevice::WriteOnly);
			Window::Notifications::GenerateUserpic(peer, userpicView).save(
				&buffer,
				"PNG");

			notification.set_icon(
				Gio::BytesIcon::new_(
					GLib::Bytes::new_with_free_func(
						reinterpret_cast<const uchar*>(imageData.constData()),
						imageData.size(),
						[imageData] {})));
		}, [&](const Notification &notification) {
			if (imageKey.empty()) {
				return;
			}

			const auto image = Window::Notifications::GenerateUserpic(
				peer,
				userpicView
			).convertToFormat(QImage::Format_RGBA8888);

			hints.insert_value(imageKey, GLib::Variant::new_tuple({
				GLib::Variant::new_int32(image.width()),
				GLib::Variant::new_int32(image.height()),
				GLib::Variant::new_int32(image.bytesPerLine()),
				GLib::Variant::new_boolean(true),
				GLib::Variant::new_int32(8),
				GLib::Variant::new_int32(4),
				GLib::Variant::new_from_data(
					GLib::VariantType::new_("ay"),
					reinterpret_cast<const uchar*>(image.constBits()),
					image.sizeInBytes(),
					true,
					[image] {}),
			}));
		});
	}

	auto i = _notifications.find(key);
	if (i != end(_notifications)) {
		auto j = i->second.find(info.itemId);
		if (j != end(i->second)) {
			auto oldNotification = std::move(j->second);
			i->second.erase(j);
			v::match(oldNotification, [&](
					const std::string &oldNotification) {
				_application.withdraw_notification(oldNotification);
			}, [&](const Notification &oldNotification) {
				_interface.call_close_notification(
					oldNotification->id,
					nullptr);
			});
		}
	} else {
		i = _notifications.emplace(key).first;
	}
	v::match(notification, [&](Gio::Notification &notification) {
		const auto j = i->second.emplace(
			info.itemId,
			Gio::dbus_generate_guid()).first;
		_application.send_notification(
			v::get<std::string>(j->second),
			notification);
	}, [&](Notification &notification) {
		const auto j = i->second.emplace(
			info.itemId,
			std::move(notification)).first;

		const auto weak = base::make_weak(
			v::get<Notification>(j->second).get());

		// work around snap's activation restriction
		StartServiceAsync(
			_proxy.get_connection(),
			crl::guard(weak, [=]() mutable {
				const auto hasBodyMarkup = HasCapability("body-markup");

				const auto callbackWrap = gi::unwrap(
					Gio::AsyncReadyCallback(
						crl::guard(this, [=](
								GObject::Object,
								Gio::AsyncResult res) {
							auto &sandbox = Core::Sandbox::Instance();
							sandbox.customEnterFromEventLoop([&] {
								const auto result
									= _interface.call_notify_finish(res);

								if (!result) {
									Gio::DBusErrorNS_::strip_remote_error(
										result.error());
									LOG(("Native Notification Error: %1").arg(
										result.error().message_().c_str()));
									clearNotification(notificationId);
									return;
								}

								if (!weak) {
									_interface.call_close_notification(
										std::get<1>(*result),
										nullptr);
									return;
								}

								weak->id = std::get<1>(*result);
							});
						})),
					gi::scope_async);

				xdg_notifications_notifications_call_notify(
					_interface.gobj_(),
					AppName.data(),
					0,
					(imageKey.empty() || !hints.lookup_value(imageKey)
							? base::IconName().toStdString()
							: std::string()).c_str(),
					(hasBodyMarkup || info.subtitle.isEmpty()
						? info.title.toStdString()
						: info.subtitle.toStdString()
							+ " (" + info.title.toStdString() + ')').c_str(),
					(hasBodyMarkup
						? info.subtitle.isEmpty()
							? info.message.toHtmlEscaped().toStdString()
							: u"<b>%1</b>\n%2"_q.arg(
								info.subtitle.toHtmlEscaped(),
								info.message.toHtmlEscaped()).toStdString()
						: info.message.toStdString()).c_str(),
					!actions.empty()
						? (actions
							| ranges::views::transform(&gi::cstring::c_str)
							| ranges::to_vector).data()
						: nullptr,
					hints.end().gobj_(),
					-1,
					nullptr,
					&callbackWrap->wrapper,
					callbackWrap);
			}));
	});
}

void Manager::Private::clearAll() {
	for (const auto &[key, notifications] : base::take(_notifications)) {
		for (const auto &[msgId, notification] : notifications) {
			v::match(notification, [&](const std::string &notification) {
				_application.withdraw_notification(notification);
			}, [&](const Notification &notification) {
				_interface.call_close_notification(notification->id, nullptr);
			});
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
	v::match(taken, [&](const std::string &taken) {
		_application.withdraw_notification(taken);
	}, [&](const Notification &taken) {
		_interface.call_close_notification(taken->id, nullptr);
	});
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
			v::match(notification, [&](const std::string &notification) {
				_application.withdraw_notification(notification);
			}, [&](const Notification &notification) {
				_interface.call_close_notification(notification->id, nullptr);
			});
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
			v::match(notification, [&](const std::string &notification) {
				_application.withdraw_notification(notification);
			}, [&](const Notification &notification) {
				_interface.call_close_notification(notification->id, nullptr);
			});
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
			v::match(notification, [&](const std::string &notification) {
				_application.withdraw_notification(notification);
			}, [&](const Notification &notification) {
				_interface.call_close_notification(notification->id, nullptr);
			});
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

Manager::~Manager() = default;

void Manager::doShowNativeNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) {
	_private->showNotification(std::move(info), userpicView);
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
	if (UseGNotification()
		|| !HasCapability("sound")
		|| !Core::App().settings().desktopNotify()) {
		_private->invokeIfNotInhibited(std::move(playSound));
	}
}

void Manager::doMaybeFlashBounce(Fn<void()> flashBounce) {
	_private->invokeIfNotInhibited(std::move(flashBounce));
}

} // namespace Notifications
} // namespace Platform
