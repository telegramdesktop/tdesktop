/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/notifications_manager_linux.h"

#include "history/history.h"
#include "lang/lang_keys.h"
#include "facades.h"

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
#include <QtCore/QVersionNumber>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusMetaType>
#endif

namespace Platform {
namespace Notifications {

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
namespace {

constexpr auto kService = str_const("org.freedesktop.Notifications");
constexpr auto kObjectPath = str_const("/org/freedesktop/Notifications");
constexpr auto kInterface = kService;

std::vector<QString> GetServerInformation(
		const std::shared_ptr<QDBusInterface> &notificationInterface) {
	std::vector<QString> serverInformation;
	auto serverInformationReply = notificationInterface
		->call(qsl("GetServerInformation"));

	if (serverInformationReply.type() == QDBusMessage::ReplyMessage) {
		for (const auto &arg : serverInformationReply.arguments()) {
			if (static_cast<QMetaType::Type>(arg.type())
				== QMetaType::QString) {
				serverInformation.push_back(arg.toString());
			} else {
				LOG(("Native notification error: "
					"all elements in GetServerInformation "
					"should be strings"));
			}
		}
	} else if (serverInformationReply.type() == QDBusMessage::ErrorMessage) {
		LOG(("Native notification error: %1")
			.arg(QDBusError(serverInformationReply).message()));
	} else {
		LOG(("Native notification error: "
			"error while getting information about notification daemon"));
	}

	return serverInformation;
}

QStringList GetCapabilities(
		const std::shared_ptr<QDBusInterface> &notificationInterface) {
	QDBusReply<QStringList> capabilitiesReply = notificationInterface
		->call(qsl("GetCapabilities"));

	if (capabilitiesReply.isValid()) {
		return capabilitiesReply.value();
	} else {
		LOG(("Native notification error: %1")
			.arg(capabilitiesReply.error().message()));
	}

	return {};
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

}

NotificationData::NotificationData(
		const std::shared_ptr<QDBusInterface> &notificationInterface,
		const base::weak_ptr<Manager> &manager,
		const QString &title, const QString &subtitle,
		const QString &msg, PeerId peerId, MsgId msgId)
: _notificationInterface(notificationInterface)
, _manager(manager)
, _title(title)
, _peerId(peerId)
, _msgId(msgId) {
	auto capabilities = GetCapabilities(_notificationInterface);

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

		connect(_notificationInterface.get(),
			SIGNAL(ActionInvoked(uint, QString)),
			this, SLOT(notificationClicked(uint)));

		if (capabilities.contains(qsl("inline-reply"))) {
			_actions << qsl("inline-reply")
				<< tr::lng_notification_reply(tr::now);

			connect(_notificationInterface.get(),
				SIGNAL(NotificationReplied(uint,QString)),
				this, SLOT(notificationReplied(uint,QString)));
		} else {
			// icon name according to https://specifications.freedesktop.org/icon-naming-spec/icon-naming-spec-latest.html
			_actions << qsl("mail-reply-sender")
				<< tr::lng_notification_reply(tr::now);
		}
	}

	if (capabilities.contains(qsl("action-icons"))) {
		_hints["action-icons"] = true;
	}

	// suppress system sound if telegram sound activated, otherwise use system sound
	if (capabilities.contains(qsl("sound"))) {
		if (Global::SoundNotify()) {
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

	_hints["desktop-entry"] =
		qsl(MACRO_TO_STRING(TDESKTOP_LAUNCHER_BASENAME));

	connect(_notificationInterface.get(),
		SIGNAL(NotificationClosed(uint, uint)),
		this, SLOT(notificationClosed(uint)));
}

bool NotificationData::show() {
	QDBusReply<uint> notifyReply = _notificationInterface->call(qsl("Notify"),
		str_const_toString(AppName), uint(0), QString(), _title, _body,
		_actions, _hints, -1);

	if (notifyReply.isValid()) {
		_notificationId = notifyReply.value();
	} else {
		LOG(("Native notification error: %1")
			.arg(notifyReply.error().message()));
	}

	return notifyReply.isValid();
}

bool NotificationData::close() {
	QDBusReply<void> closeReply = _notificationInterface
		->call(qsl("CloseNotification"), _notificationId);

	if (!closeReply.isValid()) {
		LOG(("Native notification error: %1")
			.arg(closeReply.error().message()));
	}

	return closeReply.isValid();
}

void NotificationData::setImage(const QString &imagePath) {
	auto specificationVersion = ParseSpecificationVersion(
		GetServerInformation(_notificationInterface));

	QString imageKey;

	if (!specificationVersion.isNull()) {
		const auto majorVersion = specificationVersion.majorVersion();
		const auto minorVersion = specificationVersion.minorVersion();

		if ((majorVersion == 1 && minorVersion >= 2) || majorVersion > 1) {
			imageKey = qsl("image-data");
		} else if (majorVersion == 1 && minorVersion == 1) {
			imageKey = qsl("image_data");
		} else if ((majorVersion == 1 && minorVersion < 1)
			|| majorVersion < 1) {
			imageKey = qsl("icon_data");
		} else {
			LOG(("Native notification error: unknown specification version"));
			return;
		}
	} else {
		LOG(("Native notification error: specification version is null"));
		return;
	}

	auto image = QImage(imagePath).convertToFormat(QImage::Format_RGBA8888);
	QByteArray imageBytes((const char*)image.constBits(),
		image.sizeInBytes());

	ImageData imageData;
	imageData.width = image.width();
	imageData.height = image.height();
	imageData.rowStride = image.bytesPerLine();
	imageData.hasAlpha = true;
	imageData.bitsPerSample = 8;
	imageData.channels = 4;
	imageData.data = imageBytes;

	_hints[imageKey] = QVariant::fromValue(imageData);
}

void NotificationData::notificationClosed(uint id) {
	if (id == _notificationId) {
		const auto manager = _manager;
		crl::on_main(manager, [=] {
			manager->clearNotification(_peerId, _msgId);
		});
	}
}

void NotificationData::notificationClicked(uint id) {
	if (id == _notificationId) {
		const auto manager = _manager;
		crl::on_main(manager, [=] {
			manager->notificationActivated(_peerId, _msgId);
		});
	}
}

void NotificationData::notificationReplied(uint id, const QString &text) {
	if (id == _notificationId) {
		const auto manager = _manager;
		crl::on_main(manager, [=] {
			manager->notificationReplied(_peerId, _msgId, { text, {} });
		});
	}
}

QDBusArgument &operator<<(QDBusArgument &argument,
		const NotificationData::ImageData &imageData) {
	argument.beginStructure();
	argument << imageData.width
		<< imageData.height
		<< imageData.rowStride
		<< imageData.hasAlpha
		<< imageData.bitsPerSample
		<< imageData.channels
		<< imageData.data;
	argument.endStructure();
	return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument,
		NotificationData::ImageData &imageData) {
	argument.beginStructure();
	argument >> imageData.width
		>> imageData.height
		>> imageData.rowStride
		>> imageData.hasAlpha
		>> imageData.bitsPerSample
		>> imageData.channels
		>> imageData.data;
	argument.endStructure();
	return argument;
}
#endif

bool Supported() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	static auto Available = QDBusInterface(
		str_const_toString(kService),
		str_const_toString(kObjectPath),
		str_const_toString(kInterface)).isValid();

	return Available;
#else
	return false;
#endif
}

std::unique_ptr<Window::Notifications::Manager> Create(
		Window::Notifications::System *system) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	if (Global::NativeNotifications() && Supported()) {
		return std::make_unique<Manager>(system);
	}
#endif
	return nullptr;
}

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
Manager::Private::Private(Manager *manager, Type type)
: _cachedUserpics(type)
, _manager(manager)
, _notificationInterface(std::make_shared<QDBusInterface>(
		str_const_toString(kService),
		str_const_toString(kObjectPath),
		str_const_toString(kInterface))) {
	qDBusRegisterMetaType<NotificationData::ImageData>();

	auto specificationVersion = ParseSpecificationVersion(
		GetServerInformation(_notificationInterface));

	auto capabilities = GetCapabilities(_notificationInterface);

	if (!specificationVersion.isNull()) {
		LOG(("Notification daemon specification version: %1")
			.arg(specificationVersion.toString()));
	}

	if (!capabilities.empty()) {
		const auto capabilitiesString = capabilities.join(", ");
		LOG(("Notification daemon capabilities: %1").arg(capabilitiesString));
	}
}

void Manager::Private::showNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	auto notification = std::make_shared<NotificationData>(
		_notificationInterface,
		_manager,
		title,
		subtitle,
		msg,
		peer->id,
		msgId);

	const auto key = hideNameAndPhoto
		? InMemoryKey()
		: peer->userpicUniqueKey();
	notification->setImage(_cachedUserpics.get(key, peer));

	auto i = _notifications.find(peer->id);
	if (i != _notifications.cend()) {
		auto j = i->find(msgId);
		if (j != i->cend()) {
			auto oldNotification = j.value();
			i->erase(j);
			oldNotification->close();
			i = _notifications.find(peer->id);
		}
	}
	if (i == _notifications.cend()) {
		i = _notifications.insert(peer->id, QMap<MsgId, Notification>());
	}
	_notifications[peer->id].insert(msgId, notification);
	if (!notification->show()) {
		i = _notifications.find(peer->id);
		if (i != _notifications.cend()) {
			i->remove(msgId);
			if (i->isEmpty()) _notifications.erase(i);
		}
	}
}

void Manager::Private::clearAll() {
	auto temp = base::take(_notifications);
	for_const (auto &notifications, temp) {
		for_const (auto notification, notifications) {
			notification->close();
		}
	}
}

void Manager::Private::clearFromHistory(not_null<History*> history) {
	auto i = _notifications.find(history->peer->id);
	if (i != _notifications.cend()) {
		auto temp = base::take(i.value());
		_notifications.erase(i);

		for_const (auto notification, temp) {
			notification->close();
		}
	}
}

void Manager::Private::clearNotification(PeerId peerId, MsgId msgId) {
	auto i = _notifications.find(peerId);
	if (i != _notifications.cend()) {
		i.value().remove(msgId);
		if (i.value().isEmpty()) {
			_notifications.erase(i);
		}
	}
}

Manager::Private::~Private() {
	clearAll();
}

Manager::Manager(Window::Notifications::System *system)
: NativeManager(system)
, _private(std::make_unique<Private>(this, Private::Type::Rounded)) {
}

void Manager::clearNotification(PeerId peerId, MsgId msgId) {
	_private->clearNotification(peerId, msgId);
}

Manager::~Manager() = default;

void Manager::doShowNativeNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	_private->showNotification(
		peer,
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
#endif

} // namespace Notifications
} // namespace Platform
