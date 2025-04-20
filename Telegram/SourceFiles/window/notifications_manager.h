/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_message_reaction_id.h"
#include "base/timer.h"
#include "base/type_traits.h"
#include "media/audio/media_audio_local_cache.h"

class History;

namespace Data {
class Session;
class ForumTopic;
class Thread;
struct ItemNotification;
enum class ItemNotificationType;
} // namespace Data

namespace Ui {
struct PeerUserpicView;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Platform {
namespace Notifications {
class Manager;
} // namespace Notifications
} // namespace Platform

namespace Media::Audio {
class Track;
} // namespace Media::Audio

namespace Window {
class SessionController;
} // namespace Window

namespace Window::Notifications {

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

} // namespace Window::Notifications

namespace base {

template <>
struct custom_is_fast_copy_type<Window::Notifications::ChangeType> : std::true_type {
};

} // namespace base

namespace base::options {

template <typename Type>
class option;

using toggle = option<bool>;

} // namespace base::options

namespace Window::Notifications {

extern const char kOptionGNotification[];
extern base::options::toggle OptionGNotification;

class Manager;

class System final {
public:
	System();
	~System();

	[[nodiscard]] Main::Session *findSession(uint64 sessionId) const;

	void createManager();
	void setManager(Fn<std::unique_ptr<Manager>()> create);

	void checkDelayed();
	void schedule(Data::ItemNotification notification);
	void clearFromTopic(not_null<Data::ForumTopic*> topic);
	void clearFromHistory(not_null<History*> history);
	void clearIncomingFromTopic(not_null<Data::ForumTopic*> topic);
	void clearIncomingFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
	void clearFromItem(not_null<HistoryItem*> item);
	void clearAll();
	void clearAllFast();
	void updateAll();

	[[nodiscard]] rpl::producer<ChangeType> settingsChanged() const;
	void notifySettingsChanged(ChangeType type);

	void playSound(not_null<Main::Session*> session, DocumentId id);
	[[nodiscard]] QByteArray lookupSoundBytes(
		not_null<Data::Session*> owner,
		DocumentId id);

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
		NotificationInHistoryKey(Data::ItemNotification notification);
		NotificationInHistoryKey(
			MsgId messageId,
			Data::ItemNotificationType type);

		MsgId messageId = 0;
		Data::ItemNotificationType type = Data::ItemNotificationType();

		friend inline auto operator<=>(
			NotificationInHistoryKey a,
			NotificationInHistoryKey b) = default;
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

	void clearForThreadIf(Fn<bool(not_null<Data::Thread*>)> predicate);

	[[nodiscard]] SkipState skipNotification(
		Data::ItemNotification notification) const;
	[[nodiscard]] SkipState computeSkipState(
		Data::ItemNotification notification) const;
	[[nodiscard]] Timing countTiming(
		not_null<Data::Thread*> thread,
		crl::time minimalDelay) const;
	[[nodiscard]] bool skipReactionNotification(
		not_null<HistoryItem*> item) const;

	void showNext();
	void showGrouped();
	void ensureSoundCreated();
	[[nodiscard]] not_null<Media::Audio::Track*> lookupSound(
		not_null<Data::Session*> owner,
		DocumentId id);

	void registerThread(not_null<Data::Thread*> thread);

	base::flat_map<
		not_null<Data::Thread*>,
		base::flat_map<NotificationInHistoryKey, crl::time>> _whenMaps;

	base::flat_map<not_null<Data::Thread*>, Waiter> _waiters;
	base::flat_map<not_null<Data::Thread*>, Waiter> _settingWaiters;
	base::Timer _waitTimer;
	base::Timer _waitForAllGroupedTimer;

	base::flat_map<
		not_null<Data::Thread*>,
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

	base::flat_map<
		not_null<Data::ForumTopic*>,
		rpl::lifetime> _watchedTopics;

	int _lastForwardedCount = 0;
	uint64 _lastHistorySessionId = 0;
	FullMsgId _lastHistoryItemId;
	std::optional<DocumentId> _lastSoundId;

	rpl::lifetime _lifetime;

};

class Manager {
public:
	struct ContextId {
		uint64 sessionId = 0;
		PeerId peerId = 0;
		MsgId topicRootId = 0;

		friend inline auto operator<=>(
			const ContextId&,
			const ContextId&) = default;
	};
	struct NotificationId {
		ContextId contextId;
		MsgId msgId = 0;

		friend inline auto operator<=>(
			const NotificationId&,
			const NotificationId&) = default;
	};
	struct NotificationFields {
		not_null<HistoryItem*> item;
		int forwardedCount = 0;
		PeerData *reactionFrom = nullptr;
		Data::ReactionId reactionId;
		std::optional<DocumentId> soundId;
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
	void clearFromTopic(not_null<Data::ForumTopic*> topic) {
		doClearFromTopic(topic);
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
		bool hideNameAndPhoto : 1 = false;
		bool hideMessageText : 1 = false;
		bool hideMarkAsRead : 1 = false;
		bool hideReplyButton : 1 = false;
		bool spoilerLoginCode : 1 = false;
	};
	[[nodiscard]] DisplayOptions getNotificationOptions(
		HistoryItem *item,
		Data::ItemNotificationType type) const;
	[[nodiscard]] static TextWithEntities ComposeReactionEmoji(
		not_null<Main::Session*> session,
		const Data::ReactionId &reaction);
	[[nodiscard]] static TextWithEntities ComposeReactionNotification(
		not_null<HistoryItem*> item,
		const Data::ReactionId &reaction,
		bool hideContent);

	[[nodiscard]] TextWithEntities addTargetAccountName(
		TextWithEntities title,
		not_null<Main::Session*> session);
	[[nodiscard]] QString addTargetAccountName(
		const QString &title,
		not_null<Main::Session*> session);

	[[nodiscard]] virtual ManagerType type() const = 0;

	[[nodiscard]] bool skipToast() const {
		return doSkipToast();
	}
	void maybePlaySound(Fn<void()> playSound) {
		doMaybePlaySound(std::move(playSound));
	}
	void maybeFlashBounce(Fn<void()> flashBounce) {
		doMaybeFlashBounce(std::move(flashBounce));
	}

	virtual ~Manager() = default;

protected:
	[[nodiscard]] not_null<System*> system() const {
		return _system;
	}

	virtual void doUpdateAll() = 0;
	virtual void doShowNotification(NotificationFields &&fields) = 0;
	virtual void doClearAll() = 0;
	virtual void doClearAllFast() = 0;
	virtual void doClearFromItem(not_null<HistoryItem*> item) = 0;
	virtual void doClearFromTopic(not_null<Data::ForumTopic*> topic) = 0;
	virtual void doClearFromHistory(not_null<History*> history) = 0;
	virtual void doClearFromSession(not_null<Main::Session*> session) = 0;
	[[nodiscard]] virtual bool doSkipToast() const = 0;
	virtual void doMaybePlaySound(Fn<void()> playSound) = 0;
	virtual void doMaybeFlashBounce(Fn<void()> flashBounce) = 0;
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

	using NotificationSound = Media::Audio::LocalSound;
	struct NotificationInfo {
		not_null<PeerData*> peer;
		MsgId topicRootId = 0;
		MsgId itemId = 0;
		QString title;
		QString subtitle;
		QString message;
		Fn<NotificationSound()> sound;
		DisplayOptions options;
	};

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
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) = 0;

private:
	Media::Audio::LocalCache _localSoundCache;

};

class DummyManager : public NativeManager {
public:
	using NativeManager::NativeManager;

	[[nodiscard]] ManagerType type() const override {
		return ManagerType::Dummy;
	}

protected:
	void doShowNativeNotification(
		NotificationInfo &&info,
		Ui::PeerUserpicView &userpicView) override {
	}
	void doClearAllFast() override {
	}
	void doClearFromItem(not_null<HistoryItem*> item) override {
	}
	void doClearFromTopic(not_null<Data::ForumTopic*> topic) override {
	}
	void doClearFromHistory(not_null<History*> history) override {
	}
	void doClearFromSession(not_null<Main::Session*> session) override {
	}
	bool doSkipToast() const override {
		return false;
	}
	void doMaybePlaySound(Fn<void()> playSound) override {
		playSound();
	}
	void doMaybeFlashBounce(Fn<void()> flashBounce) override {
		flashBounce();
	}

};

[[nodiscard]] QString WrapFromScheduled(const QString &text);

} // namespace Window::Notifications
