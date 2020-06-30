
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

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
#include <QtCore/QVersionNumber>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusError>
#include <QtDBus/QDBusMetaType>
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

namespace Platform {
namespace Notifications {

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
namespace {

constexpr auto kService = "org.freedesktop.Notifications"_cs;
constexpr auto kObjectPath = "/org/freedesktop/Notifications"_cs;
constexpr auto kInterface = kService;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

bool NotificationsSupported = false;
bool InhibitedNotSupported = false;

void ComputeSupported(bool wait = false) {
	const auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("GetServerInformation"));

	auto async = QDBusConnection::sessionBus().asyncCall(message);
	auto watcher = new QDBusPendingCallWatcher(async);

	QObject::connect(
		watcher,
		&QDBusPendingCallWatcher::finished,
		[=](QDBusPendingCallWatcher *call) {
			QDBusPendingReply<QString, QString, QString, QString> reply = *call;

			if (reply.isValid()) {
				NotificationsSupported = true;
			}

			call->deleteLater();
		});

	if (wait) {
		watcher->waitForFinished();
	}
}

void GetSupported() {
	static auto Checked = false;
	if (Checked) {
		return;
	}
	Checked = true;

	if (Core::App().settings().nativeNotifications()
		&& !Platform::IsWayland()) {
		ComputeSupported(true);
	} else {
		ComputeSupported();
	}
}

std::vector<QString> ComputeServerInformation() {
	std::vector<QString> serverInformation;

	const auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("GetServerInformation"));

	const auto reply = QDBusConnection::sessionBus().call(message);

	if (reply.type() == QDBusMessage::ReplyMessage) {
		ranges::transform(
			reply.arguments(),
			ranges::back_inserter(serverInformation),
			&QVariant::toString
		);
	} else if (reply.type() == QDBusMessage::ErrorMessage) {
		LOG(("Native notification error: %1").arg(reply.errorMessage()));
	} else {
		LOG(("Native notification error: "
			"invalid reply from GetServerInformation"));
	}

	return serverInformation;
}

std::vector<QString> GetServerInformation() {
	static const auto Result = ComputeServerInformation();
	return Result;
}

QStringList ComputeCapabilities() {
	const auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("GetCapabilities"));

	const QDBusReply<QStringList> reply = QDBusConnection::sessionBus().call(
		message);

	if (reply.isValid()) {
		return reply.value();
	} else {
		LOG(("Native notification error: %1").arg(reply.error().message()));
	}

	return {};
}

QStringList GetCapabilities() {
	static const auto Result = ComputeCapabilities();
	return Result;
}

bool Inhibited() {
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

	static const auto NotSupportedErrors = {
		QDBusError::ServiceUnknown,
		QDBusError::InvalidArgs,
	};

	if (reply.isValid()) {
		return reply.value().toBool();
	} else if (ranges::contains(NotSupportedErrors, reply.error().type())) {
		InhibitedNotSupported = true;
	} else {
		if (reply.error().type() == QDBusError::AccessDenied) {
			InhibitedNotSupported = true;
		}

		LOG(("Native notification error: %1").arg(reply.error().message()));
	}

	return false;
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

} // namespace

NotificationData::NotificationData(
	const base::weak_ptr<Manager> &manager,
	const QString &title,
	const QString &subtitle,
	const QString &msg,
	NotificationId id,
	bool hideReplyButton)
: _dbusConnection(QDBusConnection::sessionBus())
, _manager(manager)
, _title(title)
, _imageKey(GetImageKey(ParseSpecificationVersion(
	GetServerInformation())))
, _id(id) {
	const auto capabilities = GetCapabilities();

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
		_actions << qsl("default") << QString();

		_actions
			<< qsl("mail-mark-read")
			<< tr::lng_context_mark_read(tr::now);

		if (capabilities.contains(qsl("inline-reply")) && !hideReplyButton) {
			_actions
				<< qsl("inline-reply")
				<< tr::lng_notification_reply(tr::now);

			_dbusConnection.connect(
				kService.utf16(),
				kObjectPath.utf16(),
				kInterface.utf16(),
				qsl("NotificationReplied"),
				this,
				SLOT(notificationReplied(uint,QString)));
		} else {
			// icon name according to https://specifications.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html
			_actions
				<< qsl("mail-reply-sender")
				<< tr::lng_notification_reply(tr::now);
		}

		_dbusConnection.connect(
			kService.utf16(),
			kObjectPath.utf16(),
			kInterface.utf16(),
			qsl("ActionInvoked"),
			this,
			SLOT(actionInvoked(uint,QString)));
	}

	if (capabilities.contains(qsl("action-icons"))) {
		_hints["action-icons"] = true;
	}

	// suppress system sound if telegram sound activated, otherwise use system sound
	if (capabilities.contains(qsl("sound"))) {
		if (Core::App().settings().soundNotify()) {
			_hints["suppress-sound"] = true;
		} else {
			// sound name according to http://0pointer.de/public/sound-naming-spec.html
			_hints["sound-name"] = qsl("message-new-instant");
		}
	}

	if (capabilities.contains(qsl("x-canonical-append"))) {
		_hints["x-canonical-append"] = qsl("true");
	}

	_hints["category"] = qsl("im.received");
	_hints["desktop-entry"] = GetLauncherBasename();

	_dbusConnection.connect(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("NotificationClosed"),
		this,
		SLOT(notificationClosed(uint)));
}

bool NotificationData::show() {
	const auto iconName = _imageKey.isEmpty() || !_hints.contains(_imageKey)
		? GetIconName()
		: QString();

	auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("Notify"));

	message.setArguments({
		AppName.utf16(),
		uint(0),
		iconName,
		_title,
		_body,
		_actions,
		_hints,
		-1
	});

	const QDBusReply<uint> reply = _dbusConnection.call(
		message);

	if (reply.isValid()) {
		_notificationId = reply.value();
	} else {
		LOG(("Native notification error: %1").arg(reply.error().message()));
	}

	return reply.isValid();
}

void NotificationData::close() {
	auto message = QDBusMessage::createMethodCall(
		kService.utf16(),
		kObjectPath.utf16(),
		kInterface.utf16(),
		qsl("CloseNotification"));

	message.setArguments({
		_notificationId
	});

	_dbusConnection.send(message);
}

void NotificationData::setImage(const QString &imagePath) {
	if (_imageKey.isEmpty()) {
		return;
	}

	const auto image = QImage(imagePath)
		.convertToFormat(QImage::Format_RGBA8888);

	const QByteArray imageBytes(
		(const char*)image.constBits(),
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
		image.byteCount());
#else // Qt < 5.10.0
		image.sizeInBytes());
#endif // Qt >= 5.10.0

	const auto imageData = ImageData{
		image.width(),
		image.height(),
		image.bytesPerLine(),
		true,
		8,
		4,
		imageBytes
	};

	_hints[_imageKey] = QVariant::fromValue(imageData);
}

void NotificationData::notificationClosed(uint id) {
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

QDBusArgument &operator<<(
		QDBusArgument &argument,
		const NotificationData::ImageData &imageData) {
	argument.beginStructure();
	argument
		<< imageData.width
		<< imageData.height
		<< imageData.rowStride
		<< imageData.hasAlpha
		<< imageData.bitsPerSample
		<< imageData.channels
		<< imageData.data;
	argument.endStructure();
	return argument;
}

const QDBusArgument &operator>>(
		const QDBusArgument &argument,
		NotificationData::ImageData &imageData) {
	argument.beginStructure();
	argument
		>> imageData.width
		>> imageData.height
		>> imageData.rowStride
		>> imageData.hasAlpha
		>> imageData.bitsPerSample
		>> imageData.channels
		>> imageData.data;
	argument.endStructure();
	return argument;
}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

bool SkipAudio() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	if (Supported()
		&& GetCapabilities().contains(qsl("inhibitions"))
		&& !InhibitedNotSupported) {
		return Inhibited();
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	return false;
}

bool SkipToast() {
	return SkipAudio();
}

bool SkipFlashBounce() {
	return SkipAudio();
}

bool Supported() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	return NotificationsSupported;
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	return false;
}

std::unique_ptr<Window::Notifications::Manager> Create(
		Window::Notifications::System *system) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	GetSupported();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	if ((Core::App().settings().nativeNotifications() && Supported())
		|| Platform::IsWayland()) {
		return std::make_unique<Manager>(system);
	}

	return nullptr;
}

#ifdef TDESKTOP_DISABLE_DBUS_INTEGRATION
class Manager::Private {
public:
	using Type = Window::Notifications::CachedUserpics::Type;
	explicit Private(not_null<Manager*> manager, Type type) {}

	void showNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {}
	void clearAll() {}
	void clearFromHistory(not_null<History*> history) {}
	void clearFromSession(not_null<Main::Session*> session) {}
	void clearNotification(NotificationId id) {}
};
#else // TDESKTOP_DISABLE_DBUS_INTEGRATION
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
	qDBusRegisterMetaType<NotificationData::ImageData>();

	if (!Supported()) {
		return;
	}

	const auto serverInformation = GetServerInformation();
	const auto capabilities = GetCapabilities();

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
	if (!notification->show()) {
		i = _notifications.find(key);
		if (i != _notifications.cend()) {
			i->second.remove(msgId);
			if (i->second.empty()) {
				_notifications.erase(i);
			}
		}
	}
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
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

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
