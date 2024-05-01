/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_manager.h"

#include "base/options.h"
#include "platform/platform_notifications_manager.h"
#include "window/notifications_manager_default.h"
#include "media/audio/media_audio_track.h"
#include "media/audio/media_audio.h"
#include "mtproto/mtproto_config.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/history_view_replies_section.h"
#include "lang/lang_keys.h"
#include "data/notify/data_notify_settings.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_poll.h"
#include "base/unixtime.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "mainwindow.h"
#include "api/api_updates.h"
#include "apiwrap.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "ui/text/text_utilities.h"

#include <QtGui/QWindow>

#if __has_include(<gio/gio.hpp>)
#include <gio/gio.hpp>
#endif // __has_include(<gio/gio.hpp>)

namespace Window {
namespace Notifications {
namespace {

// not more than one sound in 500ms from one peer - grouping
constexpr auto kMinimalDelay = crl::time(100);
constexpr auto kMinimalForwardDelay = crl::time(500);
constexpr auto kMinimalAlertDelay = crl::time(500);
constexpr auto kWaitingForAllGroupedDelay = crl::time(1000);
constexpr auto kReactionNotificationEach = 60 * 60 * crl::time(1000);

#ifdef Q_OS_MAC
constexpr auto kSystemAlertDuration = crl::time(1000);
#else // !Q_OS_MAC
constexpr auto kSystemAlertDuration = crl::time(0);
#endif // Q_OS_MAC

[[nodiscard]] QString PlaceholderReactionText() {
	static const auto result = QString::fromUtf8("\xf0\x9f\x92\xad");
	return result;
}

[[nodiscard]] QString TextWithForwardedChar(
		const QString &text,
		bool forwarded) {
	static const auto result = QString::fromUtf8("\xE2\x9E\xA1\xEF\xB8\x8F");
	return forwarded ? result + text : text;
}

[[nodiscard]] QString TextWithPermanentSpoiler(
		const TextWithEntities &textWithEntities) {
	auto text = textWithEntities.text;
	for (const auto &e : textWithEntities.entities) {
		if (e.type() == EntityType::Spoiler) {
			auto replacement = QString().fill(QChar(0x259A), e.length());
			text = text.replace(
				e.offset(),
				e.length(),
				std::move(replacement));
		}
	}
	return text;
}

[[nodiscard]] QByteArray ReadRingtoneBytes(
		const std::shared_ptr<Data::DocumentMedia> &media) {
	const auto result = media->bytes();
	if (!result.isEmpty()) {
		return result;
	}
	const auto &location = media->owner()->location();
	if (!location.isEmpty() && location.accessEnable()) {
		const auto guard = gsl::finally([&] {
			location.accessDisable();
		});
		auto f = QFile(location.name());
		if (f.open(QIODevice::ReadOnly)) {
			return f.readAll();
		}
	}
	return {};
}

} // namespace

const char kOptionGNotification[] = "gnotification";

base::options::toggle OptionGNotification({
	.id = kOptionGNotification,
	.name = "GNotification",
	.description = "Force enable GLib's GNotification."
		" When disabled, autodetect is used.",
	.scope = [] {
#if __has_include(<gio/gio.hpp>)
		using namespace gi::repository;
		return bool(Gio::Application::get_default());
#else // __has_include(<gio/gio.hpp>)
		return false;
#endif // __has_include(<gio/gio.hpp>)
	},
	.restartRequired = true,
});

struct System::Waiter {
	NotificationInHistoryKey key;
	UserData *reactionSender = nullptr;
	Data::ItemNotificationType type = Data::ItemNotificationType::Message;
	crl::time when = 0;
};

System::NotificationInHistoryKey::NotificationInHistoryKey(
	Data::ItemNotification notification)
: NotificationInHistoryKey(notification.item->id, notification.type) {
}

System::NotificationInHistoryKey::NotificationInHistoryKey(
	MsgId messageId,
	Data::ItemNotificationType type)
: messageId(messageId)
, type(type) {
}

System::System()
: _waitTimer([=] { showNext(); })
, _waitForAllGroupedTimer([=] { showGrouped(); })
, _manager(std::make_unique<DummyManager>(this)) {
	settingsChanged(
	) | rpl::start_with_next([=](ChangeType type) {
		if (type == ChangeType::DesktopEnabled) {
			clearAll();
		} else if (type == ChangeType::ViewParams) {
			updateAll();
		} else if (type == ChangeType::IncludeMuted
			|| type == ChangeType::CountMessages) {
			Core::App().domain().notifyUnreadBadgeChanged();
		}
	}, lifetime());
}

void System::createManager() {
	Platform::Notifications::Create(this);
}

void System::setManager(std::unique_ptr<Manager> manager) {
	_manager = std::move(manager);
	if (!_manager) {
		_manager = std::make_unique<Default::Manager>(this);
	}
}

Manager &System::manager() const {
	Expects(_manager != nullptr);
	return *_manager;
}

Main::Session *System::findSession(uint64 sessionId) const {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			if (session->uniqueId() == sessionId) {
				return session;
			}
		}
	}
	return nullptr;
}

bool System::skipReactionNotification(not_null<HistoryItem*> item) const {
	const auto id = ReactionNotificationId{
		.itemId = item->fullId(),
		.sessionId = item->history()->session().uniqueId(),
	};
	const auto now = crl::now();
	const auto clearBefore = now - kReactionNotificationEach;
	for (auto i = begin(_sentReactionNotifications)
		; i != end(_sentReactionNotifications)
		;) {
		if (i->second <= clearBefore) {
			i = _sentReactionNotifications.erase(i);
		} else {
			++i;
		}
	}
	return !_sentReactionNotifications.emplace(id, now).second;
}

System::SkipState System::skipNotification(
		Data::ItemNotification notification) const {
	const auto item = notification.item;
	const auto type = notification.type;
	const auto messageType = (type == Data::ItemNotificationType::Message);
	if (!item->notificationThread()->currentNotification()
		|| (messageType && item->skipNotification())
		|| (type == Data::ItemNotificationType::Reaction
			&& skipReactionNotification(item))) {
		return { SkipState::Skip };
	}
	return computeSkipState(notification);
}

System::SkipState System::computeSkipState(
		Data::ItemNotification notification) const {
	const auto type = notification.type;
	const auto item = notification.item;
	const auto thread = item->notificationThread();
	const auto notifySettings = &thread->owner().notifySettings();
	const auto messageType = (type == Data::ItemNotificationType::Message);
	const auto withSilent = [&](
			SkipState::Value value,
			bool forceSilent = false) {
		return SkipState{
			.value = value,
			.silent = (forceSilent
				|| !messageType
				|| item->isSilent()
				|| notifySettings->sound(thread).none),
		};
	};
	const auto showForMuted = messageType
		&& item->out()
		&& item->isFromScheduled();
	const auto notifyBy = messageType
		? item->specialNotificationPeer()
		: notification.reactionSender;
	if (Core::Quitting()) {
		return { SkipState::Skip };
	} else if (!Core::App().settings().notifyFromAll()
		&& &thread->session().account() != &Core::App().domain().active()) {
		return { SkipState::Skip };
	}

	if (messageType) {
		notifySettings->request(thread);
	} else if (notifyBy->blockStatus() == PeerData::BlockStatus::Unknown) {
		notifyBy->updateFull();
	}
	if (notifyBy) {
		notifySettings->request(notifyBy);
	}

	if (messageType && notifySettings->muteUnknown(thread)) {
		return { SkipState::Unknown };
	} else if (messageType && !notifySettings->isMuted(thread)) {
		return withSilent(SkipState::DontSkip);
	} else if (!notifyBy) {
		return withSilent(
			showForMuted ? SkipState::DontSkip : SkipState::Skip,
			showForMuted);
	} else if (notifySettings->muteUnknown(notifyBy)
		|| (!messageType
			&& notifyBy->blockStatus() == PeerData::BlockStatus::Unknown)) {
		return withSilent(SkipState::Unknown);
	} else if (!notifySettings->isMuted(notifyBy)
		&& (messageType || !notifyBy->isBlocked())) {
		return withSilent(SkipState::DontSkip);
	} else {
		return withSilent(
			showForMuted ? SkipState::DontSkip : SkipState::Skip,
			showForMuted);
	}
}

System::Timing System::countTiming(
		not_null<Data::Thread*> thread,
		crl::time minimalDelay) const {
	auto delay = minimalDelay;
	const auto t = base::unixtime::now();
	const auto ms = crl::now();
	const auto &updates = thread->session().updates();
	const auto &config = thread->session().serverConfig();
	const bool isOnline = updates.lastWasOnline();
	const auto otherNotOld = ((cOtherOnline() * 1000LL) + config.onlineCloudTimeout > t * 1000LL);
	const bool otherLaterThanMe = (cOtherOnline() * 1000LL + (ms - updates.lastSetOnline()) > t * 1000LL);
	if (!isOnline && otherNotOld && otherLaterThanMe) {
		delay = config.notifyCloudDelay;
	} else if (cOtherOnline() >= t) {
		delay = config.notifyDefaultDelay;
	}
	return {
		.delay = delay,
		.when = ms + delay,
	};
}

void System::registerThread(not_null<Data::Thread*> thread) {
	if (const auto topic = thread->asTopic()) {
		const auto &[i, ok] = _watchedTopics.emplace(topic, rpl::lifetime());
		if (ok) {
			topic->destroyed() | rpl::start_with_next([=] {
				clearFromTopic(topic);
			}, i->second);
		}
	}
}

void System::schedule(Data::ItemNotification notification) {
	Expects(_manager != nullptr);

	const auto item = notification.item;
	const auto type = notification.type;
	const auto thread = item->notificationThread();
	const auto skip = skipNotification(notification);
	if (skip.value == SkipState::Skip) {
		thread->popNotification(notification);
		return;
	}
	const auto ready = (skip.value != SkipState::Unknown)
		&& item->notificationReady();

	const auto minimalDelay = (type == Data::ItemNotificationType::Reaction)
		? kMinimalDelay
		: item->Has<HistoryMessageForwarded>()
		? kMinimalForwardDelay
		: kMinimalDelay;
	const auto timing = countTiming(thread, minimalDelay);
	const auto notifyBy = (type == Data::ItemNotificationType::Message)
		? item->specialNotificationPeer()
		: notification.reactionSender;
	if (!skip.silent) {
		registerThread(thread);
		_whenAlerts[thread].emplace(timing.when, notifyBy);
	}
	if (Core::App().settings().desktopNotify()
		&& !_manager->skipToast()) {
		registerThread(thread);
		const auto key = NotificationInHistoryKey(notification);
		auto &whenMap = _whenMaps[thread];
		if (whenMap.find(key) == whenMap.end()) {
			whenMap.emplace(key, timing.when);
		}

		auto &addTo = ready ? _waiters : _settingWaiters;
		const auto it = addTo.find(thread);
		if (it == addTo.end() || it->second.when > timing.when) {
			addTo.emplace(thread, Waiter{
				.key = key,
				.reactionSender = notification.reactionSender,
				.type = notification.type,
				.when = timing.when,
			});
		}
	}
	if (ready) {
		if (!_waitTimer.isActive()
			|| _waitTimer.remainingTime() > timing.delay) {
			_waitTimer.callOnce(timing.delay);
		}
	}
}

void System::clearAll() {
	if (_manager) {
		_manager->clearAll();
	}

	for (const auto &[thread, _] : _whenMaps) {
		thread->clearNotifications();
	}
	_whenMaps.clear();
	_whenAlerts.clear();
	_waiters.clear();
	_settingWaiters.clear();
	_watchedTopics.clear();
}

void System::clearFromTopic(not_null<Data::ForumTopic*> topic) {
	if (_manager) {
		_manager->clearFromTopic(topic);
	}

	topic->clearNotifications();
	_whenMaps.remove(topic);
	_whenAlerts.remove(topic);
	_waiters.remove(topic);
	_settingWaiters.remove(topic);

	_watchedTopics.remove(topic);

	_waitTimer.cancel();
	showNext();
}

void System::clearForThreadIf(Fn<bool(not_null<Data::Thread*>)> predicate) {
	for (auto i = _whenMaps.begin(); i != _whenMaps.end();) {
		const auto thread = i->first;
		if (!predicate(thread)) {
			++i;
			continue;
		}
		i = _whenMaps.erase(i);

		thread->clearNotifications();
		_whenAlerts.remove(thread);
		_waiters.remove(thread);
		_settingWaiters.remove(thread);
		if (const auto topic = thread->asTopic()) {
			_watchedTopics.remove(topic);
		}
	}
	const auto clearFrom = [&](auto &map) {
		for (auto i = map.begin(); i != map.end();) {
			const auto thread = i->first;
			if (predicate(thread)) {
				if (const auto topic = thread->asTopic()) {
					_watchedTopics.remove(topic);
				}
				i = map.erase(i);
			} else {
				++i;
			}
		}
	};
	clearFrom(_whenAlerts);
	clearFrom(_waiters);
	clearFrom(_settingWaiters);

	_waitTimer.cancel();
	showNext();
}

void System::clearFromHistory(not_null<History*> history) {
	if (_manager) {
		_manager->clearFromHistory(history);
	}
	clearForThreadIf([&](not_null<Data::Thread*> thread) {
		return (thread->owningHistory() == history);
	});
}

void System::clearFromSession(not_null<Main::Session*> session) {
	if (_manager) {
		_manager->clearFromSession(session);
	}
	clearForThreadIf([&](not_null<Data::Thread*> thread) {
		return (&thread->session() == session);
	});
}

void System::clearIncomingFromHistory(not_null<History*> history) {
	if (_manager) {
		_manager->clearFromHistory(history);
	}
	history->clearIncomingNotifications();
	_whenAlerts.remove(history);
}

void System::clearIncomingFromTopic(not_null<Data::ForumTopic*> topic) {
	if (_manager) {
		_manager->clearFromTopic(topic);
	}
	topic->clearIncomingNotifications();
	_whenAlerts.remove(topic);
}

void System::clearFromItem(not_null<HistoryItem*> item) {
	if (_manager) {
		_manager->clearFromItem(item);
	}
}

void System::clearAllFast() {
	if (_manager) {
		_manager->clearAllFast();
	}

	_whenMaps.clear();
	_whenAlerts.clear();
	_waiters.clear();
	_settingWaiters.clear();
	_watchedTopics.clear();
}

void System::checkDelayed() {
	for (auto i = _settingWaiters.begin(); i != _settingWaiters.end();) {
		const auto remove = [&] {
			const auto thread = i->first;
			const auto peer = thread->peer();
			const auto fullId = FullMsgId(peer->id, i->second.key.messageId);
			const auto item = thread->owner().message(fullId);
			if (!item) {
				return true;
			}
			const auto state = computeSkipState({
				.item = item,
				.reactionSender = i->second.reactionSender,
				.type = i->second.type,
			});
			if (state.value == SkipState::Skip) {
				return true;
			} else if (state.value == SkipState::Unknown
				|| !item->notificationReady()) {
				return false;
			}
			_waiters.emplace(i->first, i->second);
			return true;
		}();
		if (remove) {
			i = _settingWaiters.erase(i);
		} else {
			++i;
		}
	}
	_waitTimer.cancel();
	showNext();
}

void System::showGrouped() {
	Expects(_manager != nullptr);

	if (const auto session = findSession(_lastHistorySessionId)) {
		if (const auto lastItem = session->data().message(_lastHistoryItemId)) {
			_waitForAllGroupedTimer.cancel();
			_manager->showNotification({
				.item = lastItem,
				.forwardedCount = _lastForwardedCount,
			});
			_lastForwardedCount = 0;
			_lastHistoryItemId = FullMsgId();
			_lastHistorySessionId = 0;
		}
	}
}

void System::showNext() {
	Expects(_manager != nullptr);

	if (Core::Quitting()) {
		return;
	}

	const auto isSameGroup = [=](HistoryItem *item) {
		if (!_lastHistorySessionId || !_lastHistoryItemId || !item) {
			return false;
		} else if (item->history()->session().uniqueId()
			!= _lastHistorySessionId) {
			return false;
		}
		const auto lastItem = item->history()->owner().message(
			_lastHistoryItemId);
		if (lastItem) {
			return (lastItem->groupId() == item->groupId())
				|| (lastItem->author() == item->author());
		}
		return false;
	};

	auto ms = crl::now(), nextAlert = crl::time(0);
	auto alertThread = (Data::Thread*)nullptr;
	for (auto i = _whenAlerts.begin(); i != _whenAlerts.end();) {
		while (!i->second.empty() && i->second.begin()->first <= ms) {
			const auto thread = i->first;
			const auto notifySettings = &thread->owner().notifySettings();
			const auto threadUnknown = notifySettings->muteUnknown(thread);
			const auto threadAlert = !threadUnknown
				&& !notifySettings->isMuted(thread);
			const auto from = i->second.begin()->second;
			const auto fromUnknown = (!from
				|| notifySettings->muteUnknown(from));
			const auto fromAlert = !fromUnknown
				&& !notifySettings->isMuted(from);
			if (threadAlert || fromAlert) {
				alertThread = thread;
			}
			while (!i->second.empty()
				&& i->second.begin()->first <= ms + kMinimalAlertDelay) {
				i->second.erase(i->second.begin());
			}
		}
		if (i->second.empty()) {
			i = _whenAlerts.erase(i);
		} else {
			if (!nextAlert || nextAlert > i->second.begin()->first) {
				nextAlert = i->second.begin()->first;
			}
			++i;
		}
	}
	const auto &settings = Core::App().settings();
	if (alertThread) {
		if (settings.flashBounceNotify()) {
			const auto peer = alertThread->peer();
			if (const auto window = Core::App().windowFor(peer)) {
				if (const auto controller = window->sessionController()) {
					_manager->maybeFlashBounce(crl::guard(controller, [=] {
						if (const auto handle = window->widget()->windowHandle()) {
							handle->alert(kSystemAlertDuration);
							// (handle, SLOT(_q_clearAlert())); in the future.
						}
					}));
				}
			}
		}
		if (settings.soundNotify()) {
			const auto owner = &alertThread->owner();
			const auto id = owner->notifySettings().sound(alertThread).id;
			_manager->maybePlaySound(crl::guard(&owner->session(), [=] {
				const auto track = lookupSound(owner, id);
				track->playOnce();
				Media::Player::mixer()->suppressAll(track->getLengthMs());
				Media::Player::mixer()->scheduleFaderCallback();
			}));
		}
	}

	if (_waiters.empty() || !settings.desktopNotify() || _manager->skipToast()) {
		if (nextAlert) {
			_waitTimer.callOnce(nextAlert - ms);
		}
		return;
	}

	while (true) {
		auto next = 0LL;
		auto notify = std::optional<Data::ItemNotification>();
		auto notifyThread = (Data::Thread*)nullptr;
		for (auto i = _waiters.begin(); i != _waiters.end();) {
			const auto thread = i->first;
			auto current = thread->currentNotification();
			if (current && current->item->id != i->second.key.messageId) {
				auto j = _whenMaps.find(thread);
				if (j == _whenMaps.end()) {
					thread->clearNotifications();
					i = _waiters.erase(i);
					continue;
				}
				do {
					auto k = j->second.find(*current);
					if (k != j->second.cend()) {
						i->second.key = k->first;
						i->second.when = k->second;
						break;
					}
					thread->skipNotification();
					current = thread->currentNotification();
				} while (current);
			}
			if (!current) {
				_whenMaps.remove(thread);
				i = _waiters.erase(i);
				continue;
			}
			auto when = i->second.when;
			if (!notify || next > when) {
				next = when;
				notify = current,
				notifyThread = thread;
			}
			++i;
		}
		if (!notify) {
			break;
		} else if (next > ms) {
			if (nextAlert && nextAlert < next) {
				next = nextAlert;
				nextAlert = 0;
			}
			_waitTimer.callOnce(next - ms);
			break;
		}
		const auto notifyItem = notify->item;
		const auto messageType = (notify->type
			== Data::ItemNotificationType::Message);
		const auto isForwarded = messageType
			&& notifyItem->Has<HistoryMessageForwarded>();
		const auto isAlbum = messageType
			&& notifyItem->groupId();

		// Forwarded and album notify grouping.
		auto groupedItem = (isForwarded || isAlbum)
			? notifyItem.get()
			: nullptr;
		auto forwardedCount = isForwarded ? 1 : 0;

		const auto thread = notifyItem->notificationThread();
		const auto j = _whenMaps.find(thread);
		if (j == _whenMaps.cend()) {
			thread->clearNotifications();
		} else {
			while (true) {
				auto nextNotify = std::optional<Data::ItemNotification>();
				thread->skipNotification();
				if (!thread->hasNotification()) {
					break;
				}

				j->second.remove({
					(groupedItem ? groupedItem : notifyItem.get())->id,
					notify->type,
				});
				do {
					const auto k = j->second.find(
						thread->currentNotification());
					if (k != j->second.cend()) {
						nextNotify = thread->currentNotification();
						_waiters.emplace(notifyThread, Waiter{
							.key = k->first,
							.when = k->second
						});
						break;
					}
					thread->skipNotification();
				} while (thread->hasNotification());
				if (!nextNotify || !groupedItem) {
					break;
				}
				const auto nextMessageNotification
					= (nextNotify->type
						== Data::ItemNotificationType::Message);
				const auto canNextBeGrouped = nextMessageNotification
					&& ((isForwarded
						&& nextNotify->item->Has<HistoryMessageForwarded>())
						|| (isAlbum && nextNotify->item->groupId()));
				const auto nextItem = canNextBeGrouped
					? nextNotify->item.get()
					: nullptr;
				if (nextItem
					&& qAbs(int64(nextItem->date()) - int64(groupedItem->date())) < 2) {
					if (isForwarded
						&& groupedItem->author() == nextItem->author()) {
						++forwardedCount;
						groupedItem = nextItem;
						continue;
					}
					if (isAlbum
						&& groupedItem->groupId() == nextItem->groupId()) {
						groupedItem = nextItem;
						continue;
					}
				}
				break;
			}
		}

		if (!_lastHistoryItemId && groupedItem) {
			_lastHistorySessionId = groupedItem->history()->session().uniqueId();
			_lastHistoryItemId = groupedItem->fullId();
		}

		// If the current notification is grouped.
		if (isAlbum || isForwarded) {
			// If the previous notification is grouped
			// then reset the timer.
			if (_waitForAllGroupedTimer.isActive()) {
				_waitForAllGroupedTimer.cancel();
				// If this is not the same group
				// then show the previous group immediately.
				if (!isSameGroup(groupedItem)) {
					showGrouped();
				}
			}
			// We have to wait until all the messages in this group are loaded.
			_lastForwardedCount += forwardedCount;
			_lastHistorySessionId = groupedItem->history()->session().uniqueId();
			_lastHistoryItemId = groupedItem->fullId();
			_waitForAllGroupedTimer.callOnce(kWaitingForAllGroupedDelay);
		} else {
			// If the current notification is not grouped
			// then there is no reason to wait for the timer
			// to show the previous notification.
			showGrouped();
			const auto reactionNotification
				= (notify->type == Data::ItemNotificationType::Reaction);
			const auto reaction = reactionNotification
				? notify->item->lookupUnreadReaction(notify->reactionSender)
				: Data::ReactionId();
			if (!reactionNotification || !reaction.empty()) {
				_manager->showNotification({
					.item = notify->item,
					.forwardedCount = forwardedCount,
					.reactionFrom = notify->reactionSender,
					.reactionId = reaction,
				});
			}
		}

		if (!thread->hasNotification()) {
			_waiters.remove(thread);
			_whenMaps.remove(thread);
		}
	}
	if (nextAlert) {
		_waitTimer.callOnce(nextAlert - ms);
	}
}

not_null<Media::Audio::Track*> System::lookupSound(
		not_null<Data::Session*> owner,
		DocumentId id) {
	if (!id) {
		ensureSoundCreated();
		return _soundTrack.get();
	}
	const auto i = _customSoundTracks.find(id);
	if (i != end(_customSoundTracks)) {
		return i->second.get();
	}
	const auto &notifySettings = owner->notifySettings();
	if (const auto custom = notifySettings.lookupRingtone(id)) {
		const auto bytes = ReadRingtoneBytes(custom);
		if (!bytes.isEmpty()) {
			const auto j = _customSoundTracks.emplace(
				id,
				Media::Audio::Current().createTrack()
			).first;
			j->second->fillFromData(bytes::make_vector(bytes));
			return j->second.get();
		}
	}
	ensureSoundCreated();
	return _soundTrack.get();
}

void System::ensureSoundCreated() {
	if (_soundTrack) {
		return;
	}

	_soundTrack = Media::Audio::Current().createTrack();
	_soundTrack->fillFromFile(
		Core::App().settings().getSoundPath(u"msg_incoming"_q));
}

void System::updateAll() {
	if (_manager) {
		_manager->updateAll();
	}
}

rpl::producer<ChangeType> System::settingsChanged() const {
	return _settingsChanged.events();
}

void System::notifySettingsChanged(ChangeType type) {
	return _settingsChanged.fire(std::move(type));
}

void System::playSound(not_null<Main::Session*> session, DocumentId id) {
	lookupSound(&session->data(), id)->playOnce();
}

Manager::DisplayOptions Manager::getNotificationOptions(
		HistoryItem *item,
		Data::ItemNotificationType type) const {
	const auto hideEverything = Core::App().passcodeLocked()
		|| forceHideDetails();
	const auto view = Core::App().settings().notifyView();
	const auto peer = item ? item->history()->peer.get() : nullptr;
	const auto topic = item ? item->topic() : nullptr;

	auto result = DisplayOptions();
	result.hideNameAndPhoto = hideEverything
		|| (view > Core::Settings::NotifyView::ShowName);
	result.hideMessageText = hideEverything
		|| (view > Core::Settings::NotifyView::ShowPreview);
	result.hideMarkAsRead = result.hideMessageText
		|| (type != Data::ItemNotificationType::Message)
		|| !item
		|| ((item->out() || peer->isSelf()) && item->isFromScheduled());
	result.hideReplyButton = result.hideMarkAsRead
		|| (!Data::CanSendTexts(peer)
			&& (!topic || !Data::CanSendTexts(topic)))
		|| peer->isBroadcast()
		|| (peer->slowmodeSecondsLeft() > 0);
	result.spoilerLoginCode = item
		&& !item->out()
		&& peer->isNotificationsUser()
		&& Core::App().isSharingScreen();
	return result;
}

TextWithEntities Manager::ComposeReactionEmoji(
		not_null<Main::Session*> session,
		const Data::ReactionId &reaction) {
	if (const auto emoji = std::get_if<QString>(&reaction.data)) {
		return TextWithEntities{ *emoji };
	}
	const auto id = v::get<DocumentId>(reaction.data);
	const auto document = session->data().document(id);
	const auto sticker = document->sticker();
	const auto text = sticker ? sticker->alt : PlaceholderReactionText();
	return TextWithEntities{
		text,
		{
			EntityInText(
				EntityType::CustomEmoji,
				0,
				text.size(),
				Data::SerializeCustomEmojiId(id))
		}
	};
}

TextWithEntities Manager::ComposeReactionNotification(
		not_null<HistoryItem*> item,
		const Data::ReactionId &reaction,
		bool hideContent) {
	const auto reactionWithEntities = ComposeReactionEmoji(
		&item->history()->session(),
		reaction);
	const auto simple = [&](const auto &phrase) {
		return phrase(
			tr::now,
			lt_reaction,
			reactionWithEntities,
			Ui::Text::WithEntities);
	};
	if (hideContent) {
		return simple(tr::lng_reaction_notext);
	}
	const auto media = item->media();
	const auto text = [&] {
		return tr::lng_reaction_text(
			tr::now,
			lt_reaction,
			reactionWithEntities,
			lt_text,
			item->notificationText(),
			Ui::Text::WithEntities);
	};
	if (!media || media->webpage()) {
		return text();
	} else if (media->photo()) {
		return simple(tr::lng_reaction_photo);
	} else if (const auto document = media->document()) {
		if (document->isVoiceMessage()) {
			return simple(tr::lng_reaction_voice_message);
		} else if (document->isVideoMessage()) {
			return simple(tr::lng_reaction_video_message);
		} else if (document->isAnimation()) {
			return simple(tr::lng_reaction_gif);
		} else if (document->isVideoFile()) {
			return simple(tr::lng_reaction_video);
		} else if (const auto sticker = document->sticker()) {
			return tr::lng_reaction_sticker(
				tr::now,
				lt_reaction,
				reactionWithEntities,
				lt_emoji,
				Ui::Text::WithEntities(sticker->alt),
				Ui::Text::WithEntities);
		}
		return simple(tr::lng_reaction_document);
	} else if (const auto contact = media->sharedContact()) {
		const auto name = contact->firstName.isEmpty()
			? contact->lastName
			: contact->lastName.isEmpty()
			? contact->firstName
			: tr::lng_full_name(
				tr::now,
				lt_first_name,
				contact->firstName,
				lt_last_name,
				contact->lastName);
		return tr::lng_reaction_contact(
			tr::now,
			lt_reaction,
			reactionWithEntities,
			lt_name,
			Ui::Text::WithEntities(name),
			Ui::Text::WithEntities);
	} else if (media->location()) {
		return simple(tr::lng_reaction_location);
		// lng_reaction_live_location not used right now :(
	} else if (const auto poll = media->poll()) {
		return (poll->quiz()
			? tr::lng_reaction_quiz
			: tr::lng_reaction_poll)(
				tr::now,
				lt_reaction,
				reactionWithEntities,
				lt_title,
				poll->question,
				Ui::Text::WithEntities);
	} else if (media->game()) {
		return simple(tr::lng_reaction_game);
	} else if (media->invoice()) {
		return simple(tr::lng_reaction_invoice);
	}
	return text();
}

TextWithEntities Manager::addTargetAccountName(
		TextWithEntities title,
		not_null<Main::Session*> session) {
	const auto add = [&] {
		for (const auto &[index, account] : Core::App().domain().accounts()) {
			if (const auto other = account->maybeSession()) {
				if (other != session) {
					return true;
				}
			}
		}
		return false;
	}();
	if (!add) {
		return title;
	}
	return title.append(accountNameSeparator()).append(
		(session->user()->username().isEmpty()
			? session->user()->name()
			: session->user()->username()));
}

QString Manager::addTargetAccountName(
		const QString &title,
		not_null<Main::Session*> session) {
	return addTargetAccountName(TextWithEntities{ title }, session).text;
}

QString Manager::accountNameSeparator() {
	return QString::fromUtf8(" \xE2\x9E\x9C ");
}

void Manager::notificationActivated(
		NotificationId id,
		const TextWithTags &reply) {
	onBeforeNotificationActivated(id);
	if (const auto session = system()->findSession(id.contextId.sessionId)) {
		if (session->windows().empty()) {
			Core::App().domain().activate(&session->account());
		}
		if (!session->windows().empty()) {
			const auto window = session->windows().front();
			const auto history = session->data().history(
				id.contextId.peerId);
			const auto item = history->owner().message(
				history->peer,
				id.msgId);
			const auto topic = item ? item->topic() : nullptr;
			if (!reply.text.isEmpty()) {
				const auto topicRootId = topic
					? topic->rootId()
					: id.contextId.topicRootId;
				const auto replyToId = (id.msgId > 0
					&& !history->peer->isUser()
					&& id.msgId != topicRootId)
					? FullMsgId(history->peer->id, id.msgId)
					: FullMsgId();
				auto draft = std::make_unique<Data::Draft>(
					reply,
					FullReplyTo{
						.messageId = replyToId,
						.topicRootId = topicRootId,
					},
					MessageCursor{
						int(reply.text.size()),
						int(reply.text.size()),
						Ui::kQFixedMax,
					},
					Data::WebPageDraft());
				history->setLocalDraft(std::move(draft));
			}
			window->widget()->showFromTray();
			window->widget()->reActivateWindow();
			if (Core::App().passcodeLocked()) {
				window->widget()->setInnerFocus();
				system()->clearAll();
			} else {
				openNotificationMessage(history, id.msgId);
			}
			onAfterNotificationActivated(id, window);
		}
	}
}

void Manager::openNotificationMessage(
		not_null<History*> history,
		MsgId messageId) {
	const auto item = history->owner().message(history->peer, messageId);
	const auto openExactlyMessage = !history->peer->isBroadcast()
		&& item
		&& item->isRegular()
		&& (item->out() || (item->mentionsMe() && !history->peer->isUser()));
	const auto topic = item ? item->topic() : nullptr;
	const auto separate = Core::App().separateWindowForPeer(history->peer);
	const auto window = separate
		? separate->sessionController()
		: history->session().tryResolveWindow();
	const auto itemId = openExactlyMessage ? messageId : ShowAtUnreadMsgId;
	if (window) {
		if (topic) {
			window->showSection(
				std::make_shared<HistoryView::RepliesMemento>(
					history,
					topic->rootId(),
					itemId),
				SectionShow::Way::Forward);
		} else {
			window->showPeerHistory(
				history->peer->id,
				SectionShow::Way::Forward,
				itemId);
		}
	}
	if (topic) {
		system()->clearFromTopic(topic);
	} else {
		system()->clearFromHistory(history);
	}
}

void Manager::notificationReplied(
		NotificationId id,
		const TextWithTags &reply) {
	if (!id.contextId.sessionId || !id.contextId.peerId) {
		return;
	}

	const auto session = system()->findSession(id.contextId.sessionId);
	if (!session) {
		return;
	}
	const auto history = session->data().history(id.contextId.peerId);
	const auto item = history->owner().message(history->peer, id.msgId);
	const auto topic = item ? item->topic() : nullptr;
	const auto topicRootId = topic
		? topic->rootId()
		: id.contextId.topicRootId;

	auto message = Api::MessageToSend(Api::SendAction(history));
	message.textWithTags = reply;
	const auto replyToId = (id.msgId > 0 && !history->peer->isUser()
		&& id.msgId != topicRootId)
		? id.msgId
		: history->peer->isForum()
		? topicRootId
		: MsgId(0);
	message.action.replyTo = {
		.messageId = { replyToId ? history->peer->id : 0, replyToId },
		.topicRootId = topic ? topic->rootId() : 0,
	};
	message.action.clearDraft = false;
	history->session().api().sendMessage(std::move(message));

	if (item && item->isUnreadMention() && !item->isIncomingUnreadMedia()) {
		history->session().api().markContentsRead(item);
	}
}

void NativeManager::doShowNotification(NotificationFields &&fields) {
	const auto options = getNotificationOptions(
		fields.item,
		(fields.reactionFrom
			? Data::ItemNotificationType::Reaction
			: Data::ItemNotificationType::Message));
	const auto item = fields.item;
	const auto peer = item->history()->peer;
	const auto reactionFrom = fields.reactionFrom;
	if (reactionFrom && options.hideNameAndPhoto) {
		return;
	}
	const auto scheduled = !options.hideNameAndPhoto
		&& !reactionFrom
		&& (item->out() || peer->isSelf())
		&& item->isFromScheduled();
	const auto topicWithChat = [&] {
		const auto name = peer->name();
		const auto topic = item->topic();
		return topic ? (topic->title() + u" ("_q + name + ')') : name;
	};
	const auto title = options.hideNameAndPhoto
		? AppName.utf16()
		: (scheduled && peer->isSelf())
		? tr::lng_notification_reminder(tr::now)
		: topicWithChat();
	const auto fullTitle = addTargetAccountName(title, &peer->session());
	const auto subtitle = reactionFrom
		? (reactionFrom != peer ? reactionFrom->name() : QString())
		: options.hideNameAndPhoto
		? QString()
		: item->notificationHeader();
	const auto text = reactionFrom
		? TextWithPermanentSpoiler(ComposeReactionNotification(
			item,
			fields.reactionId,
			options.hideMessageText))
		: options.hideMessageText
		? tr::lng_notification_preview(tr::now)
		: (fields.forwardedCount > 1)
		? tr::lng_forward_messages(tr::now, lt_count, fields.forwardedCount)
		: item->groupId()
		? tr::lng_in_dlg_album(tr::now)
		: TextWithForwardedChar(
			TextWithPermanentSpoiler(item->notificationText({
				.spoilerLoginCode = options.spoilerLoginCode,
			})),
			(fields.forwardedCount == 1));

	// #TODO optimize
	auto userpicView = item->history()->peer->createUserpicView();
	doShowNativeNotification(
		item->history()->peer,
		item->topicRootId(),
		userpicView,
		item->id,
		scheduled ? WrapFromScheduled(fullTitle) : fullTitle,
		subtitle,
		text,
		options);
}

bool NativeManager::forceHideDetails() const {
	return Core::App().screenIsLocked();
}

System::~System() = default;

QString WrapFromScheduled(const QString &text) {
	return QString::fromUtf8("\xF0\x9F\x93\x85 ") + text;
}

} // namespace Notifications
} // namespace Window
