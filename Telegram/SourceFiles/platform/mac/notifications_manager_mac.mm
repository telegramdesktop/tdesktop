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
#include "platform/mac/notifications_manager_mac.h"

#include "pspecific.h"

namespace Platform {
namespace Notifications {

void start() {
}

Window::Notifications::Manager *manager() {
	return nullptr;
}

void finish() {
}

void defaultNotificationShown(QWidget *widget) {
	widget->hide();
	objc_holdOnTop(widget->winId());
	widget->show();
	psShowOverAll(w, false);
}

class Manager::Impl {
public:
	void showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton);
	void clearAll();
	void clearFromHistory(History *history);

	~Impl();

private:

};

void Manager::Impl::showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton) {
	auto notification = [[NSUserNotification alloc] init];

	[notification setUserInfo:[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:peer->id],@"peer",[NSNumber numberWithInt:msgId],@"msgid",[NSNumber numberWithUnsignedLongLong:Global::LaunchId()],@"launch",nil]];

	[notification setTitle:QNSString(title).s()];
	[notification setSubtitle:QNSString(subtitle).s()];
	[notification setInformativeText:QNSString(msg).s()];
	if (showUserpic && [notification respondsToSelector:@selector(setContentImage:)]) {
		auto userpic = peer->genUserpic(st::notifyMacPhotoSize);
		auto img = qt_mac_create_nsimage(userpic);
		[notification setContentImage:img];
		[img release];
	}

	if (showReplyButton && [notification respondsToSelector:@selector(setHasReplyButton:)]) {
		[notification setHasReplyButton:YES];
	}

	[notification setSoundName:nil];

	auto center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center deliverNotification:notification];

	[notification release];
}

void Manager::Impl::clearAll() {
	auto center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center removeAllDeliveredNotifications];
}

void Manager::Impl::clearFromHistory(History *history) {
	unsigned long long peerId = history->peer->id;

	auto center = [NSUserNotificationCenter defaultUserNotificationCenter];
	auto notificationsList = [center deliveredNotifications];
	for (id notify in notificationsList) {
		auto notifyUserInfo = [notify userInfo];
		auto notifyPeerId = [[notifyUserInfo objectForKey:@"peer"] unsignedLongLongValue];
		auto notifyLaunchId = [[notifyUserInfo objectForKey:@"launch"] unsignedLongLongValue];
		if (notifyPeerId == peerId && notifyLaunchId == Global::LaunchId()) {
			[center removeDeliveredNotification:notify];
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
