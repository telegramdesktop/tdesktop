
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

#include <QtCore/QVersionNumber>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusPendingCall>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusError>

#include <glibmm.h>
#include <giomm.h>

namespace Platform {
namespace Notifications {
namespace {

constexpr auto kService = "org.freedesktop.Notifications"_cs;
constexpr auto kObjectPath = "/org/freedesktop/Notifications"_cs;
constexpr auto kInterface = kService;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

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

bool GetServiceRegistered() {
	try {
		const auto connection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		static const auto activatable = ranges::contains(
			base::Platform::DBus::ListActivatableNames(connection),
			Glib::ustring(std::string(kService)));

		return base::Platform::DBus::NameHasOwner(
				connection,
				std::string(kService)) || activatable;
	} catch (...) {
	}

	return false;
}

void GetServerInformation(
		Fn<void(std::optional<ServerInformation>)> callback) {
	using ServerInformationReply = QDBusPendingReply<
		QString,
		QString,
		QString,
		QString>;

	const auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("GetServerInformation"));

	const auto async = QDBusConnection::sessionBus().asyncCall(message);
	auto watcher = new QDBusPendingCallWatcher(async);

	const auto finished = [=](QDBusPendingCallWatcher *call) {
		const ServerInformationReply reply = *call;

		if (reply.isValid()) {
			crl::on_main([=] {
				callback(ServerInformation{
					reply.argumentAt<0>(),
					reply.argumentAt<1>(),
					QVersionNumber::fromString(reply.argumentAt<2>()),
					QVersionNumber::fromString(reply.argumentAt<3>()),
				});
			});
		} else {
			LOG(("Native Notification Error: %1: %2")
				.arg(reply.error().name())
				.arg(reply.error().message()));

			crl::on_main([=] { callback(std::nullopt); });
		}

		call->deleteLater();
	};

	QObject::connect(watcher, &QDBusPendingCallWatcher::finished, finished);
}

void GetCapabilities(Fn<void(QStringList)> callback) {
	const auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("GetCapabilities"));

	const auto async = QDBusConnection::sessionBus().asyncCall(message);
	auto watcher = new QDBusPendingCallWatcher(async);

	const auto finished = [=](QDBusPendingCallWatcher *call) {
		const QDBusPendingReply<QStringList> reply = *call;

		if (reply.isValid()) {
			crl::on_main([=] { callback(reply.value()); });
		} else {
			LOG(("Native Notification Error: %1: %2")
				.arg(reply.error().name())
				.arg(reply.error().message()));

			crl::on_main([=] { callback({}); });
		}

		call->deleteLater();
	};

	QObject::connect(watcher, &QDBusPendingCallWatcher::finished, finished);
}

void GetInhibitionSupported(Fn<void(bool)> callback) {
	auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kPropertiesInterface.utf16(),
		qsl("Get"));

	message.setArguments({
		kInterface.utf16(),
		qsl("Inhibited")
	});

	const auto async = QDBusConnection::sessionBus().asyncCall(message);
	auto watcher = new QDBusPendingCallWatcher(async);

	static const auto DontLogErrors = {
		QDBusError::NoError,
		QDBusError::InvalidArgs,
		QDBusError::UnknownProperty,
	};

	const auto finished = [=](QDBusPendingCallWatcher *call) {
		const auto error = QDBusPendingReply<QVariant>(*call).error();

		if (!ranges::contains(DontLogErrors, error.type())) {
			LOG(("Native Notification Error: %1: %2")
				.arg(error.name())
				.arg(error.message()));
		}

		crl::on_main([=] { callback(!error.isValid()); });
		call->deleteLater();
	};

	QObject::connect(watcher, &QDBusPendingCallWatcher::finished, finished);
}

bool Inhibited() {
	if (!Supported()
		|| !CurrentCapabilities.contains(qsl("inhibitions"))
		|| !InhibitionSupported) {
		return false;
	}

	auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kPropertiesInterface.utf16(),
		qsl("Get"));

	message.setArguments({
		kInterface.utf16(),
		qsl("Inhibited")
	});

	const QDBusReply<QVariant> reply = QDBusConnection::sessionBus().call(
		message);

	if (reply.isValid()) {
		return reply.value().toBool();
	}

	LOG(("Native Notification Error: %1: %2")
			.arg(reply.error().name())
			.arg(reply.error().message()));

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

class NotificationData : public sigc::trackable {
public:
	using NotificationId = Window::Notifications::Manager::NotificationId;

	NotificationData(
		const base::weak_ptr<Manager> &manager,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		NotificationId id,
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
	Glib::RefPtr<Gio::DBus::Connection> _dbusConnection;
	base::weak_ptr<Manager> _manager;

	Glib::ustring _title;
	Glib::ustring _body;
	std::vector<Glib::ustring> _actions;
	std::map<Glib::ustring, Glib::VariantBase> _hints;
	Glib::ustring _imageKey;

	uint _notificationId = 0;
	uint _actionInvokedSignalId = 0;
	uint _notificationRepliedSignalId = 0;
	uint _notificationClosedSignalId = 0;
	NotificationId _id;

	void notificationClosed(uint id, uint reason);
	void actionInvoked(uint id, const Glib::ustring &actionName);
	void notificationReplied(uint id, const Glib::ustring &text);

	void notificationShown(
		const Glib::RefPtr<Gio::AsyncResult> &result);

	void signalEmitted(
		const Glib::RefPtr<Gio::DBus::Connection> &connection,
		const Glib::ustring &sender_name,
		const Glib::ustring &object_path,
		const Glib::ustring &interface_name,
		const Glib::ustring &signal_name,
		const Glib::VariantContainerBase &parameters);

};

using Notification = std::shared_ptr<NotificationData>;

NotificationData::NotificationData(
	const base::weak_ptr<Manager> &manager,
	const QString &title,
	const QString &subtitle,
	const QString &msg,
	NotificationId id,
	bool hideReplyButton)
: _manager(manager)
, _title(title.toStdString())
, _imageKey(GetImageKey(CurrentServerInformationValue().specVersion))
, _id(id) {
	try {
		_dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));

		return;
	}

	const auto capabilities = CurrentCapabilities;

	if (capabilities.contains(qsl("body-markup"))) {
		_body = subtitle.isEmpty()
			? msg.toHtmlEscaped().toStdString()
			: qsl("<b>%1</b>\n%2")
				.arg(subtitle.toHtmlEscaped())
				.arg(msg.toHtmlEscaped()).toStdString();
	} else {
		_body = subtitle.isEmpty()
			? msg.toStdString()
			: qsl("%1\n%2").arg(subtitle).arg(msg).toStdString();
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
				sigc::mem_fun(this, &NotificationData::signalEmitted),
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
			sigc::mem_fun(this, &NotificationData::signalEmitted),
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
		GetLauncherBasename().toStdString());

	_notificationClosedSignalId = _dbusConnection->signal_subscribe(
		sigc::mem_fun(this, &NotificationData::signalEmitted),
		std::string(kService),
		std::string(kInterface),
		"NotificationClosed",
		std::string(kObjectPath));
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
	try {
		const auto iconName = _imageKey.empty()
			|| _hints.find(_imageKey) == end(_hints)
				? Glib::ustring(GetIconName().toStdString())
				: Glib::ustring();

		_dbusConnection->call(
			std::string(kObjectPath),
			std::string(kInterface),
			"Notify",
			base::Platform::MakeGlibVariant(std::tuple{
				Glib::ustring(std::string(AppName)),
				uint(0),
				iconName,
				_title,
				_body,
				_actions,
				_hints,
				-1,
			}),
			sigc::mem_fun(this, &NotificationData::notificationShown),
			std::string(kService));
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));

		const auto manager = _manager;
		const auto my = _id;
		crl::on_main(manager, [=] {
			manager->clearNotification(my);
		});
	}
}

void NotificationData::notificationShown(
		const Glib::RefPtr<Gio::AsyncResult> &result) {
	try {
		auto reply = _dbusConnection->call_finish(result);
		_notificationId = base::Platform::GlibVariantCast<uint>(
			reply.get_child(0));

		return;
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	} catch (const std::exception &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	}

	const auto manager = _manager;
	const auto my = _id;
	crl::on_main(manager, [=] {
		manager->clearNotification(my);
	});
}

void NotificationData::close() {
	try {
		_dbusConnection->call(
			std::string(kObjectPath),
			std::string(kInterface),
			"CloseNotification",
			base::Platform::MakeGlibVariant(std::tuple{
				_notificationId,
			}),
			{},
			std::string(kService));
	} catch (const Glib::Error &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	}
}

void NotificationData::setImage(const QString &imagePath) {
	if (_imageKey.empty()) {
		return;
	}

	const auto image = QImage(imagePath)
		.convertToFormat(QImage::Format_RGBA8888);

	_hints[_imageKey] = base::Platform::MakeGlibVariant(std::tuple{
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

void NotificationData::signalEmitted(
		const Glib::RefPtr<Gio::DBus::Connection> &connection,
		const Glib::ustring &sender_name,
		const Glib::ustring &object_path,
		const Glib::ustring &interface_name,
		const Glib::ustring &signal_name,
		const Glib::VariantContainerBase &parameters) {
	try {
		auto parametersCopy = parameters;

		if (signal_name == "ActionInvoked") {
			const auto id = base::Platform::GlibVariantCast<uint>(
				parametersCopy.get_child(0));

			const auto actionName = base::Platform::GlibVariantCast<
				Glib::ustring>(parametersCopy.get_child(1));

			actionInvoked(id, actionName);
		} else if (signal_name == "NotificationReplied") {
			const auto id = base::Platform::GlibVariantCast<uint>(
				parametersCopy.get_child(0));

			const auto text = base::Platform::GlibVariantCast<Glib::ustring>(
				parametersCopy.get_child(1));

			notificationReplied(id, text);
		} else if (signal_name == "NotificationClosed") {
			const auto id = base::Platform::GlibVariantCast<uint>(
				parametersCopy.get_child(0));

			const auto reason = base::Platform::GlibVariantCast<uint>(
				parametersCopy.get_child(1));

			notificationClosed(id, reason);
		}
	} catch (const std::exception &e) {
		LOG(("Native Notification Error: %1").arg(
			QString::fromStdString(e.what())));
	}
}

void NotificationData::notificationClosed(uint id, uint reason) {
	if (id == _notificationId) {
		const auto manager = _manager;
		const auto my = _id;
		crl::on_main(manager, [=] {
			manager->clearNotification(my);
		});
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
		const auto manager = _manager;
		const auto my = _id;
		crl::on_main(manager, [=] {
			manager->notificationActivated(my);
		});
	} else if (actionName == "mail-mark-read") {
		const auto manager = _manager;
		const auto my = _id;
		crl::on_main(manager, [=] {
			manager->notificationReplied(my, {});
		});
	}
}

void NotificationData::notificationReplied(
		uint id,
		const Glib::ustring &text) {
	if (id == _notificationId) {
		const auto manager = _manager;
		const auto my = _id;
		crl::on_main(manager, [=] {
			manager->notificationReplied(
				my,
				{ QString::fromStdString(text), {} });
		});
	}
}

} // namespace

bool SkipAudio() {
	return Inhibited();
}

bool SkipToast() {
	// Do not skip native notifications because of Do not disturb.
	// They respect this setting anyway.
	if ((Core::App().settings().nativeNotifications() && Supported())
		|| Enforced()) {
		return false;
	}

	return Inhibited();
}

bool SkipFlashBounce() {
	return Inhibited();
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
	ServiceRegistered = GetServiceRegistered();

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

	if (!ServiceRegistered) {
		CurrentServerInformation = std::nullopt;
		CurrentCapabilities = QStringList{};
		InhibitionSupported = false;
		managerSetter();
		return;
	}

	// There are some asserts that manager is not nullptr,
	// avoid crashes until some real manager is created
	if (!system->managerType().has_value()) {
		using DummyManager = Window::Notifications::DummyManager;
		system->setManager(std::make_unique<DummyManager>(system));
	}

	const auto counter = std::make_shared<int>(3);
	const auto oneReady = [=] {
		if (!--*counter) {
			managerSetter();
		}
	};

	GetServerInformation([=](std::optional<ServerInformation> result) {
		CurrentServerInformation = result;
		oneReady();
	});

	GetCapabilities([=](QStringList result) {
		CurrentCapabilities = result;
		oneReady();
	});

	GetInhibitionSupported([=](bool result) {
		InhibitionSupported = result;
		oneReady();
	});
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
	base::flat_map<
		FullPeer,
		base::flat_map<MsgId, Notification>> _notifications;

	Window::Notifications::CachedUserpics _cachedUserpics;
	base::weak_ptr<Manager> _manager;
};

Manager::Private::Private(not_null<Manager*> manager, Type type)
: _cachedUserpics(type)
, _manager(manager) {
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
	auto notification = std::make_shared<NotificationData>(
		_manager,
		title,
		subtitle,
		msg,
		NotificationId{ .full = key, .msgId = msgId },
		hideReplyButton);

	if (!hideNameAndPhoto) {
		const auto userpicKey = peer->userpicUniqueKey(userpicView);
		notification->setImage(
			_cachedUserpics.get(userpicKey, peer, userpicView));
	}

	auto i = _notifications.find(key);
	if (i != _notifications.cend()) {
		auto j = i->second.find(msgId);
		if (j != i->second.end()) {
			auto oldNotification = j->second;
			i->second.erase(j);
			oldNotification->close();
			i = _notifications.find(key);
		}
	}
	if (i == _notifications.cend()) {
		i = _notifications.emplace(
			key,
			base::flat_map<MsgId, Notification>()).first;
	}
	i->second.emplace(msgId, notification);
	notification->show();
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

} // namespace Notifications
} // namespace Platform
