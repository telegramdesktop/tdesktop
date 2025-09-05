/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/notifications_manager_mac.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "base/platform/base_platform_info.h"
#include "platform/platform_specific.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "base/random.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_sublist.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/empty_userpic.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "window/notifications_utilities.h"
#include "styles/style_window.h"

#include <thread>
#include <Cocoa/Cocoa.h>

namespace {

constexpr auto kQuerySettingsEachMs = crl::time(1000);

crl::time LastSettingsQueryMs/* = 0*/;
bool DoNotDisturbEnabled/* = false*/;

[[nodiscard]] bool ShouldQuerySettings() {
	const auto now = crl::now();
	if (LastSettingsQueryMs > 0 && now <= LastSettingsQueryMs + kQuerySettingsEachMs) {
		return false;
	}
	LastSettingsQueryMs = now;
	return true;
}

[[nodiscard]] QString LibraryPath() {
	static const auto result = [] {
		NSURL *url = [[NSFileManager defaultManager] URLForDirectory:NSLibraryDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:NO error:nil];
		return url
			? QString::fromUtf8([[url path] fileSystemRepresentation])
			: QString();
	}();
	return result;
}

void queryDoNotDisturbState() {
	if (!ShouldQuerySettings()) {
		return;
	}
	Boolean isKeyValid;
	const auto doNotDisturb = CFPreferencesGetAppBooleanValue(
		CFSTR("doNotDisturb"),
		CFSTR("com.apple.notificationcenterui"),
		&isKeyValid);
	DoNotDisturbEnabled = isKeyValid
		? doNotDisturb
		: false;
}

using Manager = Platform::Notifications::Manager;

} // namespace

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

	NSNumber *sessionObject = [notificationUserInfo objectForKey:@"session"];
	const auto notificationSessionId = sessionObject ? [sessionObject unsignedLongLongValue] : 0;
	if (!notificationSessionId) {
		LOG(("App Error: A notification with unknown session was received"));
		return;
	}
	NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"];
	const auto notificationPeerId = peerObject ? [peerObject unsignedLongLongValue] : 0ULL;
	if (!notificationPeerId) {
		LOG(("App Error: A notification with unknown peer was received"));
		return;
	}
	NSNumber *topicObject = [notificationUserInfo objectForKey:@"topic"];
	if (!topicObject) {
		LOG(("App Error: A notification with unknown topic was received"));
		return;
	}
	const auto notificationTopicRootId = [topicObject longLongValue];
	NSNumber *monoforumPeerObject = [notificationUserInfo objectForKey:@"monoforumpeer"];
	if (!monoforumPeerObject) {
		LOG(("App Error: A notification with unknown monoforum peer was received"));
		return;
	}
	const auto notificationMonoforumPeerId = [monoforumPeerObject unsignedLongLongValue];

	NSNumber *msgObject = [notificationUserInfo objectForKey:@"msgid"];
	const auto notificationMsgId = msgObject ? [msgObject longLongValue] : 0LL;

	const auto my = Window::Notifications::Manager::NotificationId{
		.contextId = Manager::ContextId{
			.sessionId = notificationSessionId,
			.peerId = PeerId(notificationPeerId),
			.topicRootId = MsgId(notificationTopicRootId),
			.monoforumPeerId = PeerId(notificationMonoforumPeerId),
		},
		.msgId = notificationMsgId,
	};
	if (notification.activationType == NSUserNotificationActivationTypeReplied) {
		const auto notificationReply = QString::fromUtf8([[[notification response] string] UTF8String]);
		const auto manager = _manager;
		crl::on_main(manager, [=] {
			manager->notificationReplied(my, { notificationReply, {} });
		});
	} else if (notification.activationType == NSUserNotificationActivationTypeContentsClicked) {
		const auto manager = _manager;
		crl::on_main(manager, [=] {
			manager->notificationActivated(my);
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

bool SkipToastForCustom() {
	return false;
}

void MaybePlaySoundForCustom(Fn<void()> playSound) {
	playSound();
}

void MaybeFlashBounceForCustom(Fn<void()> flashBounce) {
	flashBounce();
}

bool WaitForInputForCustom() {
	return true;
}

bool Supported() {
	return true;
}

bool Enforced() {
	return Supported();
}

bool ByDefault() {
	return Supported();
}

bool VolumeSupported() {
	return false;
}

void Create(Window::Notifications::System *system) {
	system->setManager([=] { return std::make_unique<Manager>(system); });
}

class Manager::Private : public QObject {
public:
	Private(Manager *manager);

	void showNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView);
	void clearAll();
	void clearFromItem(not_null<HistoryItem*> item);
	void clearFromTopic(not_null<Data::ForumTopic*> topic);
	void clearFromSublist(not_null<Data::SavedSublist*> sublist);
	void clearFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
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

	struct ClearFromItem {
		NotificationId id;
	};
	struct ClearFromTopic {
		ContextId contextId;
	};
	struct ClearFromSublist {
		ContextId contextId;
	};
	struct ClearFromHistory {
		ContextId partialContextId;
	};
	struct ClearFromSession {
		uint64 sessionId = 0;
	};
	struct ClearAll {
	};
	struct ClearFinish {
	};
	using ClearTask = std::variant<
		ClearFromItem,
		ClearFromTopic,
		ClearFromSublist,
		ClearFromHistory,
		ClearFromSession,
		ClearAll,
		ClearFinish>;
	std::vector<ClearTask> _clearingTasks;

	Media::Audio::LocalDiskCache _sounds;

	rpl::lifetime _lifetime;

};

[[nodiscard]] QString ResolveSoundsFolder() {
	NSArray *paths = NSSearchPathForDirectoriesInDomains(
		NSLibraryDirectory,
		NSUserDomainMask,
		YES);
	NSString *library = [paths firstObject];
	NSString *sounds = [library stringByAppendingPathComponent : @"Sounds"];
	return NS2QString(sounds);
}

Manager::Private::Private(Manager *manager)
: _managerId(base::RandomValue<uint64>())
, _managerIdString(QString::number(_managerId))
, _delegate([[NotificationDelegate alloc] initWithManager:manager managerId:_managerId])
, _sounds(ResolveSoundsFolder()) {
	Core::App().settings().workModeValue(
	) | rpl::start_with_next([=](Core::Settings::WorkMode mode) {
		// We need to update the delegate _after_ the tray icon change was done in Qt.
		// Because Qt resets the delegate.
		crl::on_main(this, [=] {
			updateDelegate();
		});
	}, _lifetime);
}

void Manager::Private::showNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) {
	@autoreleasepool {

	const auto peer = info.peer;
	NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
	if ([notification respondsToSelector:@selector(setIdentifier:)]) {
		auto identifier = _managerIdString
			+ '_'
			+ QString::number(peer->id.value)
			+ '_'
			+ QString::number(info.itemId.bare);
		auto identifierValue = Q2NSString(identifier);
		[notification setIdentifier:identifierValue];
	}
	[notification setUserInfo:
		[NSDictionary dictionaryWithObjectsAndKeys:
			[NSNumber numberWithUnsignedLongLong:peer->session().uniqueId()],
			@"session",
			[NSNumber numberWithUnsignedLongLong:peer->id.value],
			@"peer",
			[NSNumber numberWithLongLong:info.topicRootId.bare],
			@"topic",
			[NSNumber numberWithUnsignedLongLong:info.monoforumPeerId.value],
			@"monoforumpeer",
			[NSNumber numberWithLongLong:info.itemId.bare],
			@"msgid",
			[NSNumber numberWithUnsignedLongLong:_managerId],
			@"manager",
			nil]];

	[notification setTitle:Q2NSString(info.title)];
	[notification setSubtitle:Q2NSString(info.subtitle)];
	[notification setInformativeText:Q2NSString(info.message)];
	if (!info.options.hideNameAndPhoto
		&& [notification respondsToSelector:@selector(setContentImage:)]) {
		NSImage *img = Q2NSImage(
			Window::Notifications::GenerateUserpic(peer, userpicView));
		[notification setContentImage:img];
	}

	if (!info.options.hideReplyButton
		&& [notification respondsToSelector:@selector(setHasReplyButton:)]) {
		[notification setHasReplyButton:YES];
	}

	const auto sound = info.sound ? info.sound() : Media::Audio::LocalSound();
	if (sound) {
		[notification setSoundName:Q2NSString(_sounds.name(sound))];
	} else {
		[notification setSoundName:nil];
	}

	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center deliverNotification:notification];

	}
}

void Manager::Private::clearingThreadLoop() {
	auto finished = false;
	while (!finished) {
		auto clearAll = false;
		auto clearFromItems = base::flat_set<NotificationId>();
		auto clearFromTopics = base::flat_set<ContextId>();
		auto clearFromSublists = base::flat_set<ContextId>();
		auto clearFromHistories = base::flat_set<ContextId>();
		auto clearFromSessions = base::flat_set<uint64>();
		{
			std::unique_lock<std::mutex> lock(_clearingMutex);
			while (_clearingTasks.empty()) {
				_clearingCondition.wait(lock);
			}
			for (auto &task : _clearingTasks) {
				v::match(task, [&](ClearFinish) {
					finished = true;
					clearAll = true;
				}, [&](ClearAll) {
					clearAll = true;
				}, [&](const ClearFromItem &value) {
					clearFromItems.emplace(value.id);
				}, [&](const ClearFromTopic &value) {
					clearFromTopics.emplace(value.contextId);
				}, [&](const ClearFromSublist &value) {
					clearFromSublists.emplace(value.contextId);
				}, [&](const ClearFromHistory &value) {
					clearFromHistories.emplace(value.partialContextId);
				}, [&](const ClearFromSession &value) {
					clearFromSessions.emplace(value.sessionId);
				});
			}
			_clearingTasks.clear();
		}

		@autoreleasepool {

		auto clearBySpecial = [&](NSDictionary *notificationUserInfo) {
			NSNumber *sessionObject = [notificationUserInfo objectForKey:@"session"];
			const auto notificationSessionId = sessionObject ? [sessionObject unsignedLongLongValue] : 0;
			if (!notificationSessionId) {
				return true;
			}
			NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"];
			const auto notificationPeerId = peerObject ? [peerObject unsignedLongLongValue] : 0;
			if (!notificationPeerId) {
				return true;
			}
			NSNumber *topicObject = [notificationUserInfo objectForKey:@"topic"];
			if (!topicObject) {
				return true;
			}
			const auto notificationTopicRootId = [topicObject longLongValue];
			NSNumber *monoforumPeerObject = [notificationUserInfo objectForKey:@"monoforumpeer"];
			if (!monoforumPeerObject) {
				return true;
			}
			const auto notificationMonoforumPeerId = [monoforumPeerObject unsignedLongLongValue];
			NSNumber *msgObject = [notificationUserInfo objectForKey:@"msgid"];
			const auto msgId = msgObject ? [msgObject longLongValue] : 0LL;
			const auto partialContextId = ContextId{
				.sessionId = notificationSessionId,
				.peerId = PeerId(notificationPeerId),
			};
			const auto contextId = notificationTopicRootId
			? ContextId{
				.sessionId = notificationSessionId,
				.peerId = PeerId(notificationPeerId),
				.topicRootId = MsgId(notificationTopicRootId),
			}
			: notificationMonoforumPeerId
			? ContextId{
				.sessionId = notificationSessionId,
				.peerId = PeerId(notificationPeerId),
				.monoforumPeerId = PeerId(notificationMonoforumPeerId),
			}
			: partialContextId;
			const auto id = NotificationId{ contextId, MsgId(msgId) };
			return clearFromSessions.contains(notificationSessionId)
				|| clearFromHistories.contains(partialContextId)
				|| clearFromTopics.contains(contextId)
				|| clearFromSublists.contains(contextId)
				|| (msgId && clearFromItems.contains(id));
		};

		NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
		NSArray *notificationsList = [center deliveredNotifications];
		for (id notification in notificationsList) {
			NSDictionary *notificationUserInfo = [notification userInfo];
			NSNumber *managerIdObject = [notificationUserInfo objectForKey:@"manager"];
			auto notificationManagerId = managerIdObject ? [managerIdObject unsignedLongLongValue] : 0ULL;
			if (notificationManagerId == _managerId) {
				if (clearAll || clearBySpecial(notificationUserInfo)) {
					[center removeDeliveredNotification:notification];
				}
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

void Manager::Private::clearFromItem(not_null<HistoryItem*> item) {
	putClearTask(ClearFromItem{ ContextId{
		.sessionId = item->history()->session().uniqueId(),
		.peerId = item->history()->peer->id,
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	}, item->id });
}

void Manager::Private::clearFromTopic(not_null<Data::ForumTopic*> topic) {
	putClearTask(ClearFromTopic{ ContextId{
		.sessionId = topic->session().uniqueId(),
		.peerId = topic->history()->peer->id,
		.topicRootId = topic->rootId(),
	} });
}

void Manager::Private::clearFromSublist(
		not_null<Data::SavedSublist*> sublist) {
	putClearTask(ClearFromSublist{ ContextId{
		.sessionId = sublist->session().uniqueId(),
		.peerId = sublist->owningHistory()->peer->id,
		.monoforumPeerId = sublist->sublistPeer()->id,
	} });
}

void Manager::Private::clearFromHistory(not_null<History*> history) {
	putClearTask(ClearFromHistory{ ContextId{
		.sessionId = history->session().uniqueId(),
		.peerId = history->peer->id,
	} });
}

void Manager::Private::clearFromSession(not_null<Main::Session*> session) {
	putClearTask(ClearFromSession{ session->uniqueId() });
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
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) {
	_private->showNotification(std::move(info), userpicView);
}

void Manager::doClearAllFast() {
	_private->clearAll();
}

void Manager::doClearFromItem(not_null<HistoryItem*> item) {
	_private->clearFromItem(item);
}

void Manager::doClearFromTopic(not_null<Data::ForumTopic*> topic) {
	_private->clearFromTopic(topic);
}

void Manager::doClearFromSublist(not_null<Data::SavedSublist*> sublist) {
	_private->clearFromSublist(sublist);
}

void Manager::doClearFromHistory(not_null<History*> history) {
	_private->clearFromHistory(history);
}

void Manager::doClearFromSession(not_null<Main::Session*> session) {
	_private->clearFromSession(session);
}

QString Manager::accountNameSeparator() {
	return QString::fromUtf8(" \xE2\x86\x92 ");
}

bool Manager::doSkipToast() const {
	return false;
}

void Manager::doMaybePlaySound(Fn<void()> playSound) {
	playSound();
}

void Manager::doMaybeFlashBounce(Fn<void()> flashBounce) {
	flashBounce();
}

} // namespace Notifications
} // namespace Platform
