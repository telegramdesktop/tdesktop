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

namespace Platform {
namespace Notifications {
namespace {

NeverFreedPointer<Manager> ManagerInstance;

bool supported() {
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
//		&& (Libs::notify_notification_set_timeout != nullptr)
//		&& (Libs::notify_notification_set_category != nullptr)
//		&& (Libs::notify_notification_set_urgency != nullptr)
//		&& (Libs::notify_notification_set_icon_from_pixbuf != nullptr)
		&& (Libs::notify_notification_set_image_from_pixbuf != nullptr)
//		&& (Libs::notify_notification_set_hint != nullptr)
//		&& (Libs::notify_notification_set_hint_int32 != nullptr)
//		&& (Libs::notify_notification_set_hint_uint32 != nullptr)
//		&& (Libs::notify_notification_set_hint_double != nullptr)
//		&& (Libs::notify_notification_set_hint_string != nullptr)
//		&& (Libs::notify_notification_set_hint_byte != nullptr)
//		&& (Libs::notify_notification_set_hint_byte_array != nullptr)
//		&& (Libs::notify_notification_clear_hints != nullptr)
//		&& (Libs::notify_notification_add_action != nullptr)
//		&& (Libs::notify_notification_clear_actions != nullptr)
		&& (Libs::notify_notification_close != nullptr)
		&& (Libs::notify_notification_get_closed_reason != nullptr)
		&& (Libs::g_object_unref != nullptr)
//		&& (Libs::gdk_pixbuf_new_from_data != nullptr)
		&& (Libs::gdk_pixbuf_new_from_file != nullptr);
}

} // namespace

void start() {
	if (supported()) {
		if (Libs::notify_is_initted() || Libs::notify_init("Telegram Desktop")) {
			ManagerInstance.makeIfNull();
		}
	}
}

Manager *manager() {
	return ManagerInstance.data();
}

void finish() {
	if (manager()) {
		ManagerInstance.reset();
		Libs::notify_uninit();
	}
}

class Manager::Impl {
public:
	void showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton);
	void clearAll();
	void clearFromHistory(History *history);

private:
	static void notificationClosed(Libs::NotifyNotification *notification, gpointer data);
	void clearNotification(Libs::NotifyNotification *notification);

	using Notifications = QMap<PeerId, QMap<MsgId, Libs::NotifyNotification*>>;
	Notifications _notifications;

	using NotificationsData = QMap<Libs::NotifyNotification*, QPair<PeerId, MsgId>>;
	NotificationsData _notificationsData;

	Window::Notifications::CachedUserpics _cachedUserpics;

};

void Manager::Impl::notificationClosed(Libs::NotifyNotification *notification, gpointer data) {
	if (auto manager = ManagerInstance.data()) {
		manager->_impl->clearNotification(notification);
	}
}

void Manager::Impl::clearNotification(Libs::NotifyNotification *notification) {
	auto dataIt = _notificationsData.find(notification);
	if (dataIt == _notificationsData.cend()) {
		return;
	}

	auto peerId = dataIt->first;
	auto msgId = dataIt->second;
	_notificationsData.erase(dataIt);

	auto i = _notifications.find(peerId);
	if (i != _notifications.cend()) {
		auto it = i.value().find(msgId);
		if (it != i.value().cend()) {
			Libs::g_object_unref(Libs::g_object_cast(it.value()));
			i.value().erase(it);
			if (i.value().isEmpty()) {
				_notifications.erase(i);
			}
		}
	}
}

void Manager::Impl::showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton) {
	auto titleUtf8 = title.toUtf8();
	auto bodyUtf8 = (subtitle.isEmpty() ? msg : ("<b>" + subtitle + "</b>\n" + msg)).toUtf8();
	auto notification = Libs::notify_notification_new(titleUtf8.constData(), bodyUtf8.constData(), nullptr);
	if (!notification) {
		return;
	}

	Libs::g_signal_connect_helper(Libs::g_object_cast(notification), "closed", G_CALLBACK(Manager::Impl::notificationClosed), peer);

	StorageKey key;
	if (showUserpic) {
		key = peer->userpicUniqueKey();
	} else {
		key = StorageKey(0, 0);
	}
	auto userpicPath = _cachedUserpics.get(key, peer);
	auto userpicPathNative = QFile::encodeName(userpicPath);
	if (auto pixbuf = Libs::gdk_pixbuf_new_from_file(userpicPathNative.constData(), nullptr)) {
		Libs::notify_notification_set_image_from_pixbuf(notification, pixbuf);
		Libs::g_object_unref(Libs::g_object_cast(pixbuf));
	}

	auto i = _notifications.find(peer->id);
	if (i != _notifications.cend()) {
		auto j = i->find(msgId);
		if (j != i->cend()) {
			auto oldNotification = j.value();
			i->erase(j);
			_notificationsData.remove(oldNotification);
			Libs::notify_notification_close(oldNotification, nullptr);
			Libs::g_object_unref(Libs::g_object_cast(oldNotification));
			i = _notifications.find(peer->id);
		}
	}
	if (i == _notifications.cend()) {
		i = _notifications.insert(peer->id, QMap<MsgId, Libs::NotifyNotification*>());
	}
	auto result = Libs::notify_notification_show(notification, nullptr);
	if (!result) {
		Libs::g_object_unref(Libs::g_object_cast(notification));
		i = _notifications.find(peer->id);
		if (i != _notifications.cend() && i->isEmpty()) _notifications.erase(i);
		return;
	}
	_notifications[peer->id].insert(msgId, notification);
	_notificationsData.insert(notification, qMakePair(peer->id, msgId));
}

void Manager::Impl::clearAll() {
	_notificationsData.clear();

	auto temp = createAndSwap(_notifications);
	for_const (auto &notifications, temp) {
		for_const (auto notification, notifications) {
			Libs::notify_notification_close(notification, nullptr);
			Libs::g_object_unref(Libs::g_object_cast(notification));
		}
	}
}

void Manager::Impl::clearFromHistory(History *history) {
	auto i = _notifications.find(history->peer->id);
	if (i != _notifications.cend()) {
		auto temp = createAndSwap(i.value());
		_notifications.erase(i);

		for_const (auto notification, temp) {
			_notificationsData.remove(notification);
			Libs::notify_notification_close(notification, nullptr);
			Libs::g_object_unref(Libs::g_object_cast(notification));
		}
	}
}

Manager::Manager() : _impl(std_::make_unique<Impl>()) {
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
