
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/notifications_manager_linux.h"

#include "window/notifications_utilities.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"
#include "platform/linux/specific_linux.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "history/history.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "base/weak_ptr.h"

#include <QtCore/QVersionNumber>
#include <QtGui/QGuiApplication>

#include <glibmm.h>
#include <giomm.h>

namespace Platform {
namespace Notifications {
namespace {

constexpr auto kService = "org.freedesktop.Notifications"_cs;
constexpr auto kObjectPath = "/org/freedesktop/Notifications"_cs;
constexpr auto kInterface = kService;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

using namespace base::Platform;

struct ServerInformation {
	QString name;
	QString vendor;
	QVersionNumber version;
	QVersionNumber specVersion;
};

bool ServiceRegistered = false;
bool InhibitionSupported = false;
std::optional<ServerInformation> CurrentServerInformation;
QStringList CurrentCapabilities;

std::unique_ptr<base::Platform::DBus::ServiceWatcher> CreateServiceWatcher() {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		const auto activatable = [&] {
			try {
				return ranges::contains(
					base::Platform::DBus::ListActivatableNames(connection),
					Glib::ustring(std::string(kService)));
			} catch (...) {
				// avoid service restart loop in sandboxed environments
				return true;
			}
		}();

		return std::make_unique<base::Platform::DBus::ServiceWatcher>(
			connection,
			std::string(kService),
			[=](
				const Glib::ustring &service,
				const Glib::ustring &oldOwner,
				const Glib::ustring &newOwner) {
				if (activatable && newOwner.empty()) {
					return;
				}

				crl::on_main([] {
					Core::App().notifications().createManager();
				});
			});
	} catch (...) {
	}

	return nullptr;
}

void StartServiceAsync(Fn<void()> callback) {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		DBus::StartServiceByNameAsync(
			connection,
			std::string(kService),
			[=](Fn<DBus::StartReply()> result) {
				try {
					result(); // get the error if any
				} catch (const Glib::Error &e) {
					static const auto NotSupportedErrors = {
						"org.freedesktop.DBus.Error.ServiceUnknown",
					};

					const auto errorName =
						Gio::DBus::ErrorUtils::get_remote_error(e);

					if (!ranges::contains(NotSupportedErrors, errorName)) {
						LOG(("Native Notification Error: %1").arg(
							QString::fromStdString(e.what())));
					}
				} catch (const std::exception &e) {
					LOG(("Native Notification Error: %1").arg(
						QString::fromStdString(e.what())));
				}

				crl::on_main(callback);
			});

			return;
	} catch (...) {
	}

	crl::on_main(callback);
}

bool GetServiceRegistered() {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		const auto hasOwner = [&] {
			try {
				return DBus::NameHasOwner(
					connection,
					std::string(kService));
			} catch (...) {
				return false;
			}
		}();

		static const auto activatable = [&] {
			try {
				return ranges::contains(
					DBus::ListActivatableNames(connection),
					Glib::ustring(std::string(kService)));
			} catch (...) {
				return false;
			}
		}();

		return hasOwner || activatable;
	} catch (...) {
	}

	return false;
}

void GetServerInformation(
		Fn<void(const std::optional<ServerInformation> &)> callback) {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		connection->call(
			std::string(kObjectPath),
			std::string(kInterface),
			"GetServerInformation",
			{},
			[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
				try {
					auto reply = connection->call_finish(result);

					const auto name = GlibVariantCast<Glib::ustring>(
						reply.get_child(0));

					const auto vendor = GlibVariantCast<Glib::ustring>(
						reply.get_child(1));

					const auto version = GlibVariantCast<Glib::ustring>(
						reply.get_child(2));

					const auto specVersion = GlibVariantCast<Glib::ustring>(
						reply.get_child(3));

					crl::on_main([=] {
						callback(ServerInformation{
							QString::fromStdString(name),
							QString::fromStdString(vendor),
							QVersionNumber::fromString(
								QString::fromStdString(version)),
							QVersionNumber::fromString(
								QString::fromStdString(specVersion)),
						});
					});

					return;
				} catch (const Glib::Error &e) {
					LOG(("Native Notification Error: %1").arg(
						QString::fromStdString(e.what())));
				} catch (const std::exception &e) {
					LOG(("Native Notification Error: %1").arg(
						QString::fromStdString(e.what())));
				}

				crl::on_main([=] { callback(std::nullopt); });
			},
			std::string(kService));

			return;
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	}

	crl::on_main([=] { callback(std::nullopt); });
}

void GetCapabilities(Fn<void(const QStringList &)> callback) {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		connection->call(
			std::string(kObjectPath),
			std::string(kInterface),
			"GetCapabilities",
			{},
			[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
				try {
					auto reply = connection->call_finish(result);

					QStringList value;
					ranges::transform(
						GlibVariantCast<std::vector<Glib::ustring>>(
							reply.get_child(0)),
						ranges::back_inserter(value),
						QString::fromStdString);

					crl::on_main([=] {
						callback(value);
					});

					return;
				} catch (const Glib::Error &e) {
					LOG(("Native Notification Error: %1").arg(
						QString::fromStdString(e.what())));
				} catch (const std::exception &e) {
					LOG(("Native Notification Error: %1").arg(
						QString::fromStdString(e.what())));
				}

				crl::on_main([=] { callback({}); });
			},
			std::string(kService));

			return;
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	}

	crl::on_main([=] { callback({}); });
}

void GetInhibitionSupported(Fn<void(bool)> callback) {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		connection->call(
			std::string(kObjectPath),
			std::string(kPropertiesInterface),
			"Get",
			MakeGlibVariant(std::tuple{
				Glib::ustring(std::string(kInterface)),
				Glib::ustring("Inhibited"),
			}),
			[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
				try {
					connection->call_finish(result);

					crl::on_main([=] {
						callback(true);
					});

					return;
				} catch (const Glib::Error &e) {
					static const auto DontLogErrors = {
						"org.freedesktop.DBus.Error.InvalidArgs",
						"org.freedesktop.DBus.Error.UnknownMethod",
					};

					const auto errorName = Gio::DBus::ErrorUtils::get_remote_error(e);
					if (!ranges::contains(DontLogErrors, errorName)) {
						LOG(("Native Notification Error: %1").arg(
							QString::fromStdString(e.what())));
					}
				}

				crl::on_main([=] { callback(false); });
			},
			std::string(kService));

			return;
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	}

	crl::on_main([=] { callback(false); });
}

bool Inhibited() {
	if (!Supported()
		|| !CurrentCapabilities.contains(qsl("inhibitions"))
		|| !InhibitionSupported) {
		return false;
	}

	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		// a hack for snap's activation restriction
		DBus::StartServiceByName(
			connection,
			std::string(kService));

		auto reply = connection->call_sync(
			std::string(kObjectPath),
			std::string(kPropertiesInterface),
			"Get",
			MakeGlibVariant(std::tuple{
				Glib::ustring(std::string(kInterface)),
				Glib::ustring("Inhibited"),
			}),
			std::string(kService));

		return GlibVariantCast<bool>(
			GlibVariantCast<Glib::VariantBase>(reply.get_child(0)));
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	} catch (const std::exception &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	}

	return false;
}

bool IsQualifiedDaemon() {
	// A list of capabilities that offer feature parity
	// with custom notifications
	static const auto NeededCapabilities = {
		// To show message content
		qsl("body"),
		// To make the sender name bold
		qsl("body-markup"),
		// To have buttons on notifications
		qsl("actions"),
		// To have quick reply
		qsl("inline-reply"),
		// To not to play sound with Don't Disturb activated
		// (no, using sound capability is not a way)
		qsl("inhibitions"),
	};

	return ranges::all_of(NeededCapabilities, [&](const auto &capability) {
		return CurrentCapabilities.contains(capability);
	}) && InhibitionSupported;
}

ServerInformation CurrentServerInformationValue() {
	return CurrentServerInformation.value_or(ServerInformation{});
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
		bool hideReplyButton);

	NotificationData(const NotificationData &other) = delete;
	NotificationData &operator=(const NotificationData &other) = delete;
	NotificationData(NotificationData &&other) = delete;
	NotificationData &operator=(NotificationData &&other) = delete;

	~NotificationData();

	void show();
	void close();
	void setImage(const QString &imagePath);

private:
	const not_null<Manager*> _manager;
	NotificationId _id;

	Glib::RefPtr<Gio::DBus::Connection> _dbusConnection;
	Glib::ustring _title;
	Glib::ustring _body;
	std::vector<Glib::ustring> _actions;
	std::map<Glib::ustring, Glib::VariantBase> _hints;
	Glib::ustring _imageKey;

	uint _notificationId = 0;
	uint _actionInvokedSignalId = 0;
	uint _notificationRepliedSignalId = 0;
	uint _notificationClosedSignalId = 0;

	void notificationClosed(uint id, uint reason);
	void actionInvoked(uint id, const Glib::ustring &actionName);
	void notificationReplied(uint id, const Glib::ustring &text);

};

using Notification = std::unique_ptr<NotificationData>;

NotificationData::NotificationData(
	not_null<Manager*> manager,
	NotificationId id)
: _manager(manager)
, _id(id) {
}

bool NotificationData::init(
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideReplyButton) {
	try {
		_dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
		return false;
	}

	const auto weak = base::make_weak(this);
	const auto capabilities = CurrentCapabilities;

	const auto signalEmitted = [=](
			const Glib::RefPtr<Gio::DBus::Connection> &connection,
			const Glib::ustring &sender_name,
			const Glib::ustring &object_path,
			const Glib::ustring &interface_name,
			const Glib::ustring &signal_name,
			Glib::VariantContainerBase parameters) {
		try {
			if (signal_name == "ActionInvoked") {
				const auto id = GlibVariantCast<uint>(
					parameters.get_child(0));

				const auto actionName = GlibVariantCast<Glib::ustring>(
					parameters.get_child(1));

				crl::on_main(weak, [=] { actionInvoked(id, actionName); });
			} else if (signal_name == "NotificationReplied") {
				const auto id = GlibVariantCast<uint>(
					parameters.get_child(0));

				const auto text = GlibVariantCast<Glib::ustring>(
					parameters.get_child(1));

				crl::on_main(weak, [=] { notificationReplied(id, text); });
			} else if (signal_name == "NotificationClosed") {
				const auto id = GlibVariantCast<uint>(
					parameters.get_child(0));

				const auto reason = GlibVariantCast<uint>(
					parameters.get_child(1));

				crl::on_main(weak, [=] { notificationClosed(id, reason); });
			}
		} catch (const std::exception &e) {
			LOG(("Native Notification Error: %1").arg(
				QString::fromStdString(e.what())));
		}
	};

	_title = title.toStdString();
	_imageKey = GetImageKey(CurrentServerInformationValue().specVersion);

	if (capabilities.contains(qsl("body-markup"))) {
		_body = subtitle.isEmpty()
			? msg.toHtmlEscaped().toStdString()
			: qsl("<b>%1</b>\n%2").arg(
				subtitle.toHtmlEscaped(),
				msg.toHtmlEscaped()).toStdString();
	} else {
		_body = subtitle.isEmpty()
			? msg.toStdString()
			: qsl("%1\n%2").arg(subtitle, msg).toStdString();
	}

	if (capabilities.contains("actions")) {
		_actions.push_back("default");
		_actions.push_back({});

		if (!hideReplyButton) {
			_actions.push_back("mail-mark-read");
			_actions.push_back(
				tr::lng_context_mark_read(tr::now).toStdString());
		}

		if (capabilities.contains("inline-reply") && !hideReplyButton) {
			_actions.push_back("inline-reply");
			_actions.push_back(
				tr::lng_notification_reply(tr::now).toStdString());

			_notificationRepliedSignalId = _dbusConnection->signal_subscribe(
				signalEmitted,
				std::string(kService),
				std::string(kInterface),
				"NotificationReplied",
				std::string(kObjectPath));
		} else {
			// icon name according to https://specifications.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html
			_actions.push_back("mail-reply-sender");
			_actions.push_back(
				tr::lng_notification_reply(tr::now).toStdString());
		}

		_actionInvokedSignalId = _dbusConnection->signal_subscribe(
			signalEmitted,
			std::string(kService),
			std::string(kInterface),
			"ActionInvoked",
			std::string(kObjectPath));
	}

	if (capabilities.contains("action-icons")) {
		_hints["action-icons"] = Glib::Variant<bool>::create(true);
	}

	// suppress system sound if telegram sound activated,
	// otherwise use system sound
	if (capabilities.contains("sound")) {
		if (Core::App().settings().soundNotify()) {
			_hints["suppress-sound"] = Glib::Variant<bool>::create(true);
		} else {
			// sound name according to http://0pointer.de/public/sound-naming-spec.html
			_hints["sound-name"] = Glib::Variant<Glib::ustring>::create(
				"message-new-instant");
		}
	}

	if (capabilities.contains("x-canonical-append")) {
		_hints["x-canonical-append"] = Glib::Variant<Glib::ustring>::create(
			"true");
	}

	_hints["category"] = Glib::Variant<Glib::ustring>::create("im.received");

	_hints["desktop-entry"] = Glib::Variant<Glib::ustring>::create(
		QGuiApplication::desktopFileName().chopped(8).toStdString());

	_notificationClosedSignalId = _dbusConnection->signal_subscribe(
		signalEmitted,
		std::string(kService),
		std::string(kInterface),
		"NotificationClosed",
		std::string(kObjectPath));
	return true;
}

NotificationData::~NotificationData() {
	if (_dbusConnection) {
		if (_actionInvokedSignalId != 0) {
			_dbusConnection->signal_unsubscribe(_actionInvokedSignalId);
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
	// a hack for snap's activation restriction
	const auto weak = base::make_weak(this);
	StartServiceAsync(crl::guard(weak, [=] {
		const auto iconName = _imageKey.empty()
			|| _hints.find(_imageKey) == end(_hints)
				? Glib::ustring(GetIconName().toStdString())
				: Glib::ustring();
		const auto connection = _dbusConnection;

		connection->call(
			std::string(kObjectPath),
			std::string(kInterface),
			"Notify",
			MakeGlibVariant(std::tuple{
				Glib::ustring(std::string(AppName)),
				uint(0),
				iconName,
				_title,
				_body,
				_actions,
				_hints,
				-1,
			}),
			[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
				try {
					auto reply = connection->call_finish(result);
					const auto notificationId = GlibVariantCast<uint>(
						reply.get_child(0));
					crl::on_main(weak, [=] {
						_notificationId = notificationId;
					});
					return;
				} catch (const Glib::Error &e) {
					LOG(("Native Notification Error: %1").arg(
						QString::fromStdString(e.what())));
				} catch (const std::exception &e) {
					LOG(("Native Notification Error: %1").arg(
						QString::fromStdString(e.what())));
				}
				crl::on_main(weak, [=] {
					_manager->clearNotification(_id);
				});
			},
			std::string(kService));
	}));
}

void NotificationData::close() {
	_dbusConnection->call(
		std::string(kObjectPath),
		std::string(kInterface),
		"CloseNotification",
		MakeGlibVariant(std::tuple{
			_notificationId,
		}),
		{},
		std::string(kService));
	_manager->clearNotification(_id);
}

void NotificationData::setImage(const QString &imagePath) {
	if (imagePath.isEmpty() || _imageKey.empty()) {
		return;
	}

	const auto image = QImage(imagePath)
		.convertToFormat(QImage::Format_RGBA8888);

	if (image.isNull()) {
		return;
	}

	_hints[_imageKey] = MakeGlibVariant(std::tuple{
		image.width(),
		image.height(),
		image.bytesPerLine(),
		true,
		8,
		4,
		std::vector<uchar>(
			image.constBits(),
			image.constBits() + image.sizeInBytes()),
	});
}

void NotificationData::notificationClosed(uint id, uint reason) {
	if (id == _notificationId) {
		_manager->clearNotification(_id);
	}
}

void NotificationData::actionInvoked(
		uint id,
		const Glib::ustring &actionName) {
	if (id != _notificationId) {
		return;
	}

	if (actionName == "default"
		|| actionName == "mail-reply-sender") {
		_manager->notificationActivated(_id);
	} else if (actionName == "mail-mark-read") {
		_manager->notificationReplied(_id, {});
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

bool SkipAudioForCustom() {
	return false;
}

bool SkipToastForCustom() {
	return false;
}

bool SkipFlashBounceForCustom() {
	return false;
}

bool Supported() {
	return ServiceRegistered;
}

bool Enforced() {
	// Wayland doesn't support positioning
	// and custom notifications don't work here
	return IsWayland();
}

bool ByDefault() {
	return IsQualifiedDaemon();
}

void Create(Window::Notifications::System *system) {
	static const auto ServiceWatcher = CreateServiceWatcher();

	const auto managerSetter = [=] {
		using ManagerType = Window::Notifications::ManagerType;
		if ((Core::App().settings().nativeNotifications() && Supported())
			|| Enforced()) {
			if (!system->managerType().has_value()
				|| *system->managerType() != ManagerType::Native) {
				system->setManager(std::make_unique<Manager>(system));
			}
		} else if (!system->managerType().has_value()
			|| *system->managerType() != ManagerType::Default) {
			system->setManager(nullptr);
		}
	};

	const auto counter = std::make_shared<int>(3);
	const auto oneReady = [=] {
		if (!--*counter) {
			managerSetter();
		}
	};

	const auto serviceActivated = [=] {
		ServiceRegistered = GetServiceRegistered();

		if (!ServiceRegistered) {
			CurrentServerInformation = std::nullopt;
			CurrentCapabilities = QStringList{};
			InhibitionSupported = false;
			managerSetter();
			return;
		}

		GetServerInformation([=](const std::optional<ServerInformation> &result) {
			CurrentServerInformation = result;
			oneReady();
		});

		GetCapabilities([=](const QStringList &result) {
			CurrentCapabilities = result;
			oneReady();
		});

		GetInhibitionSupported([=](bool result) {
			InhibitionSupported = result;
			oneReady();
		});
	};

	// There are some asserts that manager is not nullptr,
	// avoid crashes until some real manager is created
	if (!system->managerType().has_value()) {
		using DummyManager = Window::Notifications::DummyManager;
		system->setManager(std::make_unique<DummyManager>(system));
	}

	// snap doesn't allow access when the daemon is not running :(
	StartServiceAsync(serviceActivated);
}

class Manager::Private {
public:
	using Type = Window::Notifications::CachedUserpics::Type;
	explicit Private(not_null<Manager*> manager, Type type);

	void showNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton);
	void clearAll();
	void clearFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
	void clearNotification(NotificationId id);

	~Private();

private:
	const not_null<Manager*> _manager;

	base::flat_map<
		FullPeer,
		base::flat_map<MsgId, Notification>> _notifications;

	Window::Notifications::CachedUserpics _cachedUserpics;

};

Manager::Private::Private(not_null<Manager*> manager, Type type)
: _manager(manager)
, _cachedUserpics(type) {
	if (!Supported()) {
		return;
	}

	const auto serverInformation = CurrentServerInformation;
	const auto capabilities = CurrentCapabilities;

	if (serverInformation.has_value()) {
		LOG(("Notification daemon product name: %1")
			.arg(serverInformation->name));

		LOG(("Notification daemon vendor name: %1")
			.arg(serverInformation->vendor));

		LOG(("Notification daemon version: %1")
			.arg(serverInformation->version.toString()));

		LOG(("Notification daemon specification version: %1")
			.arg(serverInformation->specVersion.toString()));
	}

	if (!capabilities.isEmpty()) {
		LOG(("Notification daemon capabilities: %1")
			.arg(capabilities.join(", ")));
	}
}

void Manager::Private::showNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	if (!Supported()) {
		return;
	}

	const auto key = FullPeer{
		.sessionId = peer->session().uniqueId(),
		.peerId = peer->id
	};
	const auto notificationId = NotificationId{ .full = key, .msgId = msgId };
	auto notification = std::make_unique<NotificationData>(
		_manager,
		notificationId);
	const auto inited = notification->init(
		title,
		subtitle,
		msg,
		hideReplyButton);
	if (!inited) {
		return;
	}

	if (!hideNameAndPhoto) {
		const auto userpicKey = peer->userpicUniqueKey(userpicView);
		notification->setImage(
			_cachedUserpics.get(userpicKey, peer, userpicView));
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
	if (!Supported()) {
		return;
	}

	for (const auto &[key, notifications] : base::take(_notifications)) {
		for (const auto &[msgId, notification] : notifications) {
			notification->close();
		}
	}
}

void Manager::Private::clearFromHistory(not_null<History*> history) {
	if (!Supported()) {
		return;
	}

	const auto key = FullPeer{
		.sessionId = history->session().uniqueId(),
		.peerId = history->peer->id
	};
	auto i = _notifications.find(key);
	if (i != _notifications.cend()) {
		const auto temp = base::take(i->second);
		_notifications.erase(i);

		for (const auto &[msgId, notification] : temp) {
			notification->close();
		}
	}
}

void Manager::Private::clearFromSession(not_null<Main::Session*> session) {
	if (!Supported()) {
		return;
	}

	const auto sessionId = session->uniqueId();
	for (auto i = _notifications.begin(); i != _notifications.end();) {
		if (i->first.sessionId != sessionId) {
			++i;
			continue;
		}
		const auto temp = base::take(i->second);
		i = _notifications.erase(i);

		for (const auto &[msgId, notification] : temp) {
			notification->close();
		}
	}
}

void Manager::Private::clearNotification(NotificationId id) {
	if (!Supported()) {
		return;
	}

	auto i = _notifications.find(id.full);
	if (i != _notifications.cend()) {
		if (i->second.remove(id.msgId) && i->second.empty()) {
			_notifications.erase(i);
		}
	}
}

Manager::Private::~Private() {
	clearAll();
}

Manager::Manager(not_null<Window::Notifications::System*> system)
: NativeManager(system)
, _private(std::make_unique<Private>(this, Private::Type::Rounded)) {
}

void Manager::clearNotification(NotificationId id) {
	_private->clearNotification(id);
}

Manager::~Manager() = default;

void Manager::doShowNativeNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	_private->showNotification(
		peer,
		userpicView,
		msgId,
		title,
		subtitle,
		msg,
		hideNameAndPhoto,
		hideReplyButton);
}

void Manager::doClearAllFast() {
	_private->clearAll();
}

void Manager::doClearFromHistory(not_null<History*> history) {
	_private->clearFromHistory(history);
}

void Manager::doClearFromSession(not_null<Main::Session*> session) {
	_private->clearFromSession(session);
}

bool Manager::doSkipAudio() const {
	return Inhibited();
}

bool Manager::doSkipToast() const {
	return false;
}

bool Manager::doSkipFlashBounce() const {
	return Inhibited();
}

} // namespace Notifications
} // namespace Platform
