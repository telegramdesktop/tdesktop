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
#include "base/openssl_help.h"
#include "history/history.h"
#include "ui/empty_userpic.h"
#include "main/main_session.h"
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

	NSNumber *msgObject = [notificationUserInfo objectForKey:@"msgid"];
	const auto notificationMsgId = msgObject ? [msgObject intValue] : 0;

	const auto my = Window::Notifications::Manager::NotificationId{
		.full = Manager::FullPeer{
			.sessionId = notificationSessionId,
			.peerId = PeerId(notificationPeerId)
		},
		.msgId = notificationMsgId
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

bool SkipAudioForCustom() {
	return false;
}

bool SkipToastForCustom() {
	return false;
}

bool SkipFlashBounceForCustom() {
	return false;
}

bool Supported() {
	return Platform::IsMac10_8OrGreater();
}

bool Enforced() {
	return Supported();
}

bool ByDefault() {
	return Supported();
}

void Create(Window::Notifications::System *system) {
	if (Supported()) {
		system->setManager(std::make_unique<Manager>(system));
	} else {
		system->setManager(nullptr);
	}
}

class Manager::Private : public QObject {
public:
	Private(Manager *manager);

	void showNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton);
	void clearAll();
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

	struct ClearFromHistory {
		FullPeer fullPeer;
	};
	struct ClearFromSession {
		uint64 sessionId = 0;
	};
	struct ClearAll {
	};
	struct ClearFinish {
	};
	using ClearTask = std::variant<
		ClearFromHistory,
		ClearFromSession,
		ClearAll,
		ClearFinish>;
	std::vector<ClearTask> _clearingTasks;

	rpl::lifetime _lifetime;

};

Manager::Private::Private(Manager *manager)
: _managerId(openssl::RandomValue<uint64>())
, _managerIdString(QString::number(_managerId))
, _delegate([[NotificationDelegate alloc] initWithManager:manager managerId:_managerId]) {
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
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	@autoreleasepool {

	NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
	if ([notification respondsToSelector:@selector(setIdentifier:)]) {
		auto identifier = _managerIdString + '_' + QString::number(peer->id.value) + '_' + QString::number(msgId);
		auto identifierValue = Q2NSString(identifier);
		[notification setIdentifier:identifierValue];
	}
	[notification setUserInfo:[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:peer->session().uniqueId()],@"session",[NSNumber numberWithUnsignedLongLong:peer->id.value],@"peer",[NSNumber numberWithInt:msgId],@"msgid",[NSNumber numberWithUnsignedLongLong:_managerId],@"manager",nil]];

	[notification setTitle:Q2NSString(title)];
	[notification setSubtitle:Q2NSString(subtitle)];
	[notification setInformativeText:Q2NSString(msg)];
	if (!hideNameAndPhoto && [notification respondsToSelector:@selector(setContentImage:)]) {
		auto userpic = peer->isSelf()
			? Ui::EmptyUserpic::GenerateSavedMessages(st::notifyMacPhotoSize)
			: peer->isRepliesChat()
			? Ui::EmptyUserpic::GenerateRepliesMessages(st::notifyMacPhotoSize)
			: peer->genUserpic(userpicView, st::notifyMacPhotoSize);
		NSImage *img = Q2NSImage(userpic.toImage());
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
		auto clearFromPeers = base::flat_set<FullPeer>();
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
				}, [&](const ClearFromHistory &value) {
					clearFromPeers.emplace(value.fullPeer);
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
			if (NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"]) {
				const auto notificationPeerId = [peerObject unsignedLongLongValue];
				if (notificationPeerId) {
					return clearFromSessions.contains(notificationSessionId)
						|| clearFromPeers.contains(FullPeer{
							.sessionId = notificationSessionId,
							.peerId = PeerId(notificationPeerId)
						});
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

void Manager::Private::clearFromHistory(not_null<History*> history) {
	putClearTask(ClearFromHistory { FullPeer{
		.sessionId = history->session().uniqueId(),
		.peerId = history->peer->id
	} });
}

void Manager::Private::clearFromSession(not_null<Main::Session*> session) {
	putClearTask(ClearFromSession { session->uniqueId() });
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
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) {
	_private->showNotification(
		peer,
		userpicView,
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

void Manager::doClearFromSession(not_null<Main::Session*> session) {
	_private->clearFromSession(session);
}

QString Manager::accountNameSeparator() {
	return QString::fromUtf8(" \xE2\x86\x92 ");
}

bool Manager::doSkipAudio() const {
	queryDoNotDisturbState();
	return DoNotDisturbEnabled;
}

bool Manager::doSkipToast() const {
	return false;
}

bool Manager::doSkipFlashBounce() const {
	return doSkipAudio();
}

} // namespace Notifications
} // namespace Platform
