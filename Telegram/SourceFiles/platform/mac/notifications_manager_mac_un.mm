/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/notifications_manager_mac_un.h"

#include "base/options.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "base/random.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "history/history_item.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings.h"
#include "window/notifications_utilities.h"

#include <xxhash.h> // XXH64.

#include <Cocoa/Cocoa.h>
#include <UserNotifications/UserNotifications.h>

namespace {

using Manager = Platform::Notifications::UNManager;
using ContextId = Manager::ContextId;
using NotificationId = Manager::NotificationId;

struct NotificationParsed {
	uint64 sessionId = 0;
	uint64 peerId = 0;
	int64 topicRootId = 0;
	uint64 monoforumPeerId = 0;
	int64 msgId = 0;
	bool valid = false;
};

struct InFlightRequest {
	QString identifier;
	NotificationParsed parsed;
	bool removeWhenDone = false;
};

[[nodiscard]] NotificationParsed ParseNotification(NSDictionary *userInfo) {
	auto result = NotificationParsed();
	NSNumber *sessionObject = [userInfo objectForKey:@"session"];
	result.sessionId = sessionObject
		? [sessionObject unsignedLongLongValue]
		: 0;
	NSNumber *peerObject = [userInfo objectForKey:@"peer"];
	result.peerId = peerObject ? [peerObject unsignedLongLongValue] : 0;
	NSNumber *topicObject = [userInfo objectForKey:@"topic"];
	result.topicRootId = topicObject ? [topicObject longLongValue] : 0;
	NSNumber *monoforumPeerObject = [userInfo objectForKey:@"monoforumpeer"];
	result.monoforumPeerId = monoforumPeerObject
		? [monoforumPeerObject unsignedLongLongValue]
		: 0;
	NSNumber *msgObject = [userInfo objectForKey:@"msgid"];
	result.msgId = msgObject ? [msgObject longLongValue] : 0;
	result.valid = (result.sessionId != 0)
		&& (result.peerId != 0)
		&& (topicObject != nil)
		&& (monoforumPeerObject != nil);
	return result;
}

[[nodiscard]] uint64 ParseManagerId(NSDictionary *userInfo) {
	NSNumber *managerIdObject = [userInfo objectForKey:@"manager"];
	return managerIdObject ? [managerIdObject unsignedLongLongValue] : 0ULL;
}

[[nodiscard]] ContextId ParsedContextId(const NotificationParsed &parsed) {
	return parsed.topicRootId
		? ContextId{
			.sessionId = parsed.sessionId,
			.peerId = PeerId(parsed.peerId),
			.topicRootId = MsgId(parsed.topicRootId),
		}
		: parsed.monoforumPeerId
		? ContextId{
			.sessionId = parsed.sessionId,
			.peerId = PeerId(parsed.peerId),
			.monoforumPeerId = PeerId(parsed.monoforumPeerId),
		}
		: ContextId{
			.sessionId = parsed.sessionId,
			.peerId = PeerId(parsed.peerId),
		};
}

[[nodiscard]] QString ResolveSoundsFolder() {
	NSArray *paths = NSSearchPathForDirectoriesInDomains(
		NSLibraryDirectory,
		NSUserDomainMask,
		YES);
	NSString *library = [paths firstObject];
	NSString *sounds = [library stringByAppendingPathComponent:@"Sounds"];
	return Platform::NS2QString(sounds);
}

} // namespace

API_AVAILABLE(macos(10.14))
@interface UserNotificationsDelegate
	: NSObject<UNUserNotificationCenterDelegate> {
}

- (id) initWithManager:(base::weak_ptr<Manager>)manager
		managerId:(uint64)managerId;

@end // @interface UserNotificationsDelegate

@implementation UserNotificationsDelegate {
	base::weak_ptr<Manager> _manager;
	uint64 _managerId;

}

- (id) initWithManager:(base::weak_ptr<Manager>)manager
		managerId:(uint64)managerId {
	if (self = [super init]) {
		_manager = manager;
		_managerId = managerId;
	}
	return self;
}

- (void) userNotificationCenter:(UNUserNotificationCenter *)center
		willPresentNotification:(UNNotification *)notification
		withCompletionHandler:
			(void (^)(UNNotificationPresentationOptions))completionHandler {
	if (@available(macOS 11.0, *)) {
		completionHandler(UNNotificationPresentationOptionBanner
			| UNNotificationPresentationOptionList
			| UNNotificationPresentationOptionSound);
	} else {
		completionHandler(UNNotificationPresentationOptionAlert
			| UNNotificationPresentationOptionSound);
	}
}

- (void) userNotificationCenter:(UNUserNotificationCenter *)center
		didReceiveNotificationResponse:(UNNotificationResponse *)response
		withCompletionHandler:(void (^)(void))completionHandler {
	NSDictionary *userInfo = response.notification.request.content.userInfo;
	const auto notificationManagerId = ParseManagerId(userInfo);
	DEBUG_LOG(("Received UN notification with instance %1, mine: %2"
		).arg(notificationManagerId
		).arg(_managerId));
	if (notificationManagerId != _managerId) { // stale launch notification
		completionHandler();
		return;
	}
	const auto parsed = ParseNotification(userInfo);
	if (!parsed.valid) {
		LOG(("App Error: A UN notification with unknown data was received"));
		completionHandler();
		return;
	}
	const auto my = Window::Notifications::Manager::NotificationId{
		.contextId = ContextId{
			.sessionId = parsed.sessionId,
			.peerId = PeerId(parsed.peerId),
			.topicRootId = MsgId(parsed.topicRootId),
			.monoforumPeerId = PeerId(parsed.monoforumPeerId),
		},
		.msgId = parsed.msgId,
	};
	const auto manager = _manager;
	NSString *actionIdentifier = response.actionIdentifier;
	if ([response isKindOfClass:[UNTextInputNotificationResponse class]]) {
		const auto text = QString::fromNSString(
			((UNTextInputNotificationResponse*)response).userText);
		crl::on_main(manager, [=] {
			manager->notificationReplied(my, { text, {} });
		});
	} else if ([actionIdentifier
			isEqualToString:UNNotificationDefaultActionIdentifier]) {
		crl::on_main(manager, [=] {
			manager->notificationActivated(my);
		});
	} else if (![actionIdentifier
			isEqualToString:UNNotificationDismissActionIdentifier]) {
		const auto action = QString::fromNSString(actionIdentifier);
		crl::on_main(manager, [=] {
			manager->notificationActionActivated(my, action);
		});
	}
	completionHandler();
}

@end // @implementation UserNotificationsDelegate

namespace Platform {
namespace Notifications {
namespace {

[[nodiscard]] const base::options::toggle &UNManagerOption() {
	static const auto &result = base::options::lookup<bool>(
		Window::Notifications::kOptionMacModernNotifications);
	return result;
}

} // namespace

bool UseUNManager() {
	if (!UNManagerOption().value()) {
		return false;
	}
	if (@available(macOS 10.14, *)) {
		return [[NSBundle mainBundle] bundleIdentifier] != nil;
	}
	return false;
}

class UNManager::Private final {
public:
	Private(UNManager *manager);

	void showNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView);
	void clearAll();
	void clearFromItem(not_null<HistoryItem*> item);
	void clearFromTopic(not_null<Data::ForumTopic*> topic);
	void clearFromSublist(not_null<Data::SavedSublist*> sublist);
	void clearFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);

	~Private();

private:
	QString ensureCategory(
		bool withReply,
		bool withMarkAsRead,
		const std::vector<NotificationAction> &actions);
	void removeDelivered(Fn<bool(const NotificationParsed &)> filter);
	void finishRequest(const QString &identifier);

	const uint64 _managerId = 0;
	QString _managerIdString;
	const base::weak_ptr<UNManager> _manager;

	NSObject *_delegate = nil;
	NSMutableArray *_categories = nil;
	base::flat_set<QString> _categoryIds;
	std::vector<InFlightRequest> _inFlight;

	Media::Audio::LocalDiskCache _sounds;

};

UNManager::Private::Private(UNManager *manager)
: _managerId(base::RandomValue<uint64>())
, _managerIdString(QString::number(_managerId))
, _manager(manager)
, _sounds(ResolveSoundsFolder()) {
	QDir().mkpath(cWorkingDir() + u"tdata/temp"_q);
	if (@available(macOS 10.14, *)) {
		_delegate = [[UserNotificationsDelegate alloc]
			initWithManager:manager
			managerId:_managerId];
		_categories = [[NSMutableArray alloc] init];
		UNUserNotificationCenter *center
			= [UNUserNotificationCenter currentNotificationCenter];
		[center setDelegate:(id<UNUserNotificationCenterDelegate>)_delegate];
		[center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert
			| UNAuthorizationOptionSound)
			completionHandler:^(BOOL granted, NSError *error) {
				@autoreleasepool {

				if (error) {
					LOG(("App Error: Notifications authorization error: %1"
						).arg(NS2QString(error.localizedDescription)));
				} else if (!granted) {
					LOG(("App Warning: Notifications authorization denied."));
				}

				}
			}];
	}

	// Register the common categories, so that notifications left from
	// the previous launch keep their action buttons.
	ensureCategory(true, true, {});
	ensureCategory(true, false, {});
	ensureCategory(false, true, {});
}

QString UNManager::Private::ensureCategory(
		bool withReply,
		bool withMarkAsRead,
		const std::vector<NotificationAction> &actions) {
	if (!withReply && !withMarkAsRead && actions.empty()) {
		return QString();
	}
	const auto replyText = tr::lng_notification_reply(tr::now);
	const auto markAsReadText = tr::lng_context_mark_read(tr::now);
	auto signature = QString();
	if (withReply) {
		signature += u"reply:"_q + replyText + '\n';
	}
	if (withMarkAsRead) {
		signature += u"markAsRead:"_q + markAsReadText + '\n';
	}
	for (const auto &action : actions) {
		signature += action.id + ':' + action.text + '\n';
	}
	const auto hash = XXH64(
		signature.constData(),
		signature.size() * sizeof(QChar),
		0);
	const auto identifier = u"tdesktop."_q + QString::number(hash, 16);
	if (_categoryIds.contains(identifier)) {
		return identifier;
	}
	if (@available(macOS 10.14, *)) {
		@autoreleasepool {

		NSMutableArray *list = [NSMutableArray array];
		if (withReply) {
			[list addObject:[UNTextInputNotificationAction
				actionWithIdentifier:@"reply"
				title:Q2NSString(replyText)
				options:0
				textInputButtonTitle:Q2NSString(replyText)
				textInputPlaceholder:@""]];
		}
		if (withMarkAsRead) {
			[list addObject:[UNNotificationAction
				actionWithIdentifier:@"markAsRead"
				title:Q2NSString(markAsReadText)
				options:0]];
		}
		for (const auto &action : actions) {
			[list addObject:[UNNotificationAction
				actionWithIdentifier:Q2NSString(action.id)
				title:Q2NSString(action.text)
				options:0]];
		}
		UNNotificationCategory *category = [UNNotificationCategory
			categoryWithIdentifier:Q2NSString(identifier)
			actions:list
			intentIdentifiers:@[]
			options:0];
		[_categories addObject:category];
		[[UNUserNotificationCenter currentNotificationCenter]
			setNotificationCategories:[NSSet setWithArray:_categories]];

		}
	}
	_categoryIds.emplace(identifier);
	return identifier;
}

void UNManager::Private::showNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) {
	if (@available(macOS 10.14, *)) {
		@autoreleasepool {

		const auto peer = info.peer;
		UNMutableNotificationContent *content
			= [[[UNMutableNotificationContent alloc] init] autorelease];
		[content setTitle:Q2NSString(info.title)];
		[content setSubtitle:Q2NSString(info.subtitle)];
		[content setBody:Q2NSString(info.message)];
		const auto sessionId = peer->session().uniqueId();
		const auto monoforumPeerId = info.monoforumPeerId.value;
		[content setUserInfo:
			[NSDictionary dictionaryWithObjectsAndKeys:
				[NSNumber numberWithUnsignedLongLong:sessionId],
				@"session",
				[NSNumber numberWithUnsignedLongLong:peer->id.value],
				@"peer",
				[NSNumber numberWithLongLong:info.topicRootId.bare],
				@"topic",
				[NSNumber numberWithUnsignedLongLong:monoforumPeerId],
				@"monoforumpeer",
				[NSNumber numberWithLongLong:info.itemId.bare],
				@"msgid",
				[NSNumber numberWithUnsignedLongLong:_managerId],
				@"manager",
				nil]];

		const auto category = ensureCategory(
			!info.options.hideReplyButton,
			!info.options.hideMarkAsRead,
			info.actions);
		if (!category.isEmpty()) {
			[content setCategoryIdentifier:Q2NSString(category)];
		}

		const auto sound = info.sound
			? info.sound()
			: Media::Audio::LocalSound();
		if (sound) {
			NSString *name = Q2NSString(_sounds.name(sound));
			[content setSound:[UNNotificationSound soundNamed:name]];
		}

		if (!info.options.hideNameAndPhoto) {
			const auto path = u"%1tdata/temp/%2.png"_q.arg(
				cWorkingDir(),
				QString::number(base::RandomValue<uint64>(), 16));
			if (Window::Notifications::GenerateUserpic(peer, userpicView)
					.save(path, "PNG")) {
				NSError *error = nil;
				UNNotificationAttachment *attachment
					= [UNNotificationAttachment
						attachmentWithIdentifier:@"userpic"
						URL:[NSURL fileURLWithPath:Q2NSString(path)]
						options:nil
						error:&error];
				if (attachment) {
					[content setAttachments:@[attachment]];
				} else {
					if (error) {
						LOG(("App Error: Notification attachment error: %1"
							).arg(NS2QString(error.localizedDescription)));
					}
					QFile(path).remove();
				}
			}
		}

		auto identifier = _managerIdString
			+ '_'
			+ QString::number(peer->id.value)
			+ '_'
			+ QString::number(info.itemId.bare);
		UNNotificationRequest *request = [UNNotificationRequest
			requestWithIdentifier:Q2NSString(identifier)
			content:content
			trigger:nil];
		UNUserNotificationCenter *center
			= [UNUserNotificationCenter currentNotificationCenter];
		_inFlight.push_back({
			.identifier = identifier,
			.parsed = NotificationParsed{
				.sessionId = sessionId,
				.peerId = peer->id.value,
				.topicRootId = info.topicRootId.bare,
				.monoforumPeerId = monoforumPeerId,
				.msgId = info.itemId.bare,
				.valid = true,
			},
		});
		const auto weak = _manager;
		const auto that = this;
		[center addNotificationRequest:request
			withCompletionHandler:^(NSError *error) {
				@autoreleasepool {

				if (error) {
					LOG(("App Error: Failed to show UN notification: %1"
						).arg(NS2QString(error.localizedDescription)));
				}
				crl::on_main(weak, [=] {
					that->finishRequest(identifier);
				});

				}
			}];

		}
	}
}

void UNManager::Private::finishRequest(const QString &identifier) {
	for (auto i = _inFlight.begin(); i != _inFlight.end(); ++i) {
		if (i->identifier != identifier) {
			continue;
		}
		const auto remove = i->removeWhenDone;
		_inFlight.erase(i);
		if (remove) {
			if (@available(macOS 10.14, *)) {
				@autoreleasepool {

				[[UNUserNotificationCenter currentNotificationCenter]
					removeDeliveredNotificationsWithIdentifiers:
						@[Q2NSString(identifier)]];

				}
			}
		}
		return;
	}
}

void UNManager::Private::removeDelivered(
		Fn<bool(const NotificationParsed &)> filter) {
	for (auto &request : _inFlight) {
		if (filter(request.parsed)) {
			request.removeWhenDone = true;
		}
	}
	if (@available(macOS 10.14, *)) {
		const auto managerId = _managerId;
		UNUserNotificationCenter *center
			= [UNUserNotificationCenter currentNotificationCenter];
		[center getDeliveredNotificationsWithCompletionHandler:^(
				NSArray<UNNotification*> *notifications) {
			@autoreleasepool {

			NSMutableArray<NSString*> *identifiers = [NSMutableArray array];
			for (UNNotification *notification in notifications) {
				NSDictionary *userInfo
					= notification.request.content.userInfo;
				if (ParseManagerId(userInfo) != managerId) {
					continue;
				}
				const auto parsed = ParseNotification(userInfo);
				if (!parsed.valid || filter(parsed)) {
					[identifiers addObject:notification.request.identifier];
				}
			}
			if ([identifiers count] > 0) {
				[center removeDeliveredNotificationsWithIdentifiers:
					identifiers];
			}

			}
		}];
	}
}

void UNManager::Private::clearAll() {
	removeDelivered([](const NotificationParsed &) {
		return true;
	});
}

void UNManager::Private::clearFromItem(not_null<HistoryItem*> item) {
	const auto target = NotificationId{ ContextId{
		.sessionId = item->history()->session().uniqueId(),
		.peerId = item->history()->peer->id,
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	}, item->id };
	removeDelivered([=](const NotificationParsed &parsed) {
		return (parsed.msgId != 0)
			&& (NotificationId{
				ParsedContextId(parsed),
				MsgId(parsed.msgId) } == target);
	});
}

void UNManager::Private::clearFromTopic(
		not_null<Data::ForumTopic*> topic) {
	const auto target = ContextId{
		.sessionId = topic->session().uniqueId(),
		.peerId = topic->history()->peer->id,
		.topicRootId = topic->rootId(),
	};
	removeDelivered([=](const NotificationParsed &parsed) {
		return ParsedContextId(parsed) == target;
	});
}

void UNManager::Private::clearFromSublist(
		not_null<Data::SavedSublist*> sublist) {
	const auto target = ContextId{
		.sessionId = sublist->session().uniqueId(),
		.peerId = sublist->owningHistory()->peer->id,
		.monoforumPeerId = sublist->sublistPeer()->id,
	};
	removeDelivered([=](const NotificationParsed &parsed) {
		return ParsedContextId(parsed) == target;
	});
}

void UNManager::Private::clearFromHistory(not_null<History*> history) {
	const auto target = ContextId{
		.sessionId = history->session().uniqueId(),
		.peerId = history->peer->id,
	};
	removeDelivered([=](const NotificationParsed &parsed) {
		return ContextId{
			.sessionId = parsed.sessionId,
			.peerId = PeerId(parsed.peerId),
		} == target;
	});
}

void UNManager::Private::clearFromSession(
		not_null<Main::Session*> session) {
	const auto sessionId = session->uniqueId();
	removeDelivered([=](const NotificationParsed &parsed) {
		return parsed.sessionId == sessionId;
	});
}

UNManager::Private::~Private() {
	if (@available(macOS 10.14, *)) {
		UNUserNotificationCenter *center
			= [UNUserNotificationCenter currentNotificationCenter];
		if (center.delegate
			== (id<UNUserNotificationCenterDelegate>)_delegate) {
			[center setDelegate:nil];
		}
	}
	[_delegate release];
	[_categories release];
}

UNManager::UNManager(Window::Notifications::System *system)
: NativeManager(system)
, _private(std::make_unique<Private>(this)) {
}

UNManager::~UNManager() = default;

void UNManager::doShowNativeNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) {
	_private->showNotification(std::move(info), userpicView);
}

void UNManager::doClearAllFast() {
	_private->clearAll();
}

void UNManager::doClearFromItem(not_null<HistoryItem*> item) {
	_private->clearFromItem(item);
}

void UNManager::doClearFromTopic(not_null<Data::ForumTopic*> topic) {
	_private->clearFromTopic(topic);
}

void UNManager::doClearFromSublist(not_null<Data::SavedSublist*> sublist) {
	_private->clearFromSublist(sublist);
}

void UNManager::doClearFromHistory(not_null<History*> history) {
	_private->clearFromHistory(history);
}

void UNManager::doClearFromSession(not_null<Main::Session*> session) {
	_private->clearFromSession(session);
}

QString UNManager::accountNameSeparator() {
	return QString::fromUtf8(" \xE2\x86\x92 ");
}

bool UNManager::doSkipToast() const {
	return false;
}

void UNManager::doMaybePlaySound(Fn<void()> playSound) {
	playSound();
}

void UNManager::doMaybeFlashBounce(Fn<void()> flashBounce) {
	flashBounce();
}

} // namespace Notifications
} // namespace Platform
