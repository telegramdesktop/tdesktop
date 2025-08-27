
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
#include "platform/platform_specific.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_sublist.h"
#include "data/data_peer.h"
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

#include <ksandbox.h>

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
	void clearFromSublist(not_null<Data::SavedSublist*> sublist);
	void clearFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
	void clearNotification(NotificationId id);
	void invokeIfNotInhibited(Fn<void()> callback);

private:
	struct NotificationData : public base::has_weak_ptr {
		std::variant<v::null_t, uint, std::string> id;
		rpl::lifetime lifetime;
	};
	using Notification = std::unique_ptr<NotificationData>;

	const not_null<Manager*> _manager;
	Gio::Application _application;
	XdgNotifications::NotificationsProxy _proxy;
	XdgNotifications::Notifications _interface;
	Media::Audio::LocalDiskCache _sounds;
	base::flat_map<
		ContextId,
		base::flat_map<MsgId, Notification>> _notifications;
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

bool VolumeSupported() {
	return UseGNotification() || !HasCapability("sound");
}

void Create(Window::Notifications::System *system) {
	static const auto ServiceWatcher = CreateServiceWatcher();

	const auto managerSetter = [=](
			XdgNotifications::NotificationsProxy proxy) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			system->setManager([=] {
				auto manager = std::make_unique<Manager>(system);
				manager->_private->init(proxy);
				return manager;
			});
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

			ServiceRegistered = proxy ? bool(proxy.get_name_owner()) : false;
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
					.topicRootId = MsgId(
						dict.lookup_value("topic").get_int64()),
					.monoforumPeerId = PeerId(dict.lookup_value(
						"monoforumpeer").get_uint64()),
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
					const auto &nid = notification->id;
					if (v::is<uint>(nid) && v::get<uint>(nid) == id) {
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
					const auto &nid = notification->id;
					if (v::is<uint>(nid) && v::get<uint>(nid) == id) {
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
				const auto &nid = notification->id;
				if (v::is<uint>(nid) && v::get<uint>(nid) == id) {
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
					const auto &nid = notification->id;
					if (v::is<uint>(nid) && v::get<uint>(nid) == id && reason == 2) {
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
		.monoforumPeerId = info.monoforumPeerId,
	};
	const auto notificationId = NotificationId{
		.contextId = key,
		.msgId = info.itemId,
	};
	auto notification = _application
		? Gio::Notification::new_(info.title.toStdString())
		: Gio::Notification();

	std::vector<gi::cstring> actions;
	auto hints = GLib::VariantDict::new_();
	if (notification) {
		notification.set_body(info.subtitle.isEmpty()
			? info.message.toStdString()
			: tr::lng_dialogs_text_with_from(
				tr::now,
				lt_from_part,
				tr::lng_dialogs_text_from_wrapped(
					tr::now,
					lt_from,
					info.subtitle),
				lt_message,
				info.message).toStdString());

		notification.set_icon(
			Gio::ThemedIcon::new_(ApplicationIconName().toStdString()));

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
				GLib::Variant::new_string("monoforumpeer"),
				GLib::Variant::new_variant(
					GLib::Variant::new_uint64(info.monoforumPeerId.value))),
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
	} else {
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
		}

		actions.push_back({});

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
	}

	const auto imageKey = GetImageKey();
	if (!options.hideNameAndPhoto) {
		if (notification) {
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
		} else if (!imageKey.empty()) {
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
		}
	}

	const auto &data
		= _notifications[key][info.itemId]
			= std::make_unique<NotificationData>();
	data->lifetime.add([=, notification = data.get()] {
		v::match(notification->id, [&](const std::string &id) {
			_application.withdraw_notification(id);
		}, [&](uint id) {
			_interface.call_close_notification(id, nullptr);
		}, [](v::null_t) {});
	});

	if (notification) {
		const auto id = Gio::dbus_generate_guid();
		data->id = id;
		_application.send_notification(id, notification);
	} else {
		// work around snap's activation restriction
		const auto weak = base::make_weak(data);
		StartServiceAsync(
			_proxy.get_connection(),
			crl::guard(weak, [=]() mutable {
				const auto hasImage = !imageKey.empty()
					&& hints.lookup_value(imageKey);

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
					(!hasImage
						? ApplicationIconName().toStdString()
						: std::string()).c_str(),
					info.title.toStdString().c_str(),
					(HasCapability("body-markup")
						? info.subtitle.isEmpty()
							? info.message.toHtmlEscaped().toStdString()
							: u"<b>%1</b>\n%2"_q.arg(
								info.subtitle.toHtmlEscaped(),
								info.message.toHtmlEscaped()).toStdString()
						: info.subtitle.isEmpty()
							? info.message.toStdString()
							: tr::lng_dialogs_text_with_from(
								tr::now,
								lt_from_part,
								tr::lng_dialogs_text_from_wrapped(
									tr::now,
									lt_from,
									info.subtitle),
								lt_message,
								info.message).toStdString()).c_str(),
					(actions
						| ranges::views::transform(&gi::cstring::c_str)
						| ranges::to_vector).data(),
					hints.end().gobj_(),
					-1,
					nullptr,
					&callbackWrap->wrapper,
					callbackWrap);
			}));
	}
}

void Manager::Private::clearAll() {
	_notifications.clear();
}

void Manager::Private::clearFromItem(not_null<HistoryItem*> item) {
	const auto i = _notifications.find(ContextId{
		.sessionId = item->history()->session().uniqueId(),
		.peerId = item->history()->peer->id,
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	});
	if (i != _notifications.cend()
			&& i->second.remove(item->id)
			&& i->second.empty()) {
		_notifications.erase(i);
	}
}

void Manager::Private::clearFromTopic(not_null<Data::ForumTopic*> topic) {
	_notifications.remove(ContextId{
		.sessionId = topic->session().uniqueId(),
		.peerId = topic->history()->peer->id,
		.topicRootId = topic->rootId(),
	});
}

void Manager::Private::clearFromSublist(
		not_null<Data::SavedSublist*> sublist) {
	_notifications.remove(ContextId{
		.sessionId = sublist->session().uniqueId(),
		.peerId = sublist->owningHistory()->peer->id,
		.monoforumPeerId = sublist->sublistPeer()->id,
	});
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
		i = _notifications.erase(i);
	}
}

void Manager::Private::clearFromSession(not_null<Main::Session*> session) {
	const auto sessionId = session->uniqueId();
	auto i = _notifications.lower_bound(ContextId{
		.sessionId = sessionId,
	});
	while (i != _notifications.cend() && i->first.sessionId == sessionId) {
		i = _notifications.erase(i);
	}
}

void Manager::Private::clearNotification(NotificationId id) {
	auto i = _notifications.find(id.contextId);
	if (i != _notifications.cend()
			&& i->second.remove(id.msgId)
			&& i->second.empty()) {
		_notifications.erase(i);
	}
}

void Manager::Private::invokeIfNotInhibited(Fn<void()> callback) {
	if (!_interface.get_inhibited()) {
		callback();
	}
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

void Manager::doClearFromSublist(not_null<Data::SavedSublist*> sublist) {
	_private->clearFromSublist(sublist);
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
