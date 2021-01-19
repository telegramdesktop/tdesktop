
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/notifications_manager_linux.h"

#include "window/notifications_utilities.h"
#include "base/platform/base_platform_info.h"
#include "platform/linux/specific_linux.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "history/history.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"

#include <QtCore/QVersionNumber>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusConnectionInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusError>

extern "C" {
#undef signals
#include <gio/gio.h>
#define signals public
} // extern "C"

namespace Platform {
namespace Notifications {
namespace {

constexpr auto kDBusTimeout = 30000;
constexpr auto kService = "org.freedesktop.Notifications"_cs;
constexpr auto kObjectPath = "/org/freedesktop/Notifications"_cs;
constexpr auto kInterface = kService;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;
constexpr auto kImageDataType = "(iiibii@ay)"_cs;
constexpr auto kNotifyArgsType = "(susssasa{sv}i)"_cs;

bool ServiceRegistered = false;
bool InhibitionSupported = false;
std::vector<QString> CurrentServerInformation;
QStringList CurrentCapabilities;

bool GetServiceRegistered() {
	const auto interface = QDBusConnection::sessionBus().interface();
	const auto activatable = IsNotificationServiceActivatable();

	return interface
		? interface->isServiceRegistered(kService.utf16()) || activatable
		: activatable;
}

std::vector<QString> GetServerInformation() {
	std::vector<QString> result;

	const auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("GetServerInformation"));

	// We may be launched earlier than notification daemon
	while (true) {
		const auto reply = QDBusConnection::sessionBus().call(message);

		if (reply.type() == QDBusMessage::ReplyMessage) {
			ranges::transform(
				reply.arguments(),
				ranges::back_inserter(result),
				&QVariant::toString);
		} else if (reply.type() == QDBusMessage::ErrorMessage) {
			LOG(("Native notification error: %1").arg(reply.errorMessage()));

			if (reply.errorName()
				== qsl("org.freedesktop.DBus.Error.NoReply")) {
				continue;
			}
		} else {
			LOG(("Native notification error: "
				"invalid reply from GetServerInformation"));
		}

		break;
	}

	return result;
}

QStringList GetCapabilities() {
	const auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("GetCapabilities"));

	// We may be launched earlier than notification daemon
	while (true) {
		const QDBusReply<QStringList> reply = QDBusConnection::sessionBus()
			.call(message);

		if (reply.isValid()) {
			return reply.value();
		}

		LOG(("Native notification error: %1").arg(reply.error().message()));

		if (reply.error().type() != QDBusError::NoReply) {
			break;
		}
	}

	return {};
}

bool GetInhibitionSupported() {
	auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kPropertiesInterface.utf16(),
		qsl("Get"));

	message.setArguments({
		qsl("org.freedesktop.Notifications"),
		qsl("Inhibited")
	});

	// We may be launched earlier than notification daemon
	while (true) {
		const QDBusError error = QDBusConnection::sessionBus().call(message);

		if (!error.isValid()) {
			return true;
		} else if (error.type() == QDBusError::InvalidArgs) {
			break;
		}

		LOG(("Native notification error: %1").arg(error.message()));

		if (error.type() != QDBusError::NoReply) {
			break;
		}
	}

	return false;
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
		qsl("org.freedesktop.Notifications"),
		qsl("Inhibited")
	});

	const QDBusReply<QVariant> reply = QDBusConnection::sessionBus().call(
		message);

	if (reply.isValid()) {
		return reply.value().toBool();
	}

	LOG(("Native notification error: %1").arg(reply.error().message()));
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

QVersionNumber ParseSpecificationVersion(
		const std::vector<QString> &serverInformation) {
	if (serverInformation.size() >= 4) {
		return QVersionNumber::fromString(serverInformation[3]);
	} else {
		LOG(("Native notification error: "
			"server information should have 4 elements"));
	}

	return QVersionNumber();
}

QString GetImageKey(const QVersionNumber &specificationVersion) {
	if (!specificationVersion.isNull()) {
		if (specificationVersion >= QVersionNumber(1, 2)) {
			return qsl("image-data");
		} else if (specificationVersion == QVersionNumber(1, 1)) {
			return qsl("image_data");
		} else if (specificationVersion < QVersionNumber(1, 1)) {
			return qsl("icon_data");
		} else {
			LOG(("Native notification error: unknown specification version"));
		}
	} else {
		LOG(("Native notification error: specification version is null"));
	}

	return QString();
}

class NotificationData {
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
	GDBusConnection *_dbusConnection = nullptr;
	base::weak_ptr<Manager> _manager;

	QString _title;
	QString _body;
	std::vector<QString> _actions;
	base::flat_map<QString, GVariant*> _hints;
	QString _imageKey;
	QImage _image;

	uint _notificationId = 0;
	guint _actionInvokedSignalId = 0;
	guint _notificationRepliedSignalId = 0;
	guint _notificationClosedSignalId = 0;
	NotificationId _id;

	void notificationClosed(uint id, uint reason);
	void actionInvoked(uint id, const QString &actionName);
	void notificationReplied(uint id, const QString &text);

	static void notificationShown(
		GObject *source_object,
		GAsyncResult *res,
		gpointer user_data);

	static void signalEmitted(
		GDBusConnection *connection,
		const gchar *sender_name,
		const gchar *object_path,
		const gchar *interface_name,
		const gchar *signal_name,
		GVariant *parameters,
		gpointer user_data);

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
, _title(title)
, _imageKey(GetImageKey(ParseSpecificationVersion(CurrentServerInformation)))
, _id(id) {
	GError *error = nullptr;

	_dbusConnection = g_bus_get_sync(
		G_BUS_TYPE_SESSION,
		nullptr,
		&error);

	if (error) {
		LOG(("Native notification error: %1").arg(error->message));
		g_error_free(error);
		return;
	}

	const auto capabilities = CurrentCapabilities;

	if (capabilities.contains(qsl("body-markup"))) {
		_body = subtitle.isEmpty()
			? msg.toHtmlEscaped()
			: qsl("<b>%1</b>\n%2")
				.arg(subtitle.toHtmlEscaped())
				.arg(msg.toHtmlEscaped());
	} else {
		_body = subtitle.isEmpty()
			? msg
			: qsl("%1\n%2").arg(subtitle).arg(msg);
	}

	if (capabilities.contains(qsl("actions"))) {
		_actions.push_back(qsl("default"));
		_actions.push_back(QString());

		if (!hideReplyButton) {
			_actions.push_back(qsl("mail-mark-read"));
			_actions.push_back(tr::lng_context_mark_read(tr::now));
		}

		if (capabilities.contains(qsl("inline-reply")) && !hideReplyButton) {
			_actions.push_back(qsl("inline-reply"));
			_actions.push_back(tr::lng_notification_reply(tr::now));

			_notificationRepliedSignalId = g_dbus_connection_signal_subscribe(
				_dbusConnection,
				kService.utf8(),
				kInterface.utf8(),
				"NotificationReplied",
				kObjectPath.utf8(),
				nullptr,
				G_DBUS_SIGNAL_FLAGS_NONE,
				signalEmitted,
				this,
				nullptr);
		} else {
			// icon name according to https://specifications.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html
			_actions.push_back(qsl("mail-reply-sender"));
			_actions.push_back(tr::lng_notification_reply(tr::now));
		}

		_actionInvokedSignalId = g_dbus_connection_signal_subscribe(
			_dbusConnection,
			kService.utf8(),
			kInterface.utf8(),
			"ActionInvoked",
			kObjectPath.utf8(),
			nullptr,
			G_DBUS_SIGNAL_FLAGS_NONE,
			signalEmitted,
			this,
			nullptr);
	}

	if (capabilities.contains(qsl("action-icons"))) {
		_hints.emplace(qsl("action-icons"), g_variant_new_boolean(true));
	}

	// suppress system sound if telegram sound activated,
	// otherwise use system sound
	if (capabilities.contains(qsl("sound"))) {
		if (Core::App().settings().soundNotify()) {
			_hints.emplace(
				qsl("suppress-sound"),
				g_variant_new_boolean(true));
		} else {
			// sound name according to http://0pointer.de/public/sound-naming-spec.html
			_hints.emplace(
				qsl("sound-name"),
				g_variant_new_string("message-new-instant"));
		}
	}

	if (capabilities.contains(qsl("x-canonical-append"))) {
		_hints.emplace(
			qsl("x-canonical-append"),
			g_variant_new_string("true"));
	}

	_hints.emplace(qsl("category"), g_variant_new_string("im.received"));

	_hints.emplace(
		qsl("desktop-entry"),
		g_variant_new_string(GetLauncherBasename().toUtf8()));

	_notificationClosedSignalId = g_dbus_connection_signal_subscribe(
		_dbusConnection,
		kService.utf8(),
		kInterface.utf8(),
		"NotificationClosed",
		kObjectPath.utf8(),
		nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE,
		signalEmitted,
		this,
		nullptr);
}

NotificationData::~NotificationData() {
	if (_dbusConnection) {
		if (_actionInvokedSignalId != 0) {
			g_dbus_connection_signal_unsubscribe(
				_dbusConnection,
				_actionInvokedSignalId);
		}

		if (_notificationRepliedSignalId != 0) {
			g_dbus_connection_signal_unsubscribe(
				_dbusConnection,
				_notificationRepliedSignalId);
		}

		if (_notificationClosedSignalId != 0) {
			g_dbus_connection_signal_unsubscribe(
				_dbusConnection,
				_notificationClosedSignalId);
		}

		g_object_unref(_dbusConnection);
	}

	for (const auto &[key, value] : _hints) {
		if (value) {
			g_variant_unref(value);
		}
	}
}

void NotificationData::show() {
	GVariantBuilder actionsBuilder, hintsBuilder;
	GError *error = nullptr;

	g_variant_builder_init(&actionsBuilder, G_VARIANT_TYPE("as"));
	for (const auto &value : _actions) {
		g_variant_builder_add(
			&actionsBuilder,
			"s",
			value.toUtf8().constData());
	}

	g_variant_builder_init(&hintsBuilder, G_VARIANT_TYPE("a{sv}"));
	for (auto &[key, value] : _hints) {
		g_variant_builder_add(
			&hintsBuilder,
			"{sv}",
			key.toUtf8().constData(),
			value);

		value = nullptr;
	}

	const auto iconName = _imageKey.isEmpty() || !_hints.contains(_imageKey)
		? GetIconName()
		: QString();

	g_dbus_connection_call(
		_dbusConnection,
		kService.utf8(),
		kObjectPath.utf8(),
		kInterface.utf8(),
		"Notify",
		g_variant_new(
			kNotifyArgsType.utf8(),
			AppName.utf8().constData(),
			0,
			iconName.toUtf8().constData(),
			_title.toUtf8().constData(),
			_body.toUtf8().constData(),
			&actionsBuilder,
			&hintsBuilder,
			-1),
		nullptr,
		G_DBUS_CALL_FLAGS_NONE,
		kDBusTimeout,
		nullptr,
		notificationShown,
		this);
}

void NotificationData::notificationShown(
		GObject *source_object,
		GAsyncResult *res,
		gpointer user_data) {
	const auto notificationData = reinterpret_cast<NotificationData*>(
		user_data);

	if (!notificationData) {
		return;
	}

	GError *error = nullptr;

	auto reply = g_dbus_connection_call_finish(
		notificationData->_dbusConnection,
		res,
		&error);

	if (!error) {
		g_variant_get(reply, "(u)", &notificationData->_notificationId);
		g_variant_unref(reply);
	} else {
		const auto manager = notificationData->_manager;
		const auto my = notificationData->_id;
		crl::on_main(manager, [=] {
			manager->clearNotification(my);
		});
		LOG(("Native notification error: %1").arg(error->message));
		g_error_free(error);
	}
}

void NotificationData::close() {
	g_dbus_connection_call(
		_dbusConnection,
		kService.utf8(),
		kObjectPath.utf8(),
		kInterface.utf8(),
		"CloseNotification",
		g_variant_new("(u)", _notificationId),
		nullptr,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		nullptr,
		nullptr,
		nullptr);
}

void NotificationData::setImage(const QString &imagePath) {
	if (_imageKey.isEmpty()) {
		return;
	}

	_image = QImage(imagePath).convertToFormat(QImage::Format_RGBA8888);

	_hints.emplace(_imageKey, g_variant_new(
		kImageDataType.utf8(),
		_image.width(),
		_image.height(),
		_image.bytesPerLine(),
		true,
		8,
		4,
		g_variant_new_from_data(
			G_VARIANT_TYPE("ay"),
			_image.constBits(),
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
			_image.byteCount(),
#else // Qt < 5.10.0
			_image.sizeInBytes(),
#endif // Qt >= 5.10.0
			true,
			nullptr,
			nullptr)));
}

void NotificationData::signalEmitted(
		GDBusConnection *connection,
		const gchar *sender_name,
		const gchar *object_path,
		const gchar *interface_name,
		const gchar *signal_name,
		GVariant *parameters,
		gpointer user_data) {
	const auto notificationData = reinterpret_cast<NotificationData*>(
		user_data);

	if (!notificationData) {
		return;
	}

	if(signal_name == qstr("ActionInvoked")) {
		guint32 id;
		gchar *actionName;
		g_variant_get(parameters, "(us)", &id, &actionName);
		notificationData->actionInvoked(id, actionName);
		g_free(actionName);
	}

	if(signal_name == qstr("NotificationReplied")) {
		guint32 id;
		gchar *text;
		g_variant_get(parameters, "(us)", &id, &text);
		notificationData->notificationReplied(id, text);
		g_free(text);
	}

	if(signal_name == qstr("NotificationClosed")) {
		guint32 id;
		guint32 reason;
		g_variant_get(parameters, "(uu)", &id, &reason);
		notificationData->notificationClosed(id, reason);
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

void NotificationData::actionInvoked(uint id, const QString &actionName) {
	if (id != _notificationId) {
		return;
	}

	if (actionName == qsl("default")
		|| actionName == qsl("mail-reply-sender")) {
		const auto manager = _manager;
		const auto my = _id;
		crl::on_main(manager, [=] {
			manager->notificationActivated(my);
		});
	} else if (actionName == qsl("mail-mark-read")) {
		const auto manager = _manager;
		const auto my = _id;
		crl::on_main(manager, [=] {
			manager->notificationReplied(my, {});
		});
	}
}

void NotificationData::notificationReplied(uint id, const QString &text) {
	if (id == _notificationId) {
		const auto manager = _manager;
		const auto my = _id;
		crl::on_main(manager, [=] {
			manager->notificationReplied(my, { text, {} });
		});
	}
}

} // namespace

bool SkipAudio() {
	return Inhibited();
}

bool SkipToast() {
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
	return IsQualifiedDaemon() || IsWayland();
}

std::unique_ptr<Window::Notifications::Manager> Create(
		Window::Notifications::System *system) {
	ServiceRegistered = GetServiceRegistered();

	if (Supported()) {
		CurrentServerInformation = GetServerInformation();
		CurrentCapabilities = GetCapabilities();
		InhibitionSupported = GetInhibitionSupported();
	} else {
		CurrentServerInformation = {};
		CurrentCapabilities = QStringList{};
		InhibitionSupported = false;
	}

	if ((Core::App().settings().nativeNotifications() && Supported())
		|| Enforced()) {
		return std::make_unique<Manager>(system);
	}

	return nullptr;
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

	if (!serverInformation.empty()) {
		LOG(("Notification daemon product name: %1")
			.arg(serverInformation[0]));

		LOG(("Notification daemon vendor name: %1")
			.arg(serverInformation[1]));

		LOG(("Notification daemon version: %1")
			.arg(serverInformation[2]));

		LOG(("Notification daemon specification version: %1")
			.arg(serverInformation[3]));
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
