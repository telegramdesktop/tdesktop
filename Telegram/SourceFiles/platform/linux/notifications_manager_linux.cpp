/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "platform/linux/notifications_manager_linux.h"

#include "window/notifications_utilities.h"
#include "platform/linux/linux_libnotify.h"
#include "platform/linux/linux_libs.h"
#include "lang.h"

namespace Platform {
namespace Notifications {
namespace {

NeverFreedPointer<Manager> ManagerInstance;

bool LibNotifyLoaded() {
	return (Libs::notify_init != nullptr)
		&& (Libs::notify_uninit != nullptr)
		&& (Libs::notify_is_initted != nullptr)
//		&& (Libs::notify_get_app_name != nullptr)
//		&& (Libs::notify_set_app_name != nullptr)
		&& (Libs::notify_get_server_caps != nullptr)
		&& (Libs::notify_get_server_info != nullptr)
		&& (Libs::notify_notification_new != nullptr)
		&& (Libs::notify_notification_update != nullptr)
		&& (Libs::notify_notification_show != nullptr)
//		&& (Libs::notify_notification_set_app_name != nullptr)
		&& (Libs::notify_notification_set_timeout != nullptr)
//		&& (Libs::notify_notification_set_category != nullptr)
//		&& (Libs::notify_notification_set_urgency != nullptr)
//		&& (Libs::notify_notification_set_icon_from_pixbuf != nullptr)
		&& (Libs::notify_notification_set_image_from_pixbuf != nullptr)
//		&& (Libs::notify_notification_set_hint != nullptr)
//		&& (Libs::notify_notification_set_hint_int32 != nullptr)
//		&& (Libs::notify_notification_set_hint_uint32 != nullptr)
//		&& (Libs::notify_notification_set_hint_double != nullptr)
		&& (Libs::notify_notification_set_hint_string != nullptr)
//		&& (Libs::notify_notification_set_hint_byte != nullptr)
//		&& (Libs::notify_notification_set_hint_byte_array != nullptr)
//		&& (Libs::notify_notification_clear_hints != nullptr)
		&& (Libs::notify_notification_add_action != nullptr)
		&& (Libs::notify_notification_clear_actions != nullptr)
		&& (Libs::notify_notification_close != nullptr)
		&& (Libs::notify_notification_get_closed_reason != nullptr)
		&& (Libs::g_object_ref_sink != nullptr)
		&& (Libs::g_object_unref != nullptr)
		&& (Libs::g_list_free_full != nullptr)
		&& (Libs::g_error_free != nullptr)
		&& (Libs::g_signal_connect_data != nullptr)
		&& (Libs::g_signal_handler_disconnect != nullptr)
//		&& (Libs::gdk_pixbuf_new_from_data != nullptr)
		&& (Libs::gdk_pixbuf_new_from_file != nullptr);
}

QString escapeNotificationHtml(QString text) {
	text = text.replace(QChar('<'), qstr("&lt;"));
	text = text.replace(QChar('>'), qstr("&gt;"));
	text = text.replace(QChar('&'), qstr("&amp;"));
	return text;
}

class NotificationData {
public:
	NotificationData(const QString &title, const QString &body, PeerId peerId, MsgId msgId)
	: _data(Libs::notify_notification_new(title.toUtf8().constData(), body.toUtf8().constData(), nullptr))
	, _peerId(peerId)
	, _msgId(msgId) {
		if (valid()) {
			init();
		}
	}
	bool valid() const {
		return (_data != nullptr);
	}
	NotificationData(const NotificationData &other) = delete;
	NotificationData &operator=(const NotificationData &other) = delete;
	NotificationData(NotificationData &&other) = delete;
	NotificationData &operator=(NotificationData &&other) = delete;

	void setImage(const QString &imagePath) {
		auto imagePathNative = QFile::encodeName(imagePath);
		if (auto pixbuf = Libs::gdk_pixbuf_new_from_file(imagePathNative.constData(), nullptr)) {
			Libs::notify_notification_set_image_from_pixbuf(_data, pixbuf);
			Libs::g_object_unref(Libs::g_object_cast(pixbuf));
		}
	}
	bool show() {
		if (valid()) {
			GError *error = nullptr;
			Libs::notify_notification_show(_data, &error);
			if (!error) {
				return true;
			}

			logError(error);
		}
		return false;
	}

	bool close() {
		if (valid()) {
			GError *error = nullptr;
			Libs::notify_notification_close(_data, &error);
			if (!error) {
				return true;
			}

			logError(error);
		}
		return false;
	}

	~NotificationData() {
		if (valid()) {
			if (_handlerId > 0) {
				Libs::g_signal_handler_disconnect(Libs::g_object_cast(_data), _handlerId);
			}
			Libs::notify_notification_clear_actions(_data);
			Libs::g_object_unref(Libs::g_object_cast(_data));
		}
	}

private:
	void init() {
		_handlerId = Libs::g_signal_connect_helper(Libs::g_object_cast(_data), "closed", G_CALLBACK(NotificationData::notificationClosed), this);
		Libs::notify_notification_set_timeout(_data, Libs::NOTIFY_EXPIRES_DEFAULT);

		if (auto manager = ManagerInstance.data()) {
			if (manager->hasActionsSupport()) {
				Libs::notify_notification_add_action(_data, "default", lang(lng_context_reply_msg).toUtf8().constData(), NotificationData::notificationClicked, this, nullptr);
			}
		}
	}
	void onClose() {
		if (auto manager = ManagerInstance.data()) {
			manager->clearNotification(_peerId, _msgId);
		}
	}
	void onClick() {
		if (auto manager = ManagerInstance.data()) {
			manager->notificationActivated(_peerId, _msgId);
		}
	}

	void logError(GError *error) {
		LOG(("LibNotify Error: domain %1, code %2, message '%3'").arg(error->domain).arg(error->code).arg(QString::fromUtf8(error->message)));
		Libs::g_error_free(error);
	}

	static void notificationClosed(Libs::NotifyNotification *notification, gpointer data) {
		static_cast<NotificationData*>(data)->onClose();
	}
	static void notificationClicked(Libs::NotifyNotification *notification, char *action, gpointer data) {
		static_cast<NotificationData*>(data)->onClick();
	}

	Libs::NotifyNotification *_data = nullptr;
	PeerId _peerId = 0;
	MsgId _msgId = 0;
	gulong _handlerId = 0;

};

using Notification = QSharedPointer<NotificationData>;

} // namespace

void start() {
	if (LibNotifyLoaded()) {
		if (Libs::notify_is_initted() || Libs::notify_init("Telegram Desktop")) {
			ManagerInstance.makeIfNull();
		}
	}
}

Manager *manager() {
	if (Global::NativeNotifications()) {
		return ManagerInstance.data();
	}
	return nullptr;
}

bool supported() {
	return ManagerInstance.data() != nullptr;
}

void finish() {
	if (manager()) {
		ManagerInstance.reset();
		Libs::notify_uninit();
	}
}

class Manager::Impl {
public:
	Impl();

	void showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton);
	void clearAll();
	void clearFromHistory(History *history);
	void clearNotification(PeerId peerId, MsgId msgId);

	bool hasPoorSupport() const {
		return _poorSupported;
	}
	bool hasActionsSupport() const {
		return _actionsSupported;
	}

private:
	void showNextNotification();

	struct QueuedNotification {
		PeerData *peer = nullptr;
		MsgId msgId = 0;
		QString title;
		QString body;
		bool showUserpic = false;
	};

	using QueuedNotifications = QList<QueuedNotification>;
	QueuedNotifications _queuedNotifications;

	using Notifications = QMap<PeerId, QMap<MsgId, Notification>>;
	Notifications _notifications;

	Window::Notifications::CachedUserpics _cachedUserpics;
	bool _actionsSupported = false;
	bool _poorSupported = true;

};

void FreeCapability(void *ptr, void *data) {
	Libs::g_free(ptr);
}

Manager::Impl::Impl() {
	if (auto capabilities = Libs::notify_get_server_caps()) {
		for (auto capability = capabilities; capability; capability = capability->next) {
			if (QString::fromUtf8(static_cast<const char*>(capability->data)) == qstr("actions")) {
				_actionsSupported = true;
	            break;
	        }
	    }
	    Libs::g_list_free_full(capabilities, g_free);
	    Libs::g_list_free(capabilities);
	}

	// Unity and other Notify OSD users handle desktop notifications
	// extremely poor, even without the ability to close() them.
	gchar *name = nullptr;
	if (Libs::notify_get_server_info(&name, nullptr, nullptr, nullptr)) {
		if (name) {
			auto serverName = QString::fromUtf8(static_cast<const char*>(name));
			LOG(("Notifications Server: %1").arg(serverName));
			if (serverName == qstr("notify-osd")) {
				_actionsSupported = false;
			}
			Libs::g_free(name);
		}
	}
	if (!_actionsSupported) {
		_poorSupported = true;
	}
}

void Manager::Impl::showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton) {
	auto titleText = escapeNotificationHtml(title);
	auto bodyText = subtitle.isEmpty() ? escapeNotificationHtml(msg) : ("<b>" + escapeNotificationHtml(subtitle) + "</b>\n" + escapeNotificationHtml(msg));

	QueuedNotification notification;
	notification.peer = peer;
	notification.msgId = msgId;
	notification.title = titleText;
	notification.body = bodyText;
	notification.showUserpic = showUserpic;
	_queuedNotifications.push_back(notification);

	showNextNotification();
}

void Manager::Impl::showNextNotification() {
	// Show only one notification at a time in Unity / Notify OSD.
	if (_poorSupported) {
		for (auto b = _notifications.begin(); !_notifications.isEmpty() && b->isEmpty();) {
			_notifications.erase(b);
		}
		if (!_notifications.isEmpty()) {
			return;
		}
	}

	QueuedNotification data;
	while (!_queuedNotifications.isEmpty()) {
		data = _queuedNotifications.front();
		_queuedNotifications.pop_front();
		if (data.peer) {
			break;
		}
	}
	if (!data.peer) {
		return;
	}

	auto peerId = data.peer->id;
	auto msgId = data.msgId;
	auto notification = MakeShared<NotificationData>(data.title, data.body, peerId, msgId);
	if (!notification->valid()) {
		return;
	}

	StorageKey key;
	if (data.showUserpic) {
		key = data.peer->userpicUniqueKey();
	} else {
		key = StorageKey(0, 0);
	}
	notification->setImage(_cachedUserpics.get(key, data.peer));

	auto i = _notifications.find(peerId);
	if (i != _notifications.cend()) {
		auto j = i->find(msgId);
		if (j != i->cend()) {
			auto oldNotification = j.value();
			i->erase(j);
			oldNotification->close();
			i = _notifications.find(peerId);
		}
	}
	if (i == _notifications.cend()) {
		i = _notifications.insert(peerId, QMap<MsgId, Notification>());
	}
	_notifications[peerId].insert(msgId, notification);
	if (!notification->show()) {
		i = _notifications.find(peerId);
		if (i != _notifications.cend()) {
			i->remove(msgId);
			if (i->isEmpty()) _notifications.erase(i);
		}
		showNextNotification();
	}
}

void Manager::Impl::clearAll() {
	_queuedNotifications.clear();

	auto temp = createAndSwap(_notifications);
	for_const (auto &notifications, temp) {
		for_const (auto notification, notifications) {
			notification->close();
		}
	}
}

void Manager::Impl::clearFromHistory(History *history) {
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.end();) {
		if (i->peer == history->peer) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}

	auto i = _notifications.find(history->peer->id);
	if (i != _notifications.cend()) {
		auto temp = createAndSwap(i.value());
		_notifications.erase(i);

		for_const (auto notification, temp) {
			notification->close();
		}
	}

	showNextNotification();
}

void Manager::Impl::clearNotification(PeerId peerId, MsgId msgId) {
	auto i = _notifications.find(peerId);
	if (i != _notifications.cend()) {
		i.value().remove(msgId);
		if (i.value().isEmpty()) {
			_notifications.erase(i);
		}
	}

	showNextNotification();
}

Manager::Manager() : _impl(std_::make_unique<Impl>()) {
}

void Manager::clearNotification(PeerId peerId, MsgId msgId) {
	_impl->clearNotification(peerId, msgId);
}

bool Manager::hasPoorSupport() const {
	return _impl->hasPoorSupport();
}

bool Manager::hasActionsSupport() const {
	return _impl->hasActionsSupport();
}

Manager::~Manager() = default;

void Manager::doShowNativeNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton) {
	_impl->showNotification(peer, msgId, title, subtitle, showUserpic, msg, showReplyButton);
}

void Manager::doClearAllFast() {
	_impl->clearAll();
}

void Manager::doClearFromHistory(History *history) {
	_impl->clearFromHistory(history);
}

} // namespace Notifications
} // namespace Platform
