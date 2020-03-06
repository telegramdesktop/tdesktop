/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_notifications_manager.h"
#include "window/notifications_utilities.h"
#include "base/weak_ptr.h"

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusArgument>
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

namespace Platform {
namespace Notifications {

inline void FlashBounce() {
}

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
class NotificationData : public QObject {
	Q_OBJECT

public:
	NotificationData(
		const base::weak_ptr<Manager> &manager,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		PeerId peerId,
		MsgId msgId,
		bool hideReplyButton);

	NotificationData(const NotificationData &other) = delete;
	NotificationData &operator=(const NotificationData &other) = delete;
	NotificationData(NotificationData &&other) = delete;
	NotificationData &operator=(NotificationData &&other) = delete;

	bool show();
	void close();
	void setImage(const QString &imagePath);

	struct ImageData {
		int width, height, rowStride;
		bool hasAlpha;
		int bitsPerSample, channels;
		QByteArray data;
	};

private:
	QDBusConnection _dbusConnection;
	base::weak_ptr<Manager> _manager;

	QString _title;
	QString _body;
	QStringList _actions;
	QVariantMap _hints;
	QString _imageKey;

	uint _notificationId;
	PeerId _peerId;
	MsgId _msgId;

private slots:
	void notificationClosed(uint id);
	void actionInvoked(uint id, const QString &actionName);
	void notificationReplied(uint id, const QString &text);
};

using Notification = std::shared_ptr<NotificationData>;

QDBusArgument &operator<<(
	QDBusArgument &argument,
	const NotificationData::ImageData &imageData);

const QDBusArgument &operator>>(
	const QDBusArgument &argument,
	NotificationData::ImageData &imageData);

class Manager
	: public Window::Notifications::NativeManager
	, public base::has_weak_ptr {
public:
	Manager(not_null<Window::Notifications::System*> system);
	void clearNotification(PeerId peerId, MsgId msgId);
	~Manager();

protected:
	void doShowNativeNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) override;
	void doClearAllFast() override;
	void doClearFromHistory(not_null<History*> history) override;

private:
	class Private;
	const std::unique_ptr<Private> _private;

};

class Manager::Private {
public:
	using Type = Window::Notifications::CachedUserpics::Type;
	explicit Private(not_null<Manager*> manager, Type type);

	void showNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton);
	void clearAll();
	void clearFromHistory(not_null<History*> history);
	void clearNotification(PeerId peerId, MsgId msgId);

	~Private();

private:
	using Notifications = QMap<PeerId, QMap<MsgId, Notification>>;
	Notifications _notifications;

	Window::Notifications::CachedUserpics _cachedUserpics;
	base::weak_ptr<Manager> _manager;
};
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

} // namespace Notifications
} // namespace Platform

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
Q_DECLARE_METATYPE(Platform::Notifications::NotificationData::ImageData)
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
