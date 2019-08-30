/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/notifications_manager_mac.h"

#include "platform/platform_specific.h"
#include "platform/platform_info.h"
#include "platform/mac/mac_utilities.h"
#include "history/history.h"
#include "ui/empty_userpic.h"
#include "mainwindow.h"
#include "styles/style_window.h"

#include <thread>
#include <Cocoa/Cocoa.h>

namespace {

static constexpr auto kQuerySettingsEachMs = 1000;
auto DoNotDisturbEnabled = false;
auto LastSettingsQueryMs = 0;

void queryDoNotDisturbState() {
	auto ms = crl::now();
	if (LastSettingsQueryMs > 0 && ms <= LastSettingsQueryMs + kQuerySettingsEachMs) {
		return;
	}
	LastSettingsQueryMs = ms;

	auto userDefaults = [NSUserDefaults alloc];
	if ([userDefaults respondsToSelector:@selector(initWithSuiteName:)]) {
		id userDefaultsValue = [[[NSUserDefaults alloc] initWithSuiteName:@"com.apple.notificationcenterui_test"] objectForKey:@"doNotDisturb"];
		DoNotDisturbEnabled = ([userDefaultsValue boolValue] == YES);
	} else {
		DoNotDisturbEnabled = false;
	}
}

using Manager = Platform::Notifications::Manager;

} // namespace

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

@interface NotificationDelegate : NSObject<NSUserNotificationCenterDelegate> {
}

- (id) initWithManager:(base::weak_ptr<Manager>)manager managerId:(uint64)managerId;
- (void) userNotificationCenter:(NSUserNotificationCenter*)center didActivateNotification:(NSUserNotification*)notification;
- (BOOL) userNotificationCenter:(NSUserNotificationCenter*)center shouldPresentNotification:(NSUserNotification*)notification;

@end // @interface NotificationDelegate

@implementation NotificationDelegate {
	base::weak_ptr<Manager> _manager;
	uint64 _managerId;

}

- (id) initWithManager:(base::weak_ptr<Manager>)manager managerId:(uint64)managerId {
	if (self = [super init]) {
		_manager = manager;
		_managerId = managerId;
	}
	return self;
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification {
	NSDictionary *notificationUserInfo = [notification userInfo];
	NSNumber *managerIdObject = [notificationUserInfo objectForKey:@"manager"];
	auto notificationManagerId = managerIdObject ? [managerIdObject unsignedLongLongValue] : 0ULL;
	DEBUG_LOG(("Received notification with instance %1, mine: %2").arg(notificationManagerId).arg(_managerId));
	if (notificationManagerId != _managerId) { // other app instance notification
		crl::on_main([] {
			// Usually we show and activate main window when the application
			// is activated (receives applicationDidBecomeActive: notification).
			//
			// This is used for window show in Cmd+Tab switching to the application.
			//
			// But when a notification arrives sometimes macOS still activates the app
			// and we receive applicationDidBecomeActive: notification even if the
			// notification was sent by another instance of the application. In that case
			// we set a flag for a couple of seconds to ignore this app activation.
			objc_ignoreApplicationActivationRightNow();
		});
		return;
	}

	NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"];
	auto notificationPeerId = peerObject ? [peerObject unsignedLongLongValue] : 0ULL;
	if (!notificationPeerId) {
		LOG(("App Error: A notification with unknown peer was received"));
		return;
	}

	NSNumber *msgObject = [notificationUserInfo objectForKey:@"msgid"];
	auto notificationMsgId = msgObject ? [msgObject intValue] : 0;
	if (notification.activationType == NSUserNotificationActivationTypeReplied) {
		const auto notificationReply = QString::fromUtf8([[[notification response] string] UTF8String]);
		const auto manager = _manager;
		crl::on_main(manager, [=] {
			manager->notificationReplied(
				notificationPeerId,
				notificationMsgId,
				{ notificationReply, {} });
		});
	} else if (notification.activationType == NSUserNotificationActivationTypeContentsClicked) {
		const auto manager = _manager;
		crl::on_main(manager, [=] {
			manager->notificationActivated(notificationPeerId, notificationMsgId);
		});
	}

	[center removeDeliveredNotification: notification];
}

- (BOOL) userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification {
	return YES;
}

@end // @implementation NotificationDelegate

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
	return Platform::IsMac10_8OrGreater();
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

class Manager::Private : public QObject, private base::Subscriber {
public:
	Private(Manager *manager);

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
	void updateDelegate();

	~Private();

private:
	template <typename Task>
	void putClearTask(Task task);

	void clearingThreadLoop();

	const uint64 _managerId = 0;
	QString _managerIdString;

	NotificationDelegate *_delegate = nullptr;

	std::thread _clearingThread;
	std::mutex _clearingMutex;
	std::condition_variable _clearingCondition;

	struct ClearFromHistory {
		PeerId peerId;
	};
	struct ClearAll {
	};
	struct ClearFinish {
	};
	using ClearTask = base::variant<ClearFromHistory, ClearAll, ClearFinish>;
	std::vector<ClearTask> _clearingTasks;

};

Manager::Private::Private(Manager *manager)
: _managerId(rand_value<uint64>())
, _managerIdString(QString::number(_managerId))
, _delegate([[NotificationDelegate alloc] initWithManager:manager managerId:_managerId]) {
	updateDelegate();
	subscribe(Global::RefWorkMode(), [this](DBIWorkMode mode) {
		// We need to update the delegate _after_ the tray icon change was done in Qt.
		// Because Qt resets the delegate.
		crl::on_main(this, [=] {
			updateDelegate();
		});
	});
}

void Manager::Private::showNotification(
		not_null<PeerData*> peer,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	@autoreleasepool {

	NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
	if ([notification respondsToSelector:@selector(setIdentifier:)]) {
		auto identifier = _managerIdString + '_' + QString::number(peer->id) + '_' + QString::number(msgId);
		auto identifierValue = Q2NSString(identifier);
		[notification setIdentifier:identifierValue];
	}
	[notification setUserInfo:[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:peer->id],@"peer",[NSNumber numberWithInt:msgId],@"msgid",[NSNumber numberWithUnsignedLongLong:_managerId],@"manager",nil]];

	[notification setTitle:Q2NSString(title)];
	[notification setSubtitle:Q2NSString(subtitle)];
	[notification setInformativeText:Q2NSString(msg)];
	if (!hideNameAndPhoto && [notification respondsToSelector:@selector(setContentImage:)]) {
		auto userpic = peer->isSelf()
			? Ui::EmptyUserpic::GenerateSavedMessages(st::notifyMacPhotoSize)
			: peer->genUserpic(st::notifyMacPhotoSize);
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

void Manager::Private::clearingThreadLoop() {
	auto finished = false;
	while (!finished) {
		auto clearAll = false;
		auto clearFromPeers = std::set<PeerId>(); // Better to use flatmap.
		{
			std::unique_lock<std::mutex> lock(_clearingMutex);

			while (_clearingTasks.empty()) {
				_clearingCondition.wait(lock);
			}
			for (auto &task : _clearingTasks) {
				if (base::get_if<ClearFinish>(&task)) {
					finished = true;
					clearAll = true;
				} else if (base::get_if<ClearAll>(&task)) {
					clearAll = true;
				} else if (auto fromHistory = base::get_if<ClearFromHistory>(&task)) {
					clearFromPeers.insert(fromHistory->peerId);
				}
			}
			_clearingTasks.clear();
		}

		auto clearByPeer = [&clearFromPeers](NSDictionary *notificationUserInfo) {
			if (NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"]) {
				auto notificationPeerId = [peerObject unsignedLongLongValue];
				if (notificationPeerId) {
					return (clearFromPeers.find(notificationPeerId) != clearFromPeers.cend());
				}
			}
			return true;
		};

		NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
		NSArray *notificationsList = [center deliveredNotifications];
		for (id notification in notificationsList) {
			NSDictionary *notificationUserInfo = [notification userInfo];
			NSNumber *managerIdObject = [notificationUserInfo objectForKey:@"manager"];
			auto notificationManagerId = managerIdObject ? [managerIdObject unsignedLongLongValue] : 0ULL;
			if (notificationManagerId == _managerId) {
				if (clearAll || clearByPeer(notificationUserInfo)) {
					[center removeDeliveredNotification:notification];
				}
			}
		}
	}
}

template <typename Task>
void Manager::Private::putClearTask(Task task) {
	if (!_clearingThread.joinable()) {
		_clearingThread = std::thread([this] { clearingThreadLoop(); });
	}

	std::unique_lock<std::mutex> lock(_clearingMutex);
	_clearingTasks.push_back(task);
	_clearingCondition.notify_one();
}

void Manager::Private::clearAll() {
	putClearTask(ClearAll());
}

void Manager::Private::clearFromHistory(not_null<History*> history) {
	putClearTask(ClearFromHistory { history->peer->id });
}

void Manager::Private::updateDelegate() {
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center setDelegate:_delegate];
}

Manager::Private::~Private() {
	if (_clearingThread.joinable()) {
		putClearTask(ClearFinish());
		_clearingThread.join();
	}
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center setDelegate:nil];
	[_delegate release];
}

Manager::Manager(Window::Notifications::System *system) : NativeManager(system)
, _private(std::make_unique<Private>(this)) {
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

} // namespace Notifications
} // namespace Platform
