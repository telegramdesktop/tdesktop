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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "platform/mac/notifications_manager_mac.h"

#include "platform/platform_specific.h"
#include "platform/mac/mac_utilities.h"
#include "styles/style_window.h"
#include "mainwindow.h"
#include "core/task_queue.h"

#include <Cocoa/Cocoa.h>

namespace {

static constexpr auto kQuerySettingsEachMs = 1000;
auto DoNotDisturbEnabled = false;
auto LastSettingsQueryMs = 0;

void queryDoNotDisturbState() {
	auto ms = getms(true);
	if (LastSettingsQueryMs > 0 && ms <= LastSettingsQueryMs + kQuerySettingsEachMs) {
		return;
	}
	LastSettingsQueryMs = ms;

	id userDefaultsValue = [[[NSUserDefaults alloc] initWithSuiteName:@"com.apple.notificationcenterui"] objectForKey:@"doNotDisturb"];
	DoNotDisturbEnabled = [userDefaultsValue boolValue];
}

using Manager = Platform::Notifications::Manager;

} // namespace

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

@interface NotificationDelegate : NSObject<NSUserNotificationCenterDelegate> {
}

- (id) initWithManager:(std::shared_ptr<Manager*>)manager;
- (void)userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification;
- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification;

@end

@implementation NotificationDelegate {

std::weak_ptr<Manager*> _manager;

}

- (id) initWithManager:(std::shared_ptr<Manager*>)manager {
	if (self = [super init]) {
		_manager = manager;
	}
	return self;
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification {
	NSDictionary *notificationUserInfo = [notification userInfo];
	NSNumber *launchIdObject = [notificationUserInfo objectForKey:@"launch"];
	auto notificationLaunchId = launchIdObject ? [launchIdObject unsignedLongLongValue] : 0ULL;
	DEBUG_LOG(("Received notification with instance %1").arg(notificationLaunchId));
	if (notificationLaunchId != Global::LaunchId()) { // other app instance notification
		return;
	}

	NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"];
	auto notificationPeerId = peerObject ? [peerObject unsignedLongLongValue] : 0ULL;
	if (!notificationPeerId) {
		return;
	}

	NSNumber *msgObject = [notificationUserInfo objectForKey:@"msgid"];
	auto notificationMsgId = msgObject ? [msgObject intValue] : 0;
	if (notification.activationType == NSUserNotificationActivationTypeReplied) {
		auto notificationReply = QString::fromUtf8([[[notification response] string] UTF8String]);
		if (auto manager = _manager.lock()) {
			(*manager)->notificationReplied(notificationPeerId, notificationMsgId, notificationReply);
		}
	} else if (notification.activationType == NSUserNotificationActivationTypeContentsClicked) {
		if (auto manager = _manager.lock()) {
			(*manager)->notificationActivated(notificationPeerId, notificationMsgId);
		}
	}

	[center removeDeliveredNotification: notification];
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification {
	return YES;
}

@end

namespace Platform {
namespace Notifications {

bool SkipAudio() {
	queryDoNotDisturbState();
	return DoNotDisturbEnabled;
}

bool SkipToast() {
	if (Supported()) {
		// Do not skip native notifications because of Do not disturb.
		// They respect this setting anyway.
		return false;
	}
	queryDoNotDisturbState();
	return DoNotDisturbEnabled;
}

bool Supported() {
	return (cPlatform() != dbipMacOld);
}

std::unique_ptr<Window::Notifications::Manager> Create(Window::Notifications::System *system) {
	if (Supported()) {
		return std::make_unique<Manager>(system);
	}
	return nullptr;
}

void FlashBounce() {
	[NSApp requestUserAttention:NSInformationalRequest];
}

void CustomNotificationShownHook(QWidget *widget) {
	widget->hide();
	objc_holdOnTop(widget->winId());
	widget->show();
	psShowOverAll(widget, false);
	if (auto window = App::wnd()) {
		window->customNotificationCreated(widget);
	}
}

class Manager::Private : public QObject, private base::Subscriber {
public:
	Private(Manager *manager);
	void showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton);
	void clearAll();
	void clearFromHistory(History *history);
	void updateDelegate();

	~Private();

private:
	std::shared_ptr<Manager*> _guarded;
	NotificationDelegate *_delegate = nullptr;

};

Manager::Private::Private(Manager *manager)
: _guarded(std::make_shared<Manager*>(manager))
, _delegate([[NotificationDelegate alloc] initWithManager:_guarded]) {
	updateDelegate();
	subscribe(Global::RefWorkMode(), [this](DBIWorkMode mode) {
		// We need to update the delegate _after_ the tray icon change was done in Qt.
		// Because Qt resets the delegate.
		base::TaskQueue::Main().Put(base::lambda_guarded(this, [this] {
			updateDelegate();
		}));
	});
}

void Manager::Private::showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton) {
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
	if (!hideNameAndPhoto && [notification respondsToSelector:@selector(setContentImage:)]) {
		auto userpic = peer->genUserpic(st::notifyMacPhotoSize);
		NSImage *img = [qt_mac_create_nsimage(userpic) autorelease];
		[notification setContentImage:img];
	}

	if (!hideReplyButton && [notification respondsToSelector:@selector(setHasReplyButton:)]) {
		[notification setHasReplyButton:YES];
	}

	[notification setSoundName:nil];

	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center deliverNotification:notification];

	}
}

void Manager::Private::clearAll() {
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	NSArray *notificationsList = [center deliveredNotifications];
	for (id notification in notificationsList) {
		NSDictionary *notificationUserInfo = [notification userInfo];
		NSNumber *launchIdObject = [notificationUserInfo objectForKey:@"launch"];
		auto notificationLaunchId = launchIdObject ? [launchIdObject unsignedLongLongValue] : 0ULL;
		if (notificationLaunchId == Global::LaunchId()) {
			[center removeDeliveredNotification:notification];
		}
	}
	[center removeAllDeliveredNotifications];
}

void Manager::Private::clearFromHistory(History *history) {
	unsigned long long peerId = history->peer->id;

	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	NSArray *notificationsList = [center deliveredNotifications];
	for (id notification in notificationsList) {
		NSDictionary *notificationUserInfo = [notification userInfo];
		NSNumber *launchIdObject = [notificationUserInfo objectForKey:@"launch"];
		NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"];
		auto notificationLaunchId = launchIdObject ? [launchIdObject unsignedLongLongValue] : 0ULL;
		auto notificationPeerId = peerObject ? [peerObject unsignedLongLongValue] : 0ULL;
		if (notificationPeerId == peerId && notificationLaunchId == Global::LaunchId()) {
			[center removeDeliveredNotification:notification];
		}
	}
}

void Manager::Private::updateDelegate() {
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center setDelegate:_delegate];
}

Manager::Private::~Private() {
	clearAll();
	[_delegate release];
}

Manager::Manager(Window::Notifications::System *system) : NativeManager(system)
, _private(std::make_unique<Private>(this)) {
}

Manager::~Manager() = default;

void Manager::doShowNativeNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton) {
	_private->showNotification(peer, msgId, title, subtitle, msg, hideNameAndPhoto, hideReplyButton);
}

void Manager::doClearAllFast() {
	_private->clearAll();
}

void Manager::doClearFromHistory(History *history) {
	_private->clearFromHistory(history);
}

} // namespace Notifications
} // namespace Platform
