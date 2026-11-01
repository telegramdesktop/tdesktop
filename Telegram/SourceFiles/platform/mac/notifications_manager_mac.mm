/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/notifications_manager_mac.h"

#include "base/platform/base_platform_info.h"
#include "base/options.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "history/history_item.h"
#include "history/history.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "platform/mac/notifications_manager_mac_tokens.h"
#include "platform/mac/notifications_manager_mac_un.h"
#include "platform/platform_specific.h"
#include "ui/empty_userpic.h"
#include "window/notifications_utilities.h"
#include "styles/style_window.h"

#include <thread>
#include <Cocoa/Cocoa.h>

namespace {

constexpr auto kQuerySettingsEachMs = crl::time(1000);
constexpr auto kCacheExpirationWeeks = 5;
constexpr auto kCacheExpirationSeconds = kCacheExpirationWeeks * 7 * 24 * 60 * 60;

NSString *const kTelegramMarkAsReadText = @"TelegramMarkAsReadText";
NSString *const kTelegramMarkAsReadTimestamp = @"TelegramMarkAsReadTimestamp";
NSString *const kTelegramMarkAsReadLanguageCode = @"TelegramMarkAsReadLanguageCode";

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
using NotificationTokens = Platform::Notifications::NotificationTokens;

} // namespace

@interface NotificationDelegate : NSObject<NSUserNotificationCenterDelegate> {
}

- (id) initWithManager:(base::weak_ptr<Manager>)manager managerId:(uint64)managerId tokens:(const NotificationTokens*)tokens;
- (void) userNotificationCenter:(NSUserNotificationCenter*)center didActivateNotification:(NSUserNotification*)notification;
- (BOOL) userNotificationCenter:(NSUserNotificationCenter*)center shouldPresentNotification:(NSUserNotification*)notification;

@end // @interface NotificationDelegate

@implementation NotificationDelegate {
	base::weak_ptr<Manager> _manager;
	uint64 _managerId;
	const NotificationTokens *_tokens;

}

- (id) initWithManager:(base::weak_ptr<Manager>)manager managerId:(uint64)managerId tokens:(const NotificationTokens*)tokens {
	if (self = [super init]) {
		_manager = manager;
		_managerId = managerId;
		_tokens = tokens;
	}
	return self;
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification {
	NSDictionary *notificationUserInfo = [notification userInfo];
	const auto notificationManagerId
		= Platform::Notifications::NotificationManagerId(notificationUserInfo);
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

	const auto token
		= Platform::Notifications::NotificationToken(notificationUserInfo);
	if (!token) {
		LOG(("App Error: A notification with unknown token was received"));
		return;
	}
	const auto manager = _manager;
	const auto tokens = _tokens;
	if (notification.activationType == NSUserNotificationActivationTypeReplied) {
		const auto notificationReply = QString::fromUtf8([[[notification response] string] UTF8String]);
		crl::on_main(manager, [=] {
			if (const auto my = tokens->lookup(token)) {
				manager->notificationReplied(*my, { notificationReply, {} });
			}
		});
	} else if (notification.activationType == NSUserNotificationActivationTypeContentsClicked) {
		crl::on_main(manager, [=] {
			if (const auto my = tokens->lookup(token)) {
				manager->notificationActivated(*my);
			}
		});
	}
	if (notification.activationType == NSUserNotificationActivationTypeAdditionalActionClicked
		|| notification.activationType == NSUserNotificationActivationTypeActionButtonClicked) {
		NSString *actionId = nil;
		if (notification.activationType == NSUserNotificationActivationTypeAdditionalActionClicked
			&& [notification respondsToSelector:@selector(additionalActivationAction)]) {
			actionId = notification.additionalActivationAction.identifier;
		}
		if (!actionId) {
			actionId = [notificationUserInfo objectForKey:@"actionId"];
		}
		if (actionId) {
			const auto actionStr = QString::fromNSString(actionId);
			crl::on_main(manager, [=] {
				if (const auto my = tokens->lookup(token)) {
					manager->notificationActionActivated(*my, actionStr);
				}
			});
		}
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
	if (UseUNManager()) {
		system->setManager([=] {
			return std::make_unique<UNManager>(system);
		});
		return;
	}
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
	void clearTokens(base::flat_set<uint64> tokens);

	void clearingThreadLoop();
	void initCachedMarkAsReadText();

	const uint64 _managerId = 0;
	QString _managerIdString;

	NotificationTokens _tokens;
	NotificationDelegate *_delegate = nullptr;

	std::thread _clearingThread;
	std::mutex _clearingMutex;
	std::condition_variable _clearingCondition;

	struct ClearTokens {
		base::flat_set<uint64> tokens;
	};
	struct ClearAll {
	};
	struct ClearFinish {
	};
	using ClearTask = std::variant<ClearTokens, ClearAll, ClearFinish>;
	std::vector<ClearTask> _clearingTasks;

	Media::Audio::LocalDiskCache _sounds;
	NSString *_cachedMarkAsReadText = nil;

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

void AddActionIdToNotification(
		NSUserNotification *notification,
		NSString *actionId) {
	NSMutableDictionary *mutableUserInfo
		= [[notification userInfo] mutableCopy];
	[mutableUserInfo setObject:actionId forKey:@"actionId"];
	[notification setUserInfo:mutableUserInfo];
	[mutableUserInfo release];
}

Manager::Private::Private(Manager *manager)
: _managerId(base::RandomValue<uint64>())
, _managerIdString(QString::number(_managerId))
, _delegate([[NotificationDelegate alloc]
	initWithManager:manager
	managerId:_managerId
	tokens:&_tokens])
, _sounds(ResolveSoundsFolder()) {
	Core::App().settings().workModeValue(
	) | rpl::on_next([=](Core::Settings::WorkMode mode) {
		// We need to update the delegate _after_ the tray icon change was done in Qt.
		// Because Qt resets the delegate.
		crl::on_main(this, [=] {
			updateDelegate();
		});
	}, _lifetime);

	initCachedMarkAsReadText();
}

void Manager::Private::showNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) {
	@autoreleasepool {

	const auto peer = info.peer;
	const auto token = _tokens.add({
		.contextId = {
			.sessionId = peer->session().uniqueId(),
			.peerId = peer->id,
			.topicRootId = info.topicRootId,
			.monoforumPeerId = info.monoforumPeerId,
		},
		.msgId = info.itemId,
	});
	NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
	if ([notification respondsToSelector:@selector(setIdentifier:)]) {
		auto identifier = _managerIdString + '_' + QString::number(token);
		auto identifierValue = Q2NSString(identifier);
		[notification setIdentifier:identifierValue];
	}
	[notification setUserInfo:NotificationUserInfo(_managerId, token)];

	[notification setTitle:Q2NSString(info.title)];
	[notification setSubtitle:Q2NSString(info.subtitle)];
	[notification setInformativeText:Q2NSString(info.message)];
	if (!info.options.hideNameAndPhoto
		&& [notification respondsToSelector:@selector(setContentImage:)]) {
		NSImage *img = Q2NSImage(
			Window::Notifications::GenerateUserpic(peer, userpicView));
		[notification setContentImage:img];
	}

	if (!info.actions.empty()
		&& [notification respondsToSelector:@selector(setAdditionalActions:)]) {
		NSMutableArray *actions = [NSMutableArray array];
		if (!info.options.hideMarkAsRead) {
			[actions addObject:[NSUserNotificationAction
				actionWithIdentifier:@"markAsRead"
				title:_cachedMarkAsReadText]];
		}
		for (const auto &action : info.actions) {
			[actions addObject:[NSUserNotificationAction
				actionWithIdentifier:Q2NSString(action.id)
				title:Q2NSString(action.text)]];
		}
		if (!info.options.hideReplyButton
			&& [notification respondsToSelector:@selector(setHasReplyButton:)]) {
			[notification setHasReplyButton:YES];
		}
		[notification setAdditionalActions:actions];
		AddActionIdToNotification(
			notification,
			[[actions firstObject] identifier]);
	} else if (!info.options.hideReplyButton
		&& !info.options.hideMarkAsRead
		&& [notification respondsToSelector:@selector(setHasReplyButton:)]
		&& [notification respondsToSelector:@selector(setAdditionalActions:)]) {
		[notification setHasReplyButton:YES];

		AddActionIdToNotification(notification, @"markAsRead");

		[notification setAdditionalActions:@[
			[NSUserNotificationAction
				actionWithIdentifier:@"markAsRead"
				title:_cachedMarkAsReadText]
		]];
	} else if (!info.options.hideReplyButton
		&& [notification respondsToSelector:@selector(setHasReplyButton:)]) {
		[notification setHasReplyButton:YES];
	} else if (!info.options.hideMarkAsRead
		&& [notification respondsToSelector:@selector(setHasActionButton:)]) {
		[notification setHasActionButton:YES];
		[notification
			setActionButtonTitle:_cachedMarkAsReadText];

		AddActionIdToNotification(notification, @"markAsRead");
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
		auto tasks = std::vector<ClearTask>();
		{
			std::unique_lock<std::mutex> lock(_clearingMutex);
			while (_clearingTasks.empty()) {
				_clearingCondition.wait(lock);
			}
			tasks = base::take(_clearingTasks);
		}
		auto clearAll = false;
		auto selected = std::vector<uint64>();
		for (const auto &task : tasks) {
			v::match(task, [&](ClearFinish) {
				finished = true;
				clearAll = true;
			}, [&](ClearAll) {
				clearAll = true;
			}, [&](const ClearTokens &value) {
				selected.insert(
					selected.end(),
					value.tokens.begin(),
					value.tokens.end());
			});
		}
		const auto tokens = base::flat_set<uint64>(
			selected.begin(),
			selected.end());

		@autoreleasepool {

		NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
		NSArray *notificationsList = [center deliveredNotifications];
		for (id notification in notificationsList) {
			NSDictionary *notificationUserInfo = [notification userInfo];
			if (NotificationManagerId(notificationUserInfo) != _managerId) {
				continue;
			}
			const auto token = NotificationToken(notificationUserInfo);
			if (clearAll || !token || tokens.contains(token)) {
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
	_clearingTasks.push_back(std::move(task));
	_clearingCondition.notify_one();
}

void Manager::Private::clearTokens(base::flat_set<uint64> tokens) {
	if (!tokens.empty()) {
		putClearTask(ClearTokens{ std::move(tokens) });
	}
}

void Manager::Private::clearAll() {
	_tokens.clear();
	putClearTask(ClearAll());
}

void Manager::Private::clearFromItem(not_null<HistoryItem*> item) {
	clearTokens(_tokens.takeFromItem({ ItemContextId(item), item->id }));
}

void Manager::Private::clearFromTopic(not_null<Data::ForumTopic*> topic) {
	clearTokens(_tokens.takeFromContext({
		.sessionId = topic->session().uniqueId(),
		.peerId = topic->history()->peer->id,
		.topicRootId = topic->rootId(),
	}));
}

void Manager::Private::clearFromSublist(
		not_null<Data::SavedSublist*> sublist) {
	clearTokens(_tokens.takeFromContext(SublistContextId(sublist)));
}

void Manager::Private::clearFromHistory(not_null<History*> history) {
	clearTokens(_tokens.takeFromHistory({
		.sessionId = history->session().uniqueId(),
		.peerId = history->peer->id,
	}));
}

void Manager::Private::clearFromSession(not_null<Main::Session*> session) {
	clearTokens(_tokens.takeFromSession(session->uniqueId()));
}

void Manager::Private::updateDelegate() {
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center setDelegate:_delegate];
}

void Manager::Private::initCachedMarkAsReadText() {
	const auto langId = Lang::GetInstance().id();

	const auto preferredLang = [[NSLocale preferredLanguages] firstObject];
	const auto languageCode
		= [[NSLocale localeWithLocaleIdentifier:preferredLang] languageCode];

	const auto defaults = [NSUserDefaults standardUserDefaults];
	const auto cachedText = static_cast<NSString*>([defaults
		stringForKey:kTelegramMarkAsReadText]);
	const auto cachedTimestamp = static_cast<NSNumber*>([defaults
		objectForKey:kTelegramMarkAsReadTimestamp]);
	const auto cachedLanguageCode = static_cast<NSString*>([defaults
		stringForKey:kTelegramMarkAsReadLanguageCode]);

	const auto now = base::unixtime::now();
	const auto shouldRefresh = !cachedTimestamp
		|| (now - [cachedTimestamp longLongValue]) > kCacheExpirationSeconds
		|| ![cachedLanguageCode isEqualToString:languageCode];

	if (cachedText && !shouldRefresh) {
		_cachedMarkAsReadText = [cachedText retain];
	} else {
		_cachedMarkAsReadText
			= [Q2NSString(tr::lng_context_mark_read(tr::now)) retain];
	}

	if (langId == NS2QString(languageCode)) {
		[defaults
			setObject:Q2NSString(tr::lng_context_mark_read(tr::now))
			forKey:kTelegramMarkAsReadText];
		[defaults
			setObject:@(base::unixtime::now())
			forKey:kTelegramMarkAsReadTimestamp];
		[defaults
			setObject:languageCode
			forKey:kTelegramMarkAsReadLanguageCode];
	} else if (shouldRefresh) {
		Lang::CurrentCloudManager().getValueForLang(
			u"lng_context_mark_read"_q,
			NS2QString(languageCode),
			[=](const QString &r) {
				if (r != NS2QString(_cachedMarkAsReadText)) {
					[_cachedMarkAsReadText release];
					_cachedMarkAsReadText = [Q2NSString(r) retain];
				}
				[defaults
					setObject:Q2NSString(r)
					forKey:kTelegramMarkAsReadText];
				[defaults
					setObject:@(base::unixtime::now())
					forKey:kTelegramMarkAsReadTimestamp];
				[defaults
					setObject:languageCode
					forKey:kTelegramMarkAsReadLanguageCode];
			});
	}
}

Manager::Private::~Private() {
	if (_clearingThread.joinable()) {
		putClearTask(ClearFinish());
		_clearingThread.join();
	}
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center setDelegate:nil];
	[_delegate release];
	[_cachedMarkAsReadText release];
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
