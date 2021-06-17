/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class History;

namespace Data {
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
	[[nodiscard]] std::optional<ManagerType> managerType() const;

	void checkDelayed();
	void schedule(not_null<HistoryItem*> item);
	void clearFromHistory(not_null<History*> history);
	void clearIncomingFromHistory(not_null<History*> history);
	void clearFromSession(not_null<Main::Session*> session);
	void clearFromItem(not_null<HistoryItem*> item);
	void clearAll();
	void clearAllFast();
	void updateAll();

	[[nodiscard]] rpl::producer<ChangeType> settingsChanged() const;
	void notifySettingsChanged(ChangeType type);

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct SkipState {
		enum Value {
			Unknown,
			Skip,
			DontSkip
		};
		Value value = Value::Unknown;
		bool silent = false;
	};
	struct Waiter {
		MsgId msg;
		crl::time when;
		PeerData *notifyBy = nullptr;
	};

	[[nodiscard]] SkipState skipNotification(
		not_null<HistoryItem*> item) const;

	void showNext();
	void showGrouped();
	void ensureSoundCreated();

	base::flat_map<
		not_null<History*>,
		base::flat_map<MsgId, crl::time>> _whenMaps;

	base::flat_map<not_null<History*>, Waiter> _waiters;
	base::flat_map<not_null<History*>, Waiter> _settingWaiters;
	base::Timer _waitTimer;
	base::Timer _waitForAllGroupedTimer;

	base::flat_map<not_null<History*>, base::flat_map<crl::time, PeerData*>> _whenAlerts;

	std::unique_ptr<Manager> _manager;

	rpl::event_stream<ChangeType> _settingsChanged;

	std::unique_ptr<Media::Audio::Track> _soundTrack;

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
	};

	explicit Manager(not_null<System*> system) : _system(system) {
	}

	void showNotification(
			not_null<HistoryItem*> item,
			int forwardedCount) {
		doShowNotification(item, forwardedCount);
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

	void notificationActivated(NotificationId id);
	void notificationReplied(NotificationId id, const TextWithTags &reply);

	struct DisplayOptions {
		bool hideNameAndPhoto = false;
		bool hideMessageText = false;
		bool hideReplyButton = false;
	};
	[[nodiscard]] DisplayOptions getNotificationOptions(
		HistoryItem *item) const;

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
	virtual void doShowNotification(
		not_null<HistoryItem*> item,
		int forwardedCount) = 0;
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
	void doClearFromItem(not_null<HistoryItem*> item) override {
	}
	void doShowNotification(
		not_null<HistoryItem*> item,
		int forwardedCount) override;

	bool forceHideDetails() const override;

	virtual void doShowNativeNotification(
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpicView,
		MsgId msgId,
		const QString &title,
		const QString &subtitle,
		const QString &msg,
		bool hideNameAndPhoto,
		bool hideReplyButton) = 0;

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
		bool hideNameAndPhoto,
		bool hideReplyButton) override {
	}
	void doClearAllFast() override {
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
