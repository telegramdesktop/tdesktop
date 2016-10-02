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
#include "platform/mac/mac_utilities.h"

#include <Cocoa/Cocoa.h>

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

namespace Platform {
namespace Notifications {
namespace {

NeverFreedPointer<Manager> ManagerInstance;

} // namespace

void start() {
	if (cPlatform() != dbipMacOld) {
		ManagerInstance.makeIfNull();
	}
}

Window::Notifications::Manager *manager() {
	return ManagerInstance.data();
}

void finish() {
	ManagerInstance.clear();
}

void defaultNotificationShown(QWidget *widget) {
	widget->hide();
	objc_holdOnTop(widget->winId());
	widget->show();
	psShowOverAll(widget, false);
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
	@autoreleasepool {

	NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
	if ([notification respondsToSelector:@selector(setIdentifier:)]) {
		auto identifier = QString::number(Global::LaunchId()) + '_' + QString::number(peer->id) + '_' + QString::number(msgId);
		auto identifierValue = Q2NSString(identifier);
		[notification setIdentifier:identifierValue];
	}
	[notification setUserInfo:[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:peer->id],@"peer",[NSNumber numberWithInt:msgId],@"msgid",[NSNumber numberWithUnsignedLongLong:Global::LaunchId()],@"launch",nil]];

	[notification setTitle:Q2NSString(title)];
	[notification setSubtitle:Q2NSString(subtitle)];
	[notification setInformativeText:Q2NSString(msg)];
	if (showUserpic && [notification respondsToSelector:@selector(setContentImage:)]) {
		auto userpic = peer->genUserpic(st::notifyMacPhotoSize);
		NSImage *img = [qt_mac_create_nsimage(userpic) autorelease];
		[notification setContentImage:img];
	}

	if (showReplyButton && [notification respondsToSelector:@selector(setHasReplyButton:)]) {
		[notification setHasReplyButton:YES];
	}

	[notification setSoundName:nil];

	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center deliverNotification:notification];

	}
}

void Manager::Impl::clearAll() {
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	NSArray *notificationsList = [center deliveredNotifications];
	for (id notify in notificationsList) {
		NSDictionary *notifyUserInfo = [notify userInfo];
		auto notifyLaunchId = [[notifyUserInfo objectForKey:@"launch"] unsignedLongLongValue];
		if (notifyLaunchId == Global::LaunchId()) {
			[center removeDeliveredNotification:notify];
		}
	}
	[center removeAllDeliveredNotifications];
}

void Manager::Impl::clearFromHistory(History *history) {
	unsigned long long peerId = history->peer->id;

	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	NSArray *notificationsList = [center deliveredNotifications];
	for (id notify in notificationsList) {
		NSDictionary *notifyUserInfo = [notify userInfo];
		auto notifyPeerId = [[notifyUserInfo objectForKey:@"peer"] unsignedLongLongValue];
		auto notifyLaunchId = [[notifyUserInfo objectForKey:@"launch"] unsignedLongLongValue];
		if (notifyPeerId == peerId && notifyLaunchId == Global::LaunchId()) {
			[center removeDeliveredNotification:notify];
		}
	}
}

Manager::Impl::~Impl() {
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
