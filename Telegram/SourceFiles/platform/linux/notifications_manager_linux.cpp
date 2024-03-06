
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
#include "ui/empty_userpic.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "base/weak_ptr.h"
#include "window/notifications_utilities.h"
#include "styles/style_window.h"

#include <QtCore/QBuffer>
#include <QtCore/QVersionNumber>
#include <QtGui/QGuiApplication>

#include <glibmm.h>
#include <giomm.h>

#include <dlfcn.h>

namespace Platform {
namespace Notifications {
namespace {

constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kObjectPath = "/org/freedesktop/Notifications";
constexpr auto kInterface = kService;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

using PropertyMap = std::map<Glib::ustring, Glib::VariantBase>;

struct ServerInformation {
	Glib::ustring name;
	Glib::ustring vendor;
	QVersionNumber version;
	QVersionNumber specVersion;
};

bool ServiceRegistered = false;
ServerInformation CurrentServerInformation;
std::vector<Glib::ustring> CurrentCapabilities;

[[nodiscard]] bool HasCapability(const char *value) {
	return ranges::contains(CurrentCapabilities, value, &Glib::ustring::raw);
}

void Noexcept(Fn<void()> callback, Fn<void()> failed = nullptr) noexcept {
	try {
		callback();
		return;
	} catch (const std::exception &e) {
		LOG(("Native Notification Error: %1").arg(e.what()));
	}

	if (failed) {
		failed();
	}
}

std::unique_ptr<base::Platform::DBus::ServiceWatcher> CreateServiceWatcher() {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);

		const auto activatable = [&] {
			const auto names = base::Platform::DBus::ListActivatableNames(
				connection->gobj());

			if (!names) {
				// avoid service restart loop in sandboxed environments
				return true;
			}

			return ranges::contains(*names, kService);
		}();

		return std::make_unique<base::Platform::DBus::ServiceWatcher>(
			connection->gobj(),
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
	} catch (...) {
	}

	return nullptr;
}

void StartServiceAsync(Fn<void()> callback) {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);

		namespace DBus = base::Platform::DBus;
		DBus::StartServiceByNameAsync(
			connection->gobj(),
			kService,
			[=](Fn<DBus::Result<DBus::StartReply>()> result) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					Noexcept([&] {
						// get the error if any
						if (const auto ret = result(); !ret) {
							static const auto NotSupportedErrors = {
								"org.freedesktop.DBus.Error.ServiceUnknown",
							};

							if (ranges::none_of(
									NotSupportedErrors,
									[&](const auto &error) {
										return strstr(
											ret.error()->what(),
											error);
									})) {
								throw std::runtime_error(
									ret.error()->what());
							}
						}
					});

					callback();
				});
			});

			return;
	} catch (...) {
	}

	callback();
}

bool GetServiceRegistered() {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);

		const auto hasOwner = base::Platform::DBus::NameHasOwner(
				connection->gobj(),
				kService
		).value_or(false);

		static const auto activatable = [&] {
			const auto names = base::Platform::DBus::ListActivatableNames(
				connection->gobj());

			if (!names) {
				return false;
			}

			return ranges::contains(*names, kService);
		}();

		return hasOwner || activatable;
	} catch (...) {
	}

	return false;
}

void GetServerInformation(Fn<void(const ServerInformation &)> callback) {
	Noexcept([&] {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);

		connection->call(
			kObjectPath,
			kInterface,
			"GetServerInformation",
			{},
			[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					Noexcept([&] {
						const auto reply = connection->call_finish(result);

						const auto name = reply
							.get_child(0)
							.get_dynamic<Glib::ustring>();

						const auto vendor = reply
							.get_child(1)
							.get_dynamic<Glib::ustring>();

						const auto version = reply
							.get_child(2)
							.get_dynamic<Glib::ustring>();

						const auto specVersion = reply
							.get_child(3)
							.get_dynamic<Glib::ustring>();

						callback(ServerInformation{
							name,
							vendor,
							QVersionNumber::fromString(
								QString::fromStdString(version)),
							QVersionNumber::fromString(
								QString::fromStdString(specVersion)),
						});
					}, [&] {
						callback({});
					});
				});
			},
			kService);
	}, [&] {
		callback({});
	});
}

void GetCapabilities(Fn<void(const std::vector<Glib::ustring> &)> callback) {
	Noexcept([&] {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);

		connection->call(
			kObjectPath,
			kInterface,
			"GetCapabilities",
			{},
			[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					Noexcept([&] {
						callback(
							connection->call_finish(result)
								.get_child(0)
								.get_dynamic<std::vector<Glib::ustring>>()
						);
					}, [&] {
						callback({});
					});
				});
			},
			kService);
	}, [&] {
		callback({});
	});
}

void GetInhibited(Fn<void(bool)> callback) {
	Noexcept([&] {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);

		connection->call(
			kObjectPath,
			kPropertiesInterface,
			"Get",
			Glib::create_variant(std::tuple{
				Glib::ustring(kInterface),
				Glib::ustring("Inhibited"),
			}),
			[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					Noexcept([&] {
						callback(
							connection->call_finish(result)
								.get_child(0)
								.get_dynamic<Glib::Variant<bool>>()
								.get()
						);
					}, [&] {
						callback(false);
					});
				});
			},
			kService);
	}, [&] {
		callback(false);
	});
}

Glib::ustring GetImageKey(const QVersionNumber &specificationVersion) {
	const auto normalizedVersion = specificationVersion.normalized();

	if (normalizedVersion.isNull()) {
		LOG(("Native Notification Error: specification version is null"));
		return {};
	}

	if (normalizedVersion >= QVersionNumber(1, 2)) {
		return "image-data";
	} else if (normalizedVersion == QVersionNumber(1, 1)) {
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

class NotificationData final : public base::has_weak_ptr {
public:
	using NotificationId = Window::Notifications::Manager::NotificationId;

	NotificationData(
		not_null<Manager*> manager,
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

	Glib::RefPtr<Gio::Application> _application;
	Glib::RefPtr<Gio::Notification> _notification;
	const std::string _guid;

	Glib::RefPtr<Gio::DBus::Connection> _dbusConnection;
	Glib::ustring _title;
	Glib::ustring _body;
	std::vector<Glib::ustring> _actions;
	std::map<Glib::ustring, Glib::VariantBase> _hints;
	Glib::ustring _imageKey;

	uint _notificationId = 0;
	uint _actionInvokedSignalId = 0;
	uint _activationTokenSignalId = 0;
	uint _notificationRepliedSignalId = 0;
	uint _notificationClosedSignalId = 0;

	void notificationClosed(uint id, uint reason);
	void actionInvoked(uint id, const Glib::ustring &actionName);
	void activationToken(uint id, const Glib::ustring &token);
	void notificationReplied(uint id, const Glib::ustring &text);

};

using Notification = std::unique_ptr<NotificationData>;

NotificationData::NotificationData(
	not_null<Manager*> manager,
	NotificationId id)
: _manager(manager)
, _id(id)
, _application(UseGNotification()
		? Gio::Application::get_default()
		: nullptr)
, _guid(_application ? Gio::DBus::generate_guid() : std::string()) {
}

bool NotificationData::init(
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		Window::Notifications::Manager::DisplayOptions options) {
	if (_application) {
		_notification = Gio::Notification::create(
			subtitle.isEmpty()
				? title.toStdString()
				: subtitle.toStdString() + " (" + title.toStdString() + ')');

		_notification->set_body(msg.toStdString());

		_notification->set_icon(
			Gio::ThemedIcon::create(base::IconName().toStdString()));

		// for chat messages, according to
		// https://docs.gtk.org/gio/enum.NotificationPriority.html
		_notification->set_priority(Gio::Notification::Priority::HIGH);

		// glib 2.70+, we keep glib 2.56+ compatibility
		static const auto set_category = [] {
			// reset dlerror after dlsym call
			const auto guard = gsl::finally([] { dlerror(); });
			return reinterpret_cast<void(*)(GNotification*, const gchar*)>(
				dlsym(RTLD_DEFAULT, "g_notification_set_category"));
		}();

		if (set_category) {
			set_category(_notification->gobj(), "im.received");
		}

		const auto idTuple = _id.toTuple();

		_notification->set_default_action(
			"app.notification-activate",
			idTuple);

		if (!options.hideMarkAsRead) {
			_notification->add_button(
				tr::lng_context_mark_read(tr::now).toStdString(),
				"app.notification-mark-as-read",
				idTuple);
		}

		return true;
	}

	Noexcept([&] {
		_dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::SESSION);
	});

	if (!_dbusConnection) {
		return false;
	}

	const auto weak = base::make_weak(this);

	const auto signalEmitted = crl::guard(weak, [=](
			const Glib::RefPtr<Gio::DBus::Connection> &connection,
			const Glib::ustring &sender_name,
			const Glib::ustring &object_path,
			const Glib::ustring &interface_name,
			const Glib::ustring &signal_name,
			const Glib::VariantContainerBase &parameters) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			Noexcept([&] {
				if (signal_name == "ActionInvoked") {
					const auto id = parameters
						.get_child(0)
						.get_dynamic<uint>();

					const auto actionName = parameters
						.get_child(1)
						.get_dynamic<Glib::ustring>();

					actionInvoked(id, actionName);
				} else if (signal_name == "ActivationToken") {
					const auto id = parameters
						.get_child(0)
						.get_dynamic<uint>();

					const auto token = parameters
						.get_child(1)
						.get_dynamic<Glib::ustring>();

					activationToken(id, token);
				} else if (signal_name == "NotificationReplied") {
					const auto id = parameters
						.get_child(0)
						.get_dynamic<uint>();

					const auto text = parameters
						.get_child(1)
						.get_dynamic<Glib::ustring>();

					notificationReplied(id, text);
				} else if (signal_name == "NotificationClosed") {
					const auto id = parameters
						.get_child(0)
						.get_dynamic<uint>();

					const auto reason = parameters
						.get_child(1)
						.get_dynamic<uint>();

					notificationClosed(id, reason);
				}
			});
		});
	});

	_imageKey = GetImageKey(CurrentServerInformation.specVersion);

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

			_notificationRepliedSignalId =
				_dbusConnection->signal_subscribe(
					signalEmitted,
					kService,
					kInterface,
					"NotificationReplied",
					kObjectPath);
		}

		_actionInvokedSignalId = _dbusConnection->signal_subscribe(
			signalEmitted,
			kService,
			kInterface,
			"ActionInvoked",
			kObjectPath);

		_activationTokenSignalId = _dbusConnection->signal_subscribe(
			signalEmitted,
			kService,
			kInterface,
			"ActivationToken",
			kObjectPath);
	}

	if (HasCapability("action-icons")) {
		_hints["action-icons"] = Glib::create_variant(true);
	}

	// suppress system sound if telegram sound activated,
	// otherwise use system sound
	if (HasCapability("sound")) {
		if (Core::App().settings().soundNotify()) {
			_hints["suppress-sound"] = Glib::create_variant(true);
		} else {
			// sound name according to http://0pointer.de/public/sound-naming-spec.html
			_hints["sound-name"] = Glib::create_variant(
				Glib::ustring("message-new-instant"));
		}
	}

	if (HasCapability("x-canonical-append")) {
		_hints["x-canonical-append"] = Glib::create_variant(
			Glib::ustring("true"));
	}

	_hints["category"] = Glib::create_variant(Glib::ustring("im.received"));

	_hints["desktop-entry"] = Glib::create_variant(
		Glib::ustring(QGuiApplication::desktopFileName().toStdString()));

	_notificationClosedSignalId = _dbusConnection->signal_subscribe(
		signalEmitted,
		kService,
		kInterface,
		"NotificationClosed",
		kObjectPath);
	return true;
}

NotificationData::~NotificationData() {
	if (_dbusConnection) {
		if (_actionInvokedSignalId != 0) {
			_dbusConnection->signal_unsubscribe(_actionInvokedSignalId);
		}

		if (_activationTokenSignalId != 0) {
			_dbusConnection->signal_unsubscribe(_activationTokenSignalId);
		}

		if (_notificationRepliedSignalId != 0) {
			_dbusConnection->signal_unsubscribe(_notificationRepliedSignalId);
		}

		if (_notificationClosedSignalId != 0) {
			_dbusConnection->signal_unsubscribe(_notificationClosedSignalId);
		}
	}
}

void NotificationData::show() {
	if (_application && _notification) {
		_application->send_notification(_guid, _notification);
		return;
	}

	// a hack for snap's activation restriction
	const auto weak = base::make_weak(this);
	StartServiceAsync(crl::guard(weak, [=] {
		const auto iconName = _imageKey.empty()
			|| _hints.find(_imageKey) == end(_hints)
				? Glib::ustring(base::IconName().toStdString())
				: Glib::ustring();
		const auto connection = _dbusConnection;

		connection->call(
			kObjectPath,
			kInterface,
			"Notify",
			Glib::create_variant(std::tuple{
				Glib::ustring(std::string(AppName)),
				uint(0),
				iconName,
				_title,
				_body,
				_actions,
				_hints,
				-1,
			}),
			crl::guard(weak, [=](
					const Glib::RefPtr<Gio::AsyncResult> &result) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					Noexcept([&] {
						_notificationId = connection->call_finish(result)
							.get_child(0)
							.get_dynamic<uint>();
					}, [&] {
						_manager->clearNotification(_id);
					});
				});
			}),
			kService);
	}));
}

void NotificationData::close() {
	if (_application) {
		_application->withdraw_notification(_guid);
		_manager->clearNotification(_id);
		return;
	}

	_dbusConnection->call(
		kObjectPath,
		kInterface,
		"CloseNotification",
		Glib::create_variant(std::tuple{
			_notificationId,
		}),
		{},
		kService,
		-1,
		Gio::DBus::CallFlags::NO_AUTO_START);
	_manager->clearNotification(_id);
}

void NotificationData::setImage(QImage image) {
	if (_notification) {
		const auto imageData = [&] {
			QByteArray ba;
			QBuffer buffer(&ba);
			buffer.open(QIODevice::WriteOnly);
			image.save(&buffer, "PNG");
			return ba;
		}();

		_notification->set_icon(
			Gio::BytesIcon::create(
				Glib::Bytes::create(
					imageData.constData(),
					imageData.size())));

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

	_hints[_imageKey] = Glib::create_variant(std::tuple{
		image.width(),
		image.height(),
		int(image.bytesPerLine()),
		image.hasAlphaChannel(),
		8,
		image.hasAlphaChannel() ? 4 : 3,
		std::vector<uchar>(
			image.constBits(),
			image.constBits() + image.sizeInBytes()),
	});
}

void NotificationData::notificationClosed(uint id, uint reason) {
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
}

void NotificationData::actionInvoked(
		uint id,
		const Glib::ustring &actionName) {
	if (id != _notificationId) {
		return;
	}

	if (actionName == "default") {
		_manager->notificationActivated(_id);
	} else if (actionName == "mail-mark-read") {
		_manager->notificationReplied(_id, {});
	}
}

void NotificationData::activationToken(uint id, const Glib::ustring &token) {
	if (id == _notificationId) {
		qputenv("XDG_ACTIVATION_TOKEN", QByteArray::fromStdString(token));
	}
}

void NotificationData::notificationReplied(
		uint id,
		const Glib::ustring &text) {
	if (id == _notificationId) {
		_manager->notificationReplied(
			_id,
			{ QString::fromStdString(text), {} });
	}
}

} // namespace

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
	}, [](const auto *capability) {
		return HasCapability(capability);
	});
}

void Create(Window::Notifications::System *system) {
	static const auto ServiceWatcher = CreateServiceWatcher();

	const auto managerSetter = [=] {
		using ManagerType = Window::Notifications::ManagerType;
		if ((Core::App().settings().nativeNotifications() || Enforced())
			&& Supported()) {
			if (system->manager().type() != ManagerType::Native) {
				system->setManager(std::make_unique<Manager>(system));
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
	const auto oneReady = [=] {
		if (!--*counter) {
			managerSetter();
		}
	};

	// snap doesn't allow access when the daemon is not running :(
	StartServiceAsync([=] {
		ServiceRegistered = GetServiceRegistered();

		if (!ServiceRegistered) {
			CurrentServerInformation = {};
			CurrentCapabilities = {};
			managerSetter();
			return;
		}

		GetServerInformation([=](const ServerInformation &result) {
			CurrentServerInformation = result;
			oneReady();
		});

		GetCapabilities([=](const std::vector<Glib::ustring> &result) {
			CurrentCapabilities = result;
			oneReady();
		});
	});
}

class Manager::Private : public base::has_weak_ptr {
public:
	explicit Private(not_null<Manager*> manager);

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

	Glib::RefPtr<Gio::DBus::Connection> _dbusConnection;
	bool _inhibited = false;
	uint _inhibitedSignalId = 0;

};

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
				[](const Glib::ustring &a, const Glib::ustring &b) {
					return a + (a.empty() ? "" : ", ") + b;
				}).c_str()));
	}

	if (HasCapability("inhibitions")) {
		Noexcept([&] {
			_dbusConnection = Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::SESSION);
		});

		if (!_dbusConnection) {
			return;
		}

		const auto weak = base::make_weak(this);
		GetInhibited(crl::guard(weak, [=](bool result) {
			_inhibited = result;
		}));

		_inhibitedSignalId = _dbusConnection->signal_subscribe(
			crl::guard(weak, [=](
					const Glib::RefPtr<Gio::DBus::Connection> &connection,
					const Glib::ustring &sender_name,
					const Glib::ustring &object_path,
					const Glib::ustring &interface_name,
					const Glib::ustring &signal_name,
					const Glib::VariantContainerBase &parameters) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					Noexcept([&] {
						const auto interface = parameters
							.get_child(0)
							.get_dynamic<Glib::ustring>();

						if (interface != kInterface) {
							return;
						}

						_inhibited = parameters
							.get_child(1)
							.get_dynamic<PropertyMap>()
							.at("Inhibited")
							.get_dynamic<bool>();
					});
				});
			}),
			kService,
			kPropertiesInterface,
			"PropertiesChanged",
			kObjectPath);
	}
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
	if (!_inhibited) {
		callback();
	}
}

Manager::Private::~Private() {
	clearAll();

	if (_dbusConnection) {
		if (_inhibitedSignalId != 0) {
			_dbusConnection->signal_unsubscribe(_inhibitedSignalId);
		}
	}
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
