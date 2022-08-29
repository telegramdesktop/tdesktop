/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_message_reaction_id.h"
#include "base/observer.h"
#include "base/timer.h"

class History;
struct ItemNotification;
enum class ItemNotificationType;

namespace Data {
class Session;
class CloudImageView;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Platform {
namespace Notifications {
class Manager;
} // namespace Notifications
} // namespace Platform

namespace Media {
namespace Audio {
class Track;
} // namespace Audio
} // namespace Media

namespace Window {

class SessionController;

namespace Notifications {

enum class ManagerType {
	Dummy,
	Default,
	Native,
};

enum class ChangeType {
	SoundEnabled,
	FlashBounceEnabled,
	IncludeMuted,
	CountMessages,
	DesktopEnabled,
	ViewParams,
	MaxCount,
	Corner,
	DemoIsShown,
	DemoIsHidden,
};

} // namespace Notifications
} // namespace Window

namespace base {

template <>
struct custom_is_fast_copy_type<Window::Notifications::ChangeType> : public std::true_type {
};

} // namespace base

namespace Window {
namespace Notifications {

class Manager;

class System final {
public:
	System();
	~System();

	[[nodiscard]] Main::Session *findSession(uint64 sessionId) const;

	void createManager();
	void setManager(std::unique_ptr<Manager> manager);
	[[nodiscard]] ManagerType managerType() const;

	void checkDelayed();
	void schedule(ItemNotification notification);
	void clearFromHistory(not_null<History*> history);
	void clearIncomingFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
	void clearFromItem(not_null<HistoryItem*> item);
	void clearAll();
	void clearAllFast();
	void updateAll();

	[[nodiscard]] rpl::producer<ChangeType> settingsChanged() const;
	void notifySettingsChanged(ChangeType type);

	void playSound(not_null<Main::Session*> session, DocumentId id);

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct Waiter;

	struct SkipState {
		enum Value {
			Unknown,
			Skip,
			DontSkip
		};
		Value value = Value::Unknown;
		bool silent = false;
	};
	struct NotificationInHistoryKey {
		NotificationInHistoryKey(ItemNotification notification);
		NotificationInHistoryKey(MsgId messageId, ItemNotificationType type);

		MsgId messageId = 0;
		ItemNotificationType type = ItemNotificationType();

		friend inline bool operator<(
				NotificationInHistoryKey a,
				NotificationInHistoryKey b) {
			return std::pair(a.messageId, a.type)
				< std::pair(b.messageId, b.type);
		}
	};
	struct Timing {
		crl::time delay = 0;
		crl::time when = 0;
	};
	struct ReactionNotificationId {
		FullMsgId itemId;
		uint64 sessionId = 0;

		friend inline bool operator<(
				ReactionNotificationId a,
				ReactionNotificationId b) {
			return std::pair(a.itemId, a.sessionId)
				< std::pair(b.itemId, b.sessionId);
		}
	};

	[[nodiscard]] SkipState skipNotification(
		ItemNotification notification) const;
	[[nodiscard]] SkipState computeSkipState(
		ItemNotification notification) const;
	[[nodiscard]] Timing countTiming(
		not_null<History*> history,
		crl::time minimalDelay) const;
	[[nodiscard]] bool skipReactionNotification(
		not_null<HistoryItem*> item) const;

	void showNext();
	void showGrouped();
	void ensureSoundCreated();
	[[nodiscard]] not_null<Media::Audio::Track*> lookupSound(
		not_null<Data::Session*> owner,
		DocumentId id);

	base::flat_map<
		not_null<History*>,
		base::flat_map<NotificationInHistoryKey, crl::time>> _whenMaps;

	base::flat_map<not_null<History*>, Waiter> _waiters;
	base::flat_map<not_null<History*>, Waiter> _settingWaiters;
	base::Timer _waitTimer;
	base::Timer _waitForAllGroupedTimer;

	base::flat_map<
		not_null<History*>,
		base::flat_map<crl::time, PeerData*>> _whenAlerts;

	mutable base::flat_map<
		ReactionNotificationId,
		crl::time> _sentReactionNotifications;

	std::unique_ptr<Manager> _manager;

	rpl::event_stream<ChangeType> _settingsChanged;

	std::unique_ptr<Media::Audio::Track> _soundTrack;
	base::flat_map<
		DocumentId,
		std::unique_ptr<Media::Audio::Track>> _customSoundTracks;

	int _lastForwardedCount = 0;
	uint64 _lastHistorySessionId = 0;
	FullMsgId _lastHistoryItemId;

	rpl::lifetime _lifetime;

};

class Manager {
public:
	struct FullPeer {
		uint64 sessionId = 0;
		PeerId peerId = 0;

		friend inline bool operator<(const FullPeer &a, const FullPeer &b) {
			return std::tie(a.sessionId, a.peerId)
				< std::tie(b.sessionId, b.peerId);
		}
	};
	struct NotificationId {
		FullPeer full;
		MsgId msgId = 0;

		friend inline bool operator<(
				const NotificationId &a,
				const NotificationId &b) {
			return std::tie(a.full, a.msgId) < std::tie(b.full, b.msgId);
		}
	};
	struct NotificationFields {
		not_null<HistoryItem*> item;
		int forwardedCount = 0;
		PeerData *reactionFrom = nullptr;
		Data::ReactionId reactionId;
	};

	explicit Manager(not_null<System*> system) : _system(system) {
	}

	void showNotification(NotificationFields fields) {
		doShowNotification(std::move(fields));
	}
	void updateAll() {
		doUpdateAll();
	}
	void clearAll() {
		doClearAll();
	}
	void clearAllFast() {
		doClearAllFast();
	}
	void clearFromItem(not_null<HistoryItem*> item) {
		doClearFromItem(item);
	}
	void clearFromHistory(not_null<History*> history) {
		doClearFromHistory(history);
	}
	void clearFromSession(not_null<Main::Session*> session) {
		doClearFromSession(session);
	}

	void notificationActivated(
		NotificationId id,
		const TextWithTags &draft = {});
	void notificationReplied(NotificationId id, const TextWithTags &reply);

	struct DisplayOptions {
		bool hideNameAndPhoto = false;
		bool hideMessageText = false;
		bool hideMarkAsRead = false;
		bool hideReplyButton = false;
	};
	[[nodiscard]] DisplayOptions getNotificationOptions(
		HistoryItem *item,
		ItemNotificationType type) const;
	[[nodiscard]] static TextWithEntities ComposeReactionEmoji(
		not_null<Main::Session*> session,
		const Data::ReactionId &reaction);
	[[nodiscard]] static TextWithEntities ComposeReactionNotification(
		not_null<HistoryItem*> item,
		const Data::ReactionId &reaction,
		bool hideContent);

	[[nodiscard]] QString addTargetAccountName(
		const QString &title,
		not_null<Main::Session*> session);

	[[nodiscard]] virtual ManagerType type() const = 0;

	[[nodiscard]] bool skipAudio() const {
		return doSkipAudio();
	}
	[[nodiscard]] bool skipToast() const {
		return doSkipToast();
	}
	[[nodiscard]] bool skipFlashBounce() const {
		return doSkipFlashBounce();
	}

	virtual ~Manager() = default;

protected:
	not_null<System*> system() const {
		return _system;
	}

	virtual void doUpdateAll() = 0;
	virtual void doShowNotification(NotificationFields &&fields) = 0;
	virtual void doClearAll() = 0;
	virtual void doClearAllFast() = 0;
	virtual void doClearFromItem(not_null<HistoryItem*> item) = 0;
	virtual void doClearFromHistory(not_null<History*> history) = 0;
	virtual void doClearFromSession(not_null<Main::Session*> session) = 0;
	virtual bool doSkipAudio() const = 0;
	virtual bool doSkipToast() const = 0;
	virtual bool doSkipFlashBounce() const = 0;
	[[nodiscard]] virtual bool forceHideDetails() const {
		return false;
	}
	virtual void onBeforeNotificationActivated(NotificationId id) {
	}
	virtual void onAfterNotificationActivated(
		NotificationId id,
		not_null<SessionController*> window) {
	}
	[[nodiscard]] virtual QString accountNameSeparator();

private:
	void openNotificationMessage(
		not_null<History*> history,
		MsgId messageId);

	const not_null<System*> _system;

};

class NativeManager : public Manager {
public:
	[[nodiscard]] ManagerType type() const override {
		return ManagerType::Native;
	}

protected:
	using Manager::Manager;

	void doUpdateAll() override {
		doClearAllFast();
	}
	void doClearAll() override {
		doClearAllFast();
	}
	void doShowNotification(NotificationFields &&fields) override;

	bool forceHideDetails() const override;

	virtual void doShowNativeNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		DisplayOptions options) = 0;

};

class DummyManager : public NativeManager {
public:
	using NativeManager::NativeManager;

	[[nodiscard]] ManagerType type() const override {
		return ManagerType::Dummy;
	}

protected:
	void doShowNativeNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		DisplayOptions options) override {
	}
	void doClearAllFast() override {
	}
	void doClearFromItem(not_null<HistoryItem*> item) override {
	}
	void doClearFromHistory(not_null<History*> history) override {
	}
	void doClearFromSession(not_null<Main::Session*> session) override {
	}
	bool doSkipAudio() const override {
		return false;
	}
	bool doSkipToast() const override {
		return false;
	}
	bool doSkipFlashBounce() const override {
		return false;
	}

};

QString WrapFromScheduled(const QString &text);

} // namespace Notifications
} // namespace Window
