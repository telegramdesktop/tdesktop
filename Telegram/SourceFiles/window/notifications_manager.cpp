/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_manager.h"

#include "platform/platform_notifications_manager.h"
#include "window/notifications_manager_default.h"
#include "media/audio/media_audio_track.h"
#include "media/audio/media_audio.h"
#include "mtproto/mtproto_config.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_channel.h"
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
#include "facades.h"

#include <QtGui/QWindow>

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

QString TextWithPermanentSpoiler(const TextWithEntities &textWithEntities) {
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

} // namespace

struct System::Waiter {
	NotificationInHistoryKey key;
	UserData *reactionSender = nullptr;
	ItemNotificationType type = ItemNotificationType::Message;
	crl::time when = 0;
};

System::NotificationInHistoryKey::NotificationInHistoryKey(
	ItemNotification notification)
: NotificationInHistoryKey(notification.item->id, notification.type) {
}

System::NotificationInHistoryKey::NotificationInHistoryKey(
	MsgId messageId,
	ItemNotificationType type)
: messageId(messageId)
, type(type) {
}

System::System()
: _waitTimer([=] { showNext(); })
, _waitForAllGroupedTimer([=] { showGrouped(); }) {
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

std::optional<ManagerType> System::managerType() const {
	if (_manager) {
		return _manager->type();
	}
	return std::nullopt;
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
		ItemNotification notification) const {
	const auto item = notification.item;
	const auto type = notification.type;
	const auto messageNotification = (type == ItemNotificationType::Message);
	if (!item->history()->currentNotification()
		|| (messageNotification && item->skipNotification())
		|| (type == ItemNotificationType::Reaction
			&& skipReactionNotification(item))) {
		return { SkipState::Skip };
	}
	return computeSkipState(notification);
}

System::SkipState System::computeSkipState(
		ItemNotification notification) const {
	const auto type = notification.type;
	const auto item = notification.item;
	const auto history = item->history();
	const auto messageNotification = (type == ItemNotificationType::Message);
	const auto withSilent = [&](
			SkipState::Value value,
			bool forceSilent = false) {
		return SkipState{
			.value = value,
			.silent = (forceSilent
				|| !messageNotification
				|| item->isSilent()
				|| history->owner().notifySettings().sound(
					history->peer).none),
		};
	};
	const auto showForMuted = messageNotification
		&& item->out()
		&& item->isFromScheduled();
	const auto notifyBy = messageNotification
		? item->specialNotificationPeer()
		: notification.reactionSender;
	if (Core::Quitting()) {
		return { SkipState::Skip };
	} else if (!Core::App().settings().notifyFromAll()
		&& &history->session().account() != &Core::App().domain().active()) {
		return { SkipState::Skip };
	}

	if (messageNotification) {
		history->owner().notifySettings().request(
			history->peer);
	} else if (notifyBy->blockStatus() == PeerData::BlockStatus::Unknown) {
		notifyBy->updateFull();
	}
	if (notifyBy) {
		history->owner().notifySettings().request(notifyBy);
	}

	if (messageNotification
		&& history->owner().notifySettings().muteUnknown(history->peer)) {
		return { SkipState::Unknown };
	} else if (messageNotification
		&& !history->owner().notifySettings().isMuted(history->peer)) {
		return withSilent(SkipState::DontSkip);
	} else if (!notifyBy) {
		return withSilent(
			showForMuted ? SkipState::DontSkip : SkipState::Skip,
			showForMuted);
	} else if (history->owner().notifySettings().muteUnknown(notifyBy)
		|| (!messageNotification
			&& notifyBy->blockStatus() == PeerData::BlockStatus::Unknown)) {
		return withSilent(SkipState::Unknown);
	} else if (!history->owner().notifySettings().isMuted(notifyBy)
		&& (messageNotification || !notifyBy->isBlocked())) {
		return withSilent(SkipState::DontSkip);
	} else {
		return withSilent(
			showForMuted ? SkipState::DontSkip : SkipState::Skip,
			showForMuted);
	}
}

System::Timing System::countTiming(
		not_null<History*> history,
		crl::time minimalDelay) const {
	auto delay = minimalDelay;
	const auto t = base::unixtime::now();
	const auto ms = crl::now();
	const auto &updates = history->session().updates();
	const auto &config = history->session().serverConfig();
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

void System::schedule(ItemNotification notification) {
	Expects(_manager != nullptr);

	const auto item = notification.item;
	const auto type = notification.type;
	const auto history = item->history();
	const auto skip = skipNotification(notification);
	if (skip.value == SkipState::Skip) {
		history->popNotification(notification);
		return;
	}
	const auto ready = (skip.value != SkipState::Unknown)
		&& item->notificationReady();

	const auto minimalDelay = (type == ItemNotificationType::Reaction)
		? kMinimalDelay
		: item->Has<HistoryMessageForwarded>()
		? kMinimalForwardDelay
		: kMinimalDelay;
	const auto timing = countTiming(history, minimalDelay);
	const auto notifyBy = (type == ItemNotificationType::Message)
		? item->specialNotificationPeer()
		: notification.reactionSender;
	if (!skip.silent) {
		_whenAlerts[history].emplace(timing.when, notifyBy);
	}
	if (Core::App().settings().desktopNotify()
		&& !_manager->skipToast()) {
		const auto key = NotificationInHistoryKey(notification);
		auto &whenMap = _whenMaps[history];
		if (whenMap.find(key) == whenMap.end()) {
			whenMap.emplace(key, timing.when);
		}

		auto &addTo = ready ? _waiters : _settingWaiters;
		const auto it = addTo.find(history);
		if (it == addTo.end() || it->second.when > timing.when) {
			addTo.emplace(history, Waiter{
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

	for (auto i = _whenMaps.cbegin(), e = _whenMaps.cend(); i != e; ++i) {
		i->first->clearNotifications();
	}
	_whenMaps.clear();
	_whenAlerts.clear();
	_waiters.clear();
	_settingWaiters.clear();
}

void System::clearFromHistory(not_null<History*> history) {
	if (_manager) {
		_manager->clearFromHistory(history);
	}

	history->clearNotifications();
	_whenMaps.remove(history);
	_whenAlerts.remove(history);
	_waiters.remove(history);
	_settingWaiters.remove(history);

	_waitTimer.cancel();
	showNext();
}

void System::clearFromSession(not_null<Main::Session*> session) {
	if (_manager) {
		_manager->clearFromSession(session);
	}

	for (auto i = _whenMaps.begin(); i != _whenMaps.end();) {
		const auto history = i->first;
		if (&history->session() != session) {
			++i;
			continue;
		}
		history->clearNotifications();
		i = _whenMaps.erase(i);
		_whenAlerts.remove(history);
		_waiters.remove(history);
		_settingWaiters.remove(history);
	}
	const auto clearFrom = [&](auto &map) {
		for (auto i = map.begin(); i != map.end();) {
			if (&i->first->session() == session) {
				i = map.erase(i);
			} else {
				++i;
			}
		}
	};
	clearFrom(_whenAlerts);
	clearFrom(_waiters);
	clearFrom(_settingWaiters);
}

void System::clearIncomingFromHistory(not_null<History*> history) {
	if (_manager) {
		_manager->clearFromHistory(history);
	}
	history->clearIncomingNotifications();
	_whenAlerts.remove(history);
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
}

void System::checkDelayed() {
	for (auto i = _settingWaiters.begin(); i != _settingWaiters.end();) {
		const auto remove = [&] {
			const auto history = i->first;
			const auto peer = history->peer;
			const auto fullId = FullMsgId(peer->id, i->second.key.messageId);
			const auto item = peer->owner().message(fullId);
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
	auto alertPeer = (PeerData*)nullptr;
	for (auto i = _whenAlerts.begin(); i != _whenAlerts.end();) {
		while (!i->second.empty() && i->second.begin()->first <= ms) {
			const auto peer = i->first->peer;
			const auto &notifySettings = peer->owner().notifySettings();
			const auto peerUnknown = notifySettings.muteUnknown(peer);
			const auto peerAlert = !peerUnknown
				&& !notifySettings.isMuted(peer);
			const auto from = i->second.begin()->second;
			const auto fromUnknown = (!from
				|| notifySettings.muteUnknown(from));
			const auto fromAlert = !fromUnknown
				&& !notifySettings.isMuted(from);
			if (peerAlert || fromAlert) {
				alertPeer = peer;
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
	if (alertPeer) {
		if (settings.flashBounceNotify() && !_manager->skipFlashBounce()) {
			if (const auto window = Core::App().primaryWindow()) {
				if (const auto handle = window->widget()->windowHandle()) {
					handle->alert(kSystemAlertDuration);
					// (handle, SLOT(_q_clearAlert())); in the future.
				}
			}
		}
		if (settings.soundNotify() && !_manager->skipAudio()) {
			const auto track = lookupSound(
				&alertPeer->owner(),
				alertPeer->owner().notifySettings().sound(alertPeer).id);
			track->playOnce();
			Media::Player::mixer()->suppressAll(track->getLengthMs());
			Media::Player::mixer()->faderOnTimer();
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
		auto notify = std::optional<ItemNotification>();
		auto notifyHistory = (History*)nullptr;
		for (auto i = _waiters.begin(); i != _waiters.end();) {
			const auto history = i->first;
			auto current = history->currentNotification();
			if (current && current->item->id != i->second.key.messageId) {
				auto j = _whenMaps.find(history);
				if (j == _whenMaps.end()) {
					history->clearNotifications();
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
					history->skipNotification();
					current = history->currentNotification();
				} while (current);
			}
			if (!current) {
				_whenMaps.remove(history);
				i = _waiters.erase(i);
				continue;
			}
			auto when = i->second.when;
			if (!notify || next > when) {
				next = when;
				notify = current,
				notifyHistory = history;
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
		const auto messageNotification = (notify->type
			== ItemNotificationType::Message);
		const auto isForwarded = messageNotification
			&& notifyItem->Has<HistoryMessageForwarded>();
		const auto isAlbum = messageNotification
			&& notifyItem->groupId();

		// Forwarded and album notify grouping.
		auto groupedItem = (isForwarded || isAlbum)
			? notifyItem.get()
			: nullptr;
		auto forwardedCount = isForwarded ? 1 : 0;

		const auto history = notifyItem->history();
		const auto j = _whenMaps.find(history);
		if (j == _whenMaps.cend()) {
			history->clearNotifications();
		} else {
			while (true) {
				auto nextNotify = std::optional<ItemNotification>();
				history->skipNotification();
				if (!history->hasNotification()) {
					break;
				}

				j->second.remove({
					(groupedItem ? groupedItem : notifyItem.get())->id,
					notify->type,
				});
				do {
					const auto k = j->second.find(
						history->currentNotification());
					if (k != j->second.cend()) {
						nextNotify = history->currentNotification();
						_waiters.emplace(notifyHistory, Waiter{
							.key = k->first,
							.when = k->second
						});
						break;
					}
					history->skipNotification();
				} while (history->hasNotification());
				if (!nextNotify || !groupedItem) {
					break;
				}
				const auto nextMessageNotification
					= (nextNotify->type
						== ItemNotificationType::Message);
				const auto canNextBeGrouped = nextMessageNotification
					&& ((isForwarded && nextNotify->item->Has<HistoryMessageForwarded>())
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
				= (notify->type == ItemNotificationType::Reaction);
			const auto reaction = reactionNotification
				? notify->item->lookupUnreadReaction(notify->reactionSender)
				: QString();
			if (!reactionNotification || !reaction.isEmpty()) {
				_manager->showNotification({
					.item = notify->item,
					.forwardedCount = forwardedCount,
					.reactionFrom = notify->reactionSender,
					.reactionEmoji = reaction,
				});
			}
		}

		if (!history->hasNotification()) {
			_waiters.remove(history);
			_whenMaps.remove(history);
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
	const auto custom = notifySettings.lookupRingtone(id);
	if (custom && !custom->bytes().isEmpty()) {
		const auto j = _customSoundTracks.emplace(
			id,
			Media::Audio::Current().createTrack()
		).first;
		j->second->fillFromData(bytes::make_vector(custom->bytes()));
		return j->second.get();
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
		Core::App().settings().getSoundPath(qsl("msg_incoming")));
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
		ItemNotificationType type) const {
	const auto hideEverything = Core::App().passcodeLocked()
		|| forceHideDetails();
	const auto view = Core::App().settings().notifyView();

	auto result = DisplayOptions();
	result.hideNameAndPhoto = hideEverything
		|| (view > Core::Settings::NotifyView::ShowName);
	result.hideMessageText = hideEverything
		|| (view > Core::Settings::NotifyView::ShowPreview);
	result.hideMarkAsRead = result.hideMessageText
		|| (type != ItemNotificationType::Message)
		|| !item
		|| ((item->out() || item->history()->peer->isSelf())
			&& item->isFromScheduled());
	result.hideReplyButton = result.hideMarkAsRead
		|| !item->history()->peer->canWrite()
		|| item->history()->peer->isBroadcast()
		|| (item->history()->peer->slowmodeSecondsLeft() > 0);
	return result;
}

TextWithEntities Manager::ComposeReactionNotification(
		not_null<HistoryItem*> item,
		const QString &reaction,
		bool hideContent) {
	const auto simple = [&](const auto &phrase) {
		return TextWithEntities{ phrase(tr::now, lt_reaction, reaction) };
	};
	if (hideContent) {
		return simple(tr::lng_reaction_notext);
	}
	const auto media = item->media();
	const auto text = [&] {
		return tr::lng_reaction_text(
			tr::now,
			lt_reaction,
			Ui::Text::WithEntities(reaction),
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
			return {
				tr::lng_reaction_sticker(
					tr::now,
					lt_reaction,
					reaction,
					lt_emoji,
					sticker->alt)
			};
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
		return {
			tr::lng_reaction_contact(
				tr::now,
				lt_reaction,
				reaction,
				lt_name,
				name)
		};
	} else if (media->location()) {
		return simple(tr::lng_reaction_location);
		// lng_reaction_live_location not used right now :(
	} else if (const auto poll = media->poll()) {
		return {
			(poll->quiz() ? tr::lng_reaction_quiz : tr::lng_reaction_poll)(
				tr::now,
				lt_reaction,
				reaction,
				lt_title,
				poll->question)
		};
	} else if (media->game()) {
		return simple(tr::lng_reaction_game);
	} else if (media->invoice()) {
		return simple(tr::lng_reaction_invoice);
	}
	return text();
}

QString Manager::addTargetAccountName(
		const QString &title,
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
	return add
		? (title
			+ accountNameSeparator()
			+ (session->user()->username.isEmpty()
				? session->user()->name
				: session->user()->username))
		: title;
}

QString Manager::accountNameSeparator() {
	return QString::fromUtf8(" \xE2\x9E\x9C ");
}

void Manager::notificationActivated(
		NotificationId id,
		const TextWithTags &reply) {
	onBeforeNotificationActivated(id);
	if (const auto session = system()->findSession(id.full.sessionId)) {
		if (session->windows().empty()) {
			Core::App().domain().activate(&session->account());
		}
		if (!session->windows().empty()) {
			const auto window = session->windows().front();
			const auto history = session->data().history(id.full.peerId);
			if (!reply.text.isEmpty()) {
				const auto replyToId = (id.msgId > 0
					&& !history->peer->isUser())
					? id.msgId
					: 0;
				auto draft = std::make_unique<Data::Draft>(
					reply,
					replyToId,
					MessageCursor{
						int(reply.text.size()),
						int(reply.text.size()),
						QFIXED_MAX,
					},
					Data::PreviewState::Allowed);
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
	const auto openExactlyMessage = [&] {
		const auto peer = history->peer;
		if (peer->isBroadcast()) {
			return false;
		}
		const auto item = history->owner().message(history->peer, messageId);
		if (!item
			|| !item->isRegular()
			|| (!item->out() && (!item->mentionsMe() || peer->isUser()))) {
			return false;
		}
		return true;
	}();
	if (openExactlyMessage) {
		Ui::showPeerHistory(history, messageId);
	} else {
		Ui::showPeerHistory(history, ShowAtUnreadMsgId);
	}
	system()->clearFromHistory(history);
}

void Manager::notificationReplied(
		NotificationId id,
		const TextWithTags &reply) {
	if (!id.full.sessionId || !id.full.peerId) {
		return;
	}

	const auto session = system()->findSession(id.full.sessionId);
	if (!session) {
		return;
	}
	const auto history = session->data().history(id.full.peerId);

	auto message = Api::MessageToSend(Api::SendAction(history));
	message.textWithTags = reply;
	message.action.replyTo = (id.msgId > 0 && !history->peer->isUser())
		? id.msgId
		: 0;
	message.action.clearDraft = false;
	history->session().api().sendMessage(std::move(message));

	const auto item = history->owner().message(history->peer, id.msgId);
	if (item && item->isUnreadMention() && !item->isIncomingUnreadMedia()) {
		history->session().api().markContentsRead(item);
	}
}

void NativeManager::doShowNotification(NotificationFields &&fields) {
	const auto options = getNotificationOptions(
		fields.item,
		(fields.reactionFrom
			? ItemNotificationType::Reaction
			: ItemNotificationType::Message));
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
	const auto title = options.hideNameAndPhoto
		? AppName.utf16()
		: (scheduled && peer->isSelf())
		? tr::lng_notification_reminder(tr::now)
		: peer->name;
	const auto fullTitle = addTargetAccountName(title, &peer->session());
	const auto subtitle = reactionFrom
		? (reactionFrom != peer ? reactionFrom->name : QString())
		: options.hideNameAndPhoto
		? QString()
		: item->notificationHeader();
	const auto text = reactionFrom
		? TextWithPermanentSpoiler(ComposeReactionNotification(
			item,
			fields.reactionEmoji,
			options.hideMessageText))
		: options.hideMessageText
		? tr::lng_notification_preview(tr::now)
		: (fields.forwardedCount > 1)
		? tr::lng_forward_messages(tr::now, lt_count, fields.forwardedCount)
		: item->groupId()
		? tr::lng_in_dlg_album(tr::now)
		: TextWithPermanentSpoiler(item->notificationText());

	// #TODO optimize
	auto userpicView = item->history()->peer->createUserpicView();
	doShowNativeNotification(
		item->history()->peer,
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
