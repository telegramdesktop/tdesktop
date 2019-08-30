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
#include "history/history.h"
#include "history/history_item_components.h"
//#include "history/feed/history_feed_section.h" // #feed
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "base/unixtime.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "main/main_session.h"

namespace Window {
namespace Notifications {
namespace {

// not more than one sound in 500ms from one peer - grouping
constexpr auto kMinimalAlertDelay = crl::time(500);
constexpr auto kWaitingForAllGroupedDelay = crl::time(1000);

} // namespace

System::System(not_null<Main::Session*> session)
: _session(session)
, _waitTimer([=] { showNext(); })
, _waitForAllGroupedTimer([=] { showGrouped(); }) {
	createManager();

	subscribe(settingsChanged(), [=](ChangeType type) {
		if (type == ChangeType::DesktopEnabled) {
			App::wnd()->updateTrayMenu();
			clearAll();
		} else if (type == ChangeType::ViewParams) {
			updateAll();
		} else if (type == ChangeType::IncludeMuted
			|| type == ChangeType::CountMessages) {
			Notify::unreadCounterUpdated();
		}
	});
}

void System::createManager() {
	_manager = Platform::Notifications::Create(this);
	if (!_manager) {
		_manager = std::make_unique<Default::Manager>(this);
	}
}

System::SkipState System::skipNotification(
		not_null<HistoryItem*> item) const {
	const auto history = item->history();
	const auto notifyBy = item->specialNotificationPeer();
	if (App::quitting() || !history->currentNotification()) {
		return { SkipState::Skip };
	}
	const auto scheduled = item->out() && item->isFromScheduled();

	history->owner().requestNotifySettings(history->peer);
	if (notifyBy) {
		history->owner().requestNotifySettings(notifyBy);
	}

	if (history->owner().notifyMuteUnknown(history->peer)) {
		return { SkipState::Unknown, item->isSilent() };
	} else if (!history->owner().notifyIsMuted(history->peer)) {
		return { SkipState::DontSkip, item->isSilent() };
	} else if (!notifyBy) {
		return {
			scheduled ? SkipState::DontSkip : SkipState::Skip,
			item->isSilent() || scheduled
		};
	} else if (history->owner().notifyMuteUnknown(notifyBy)) {
		return { SkipState::Unknown, item->isSilent() };
	} else if (!history->owner().notifyIsMuted(notifyBy)) {
		return { SkipState::DontSkip, item->isSilent() };
	} else {
		return {
			scheduled ? SkipState::DontSkip : SkipState::Skip,
			item->isSilent() || scheduled
		};
	}
}

void System::schedule(not_null<HistoryItem*> item) {
	const auto history = item->history();
	const auto skip = skipNotification(item);
	if (skip.value == SkipState::Skip) {
		history->popNotification(item);
		return;
	}
	const auto notifyBy = item->specialNotificationPeer();
	const auto ready = (skip.value != SkipState::Unknown)
		&& item->notificationReady();

	auto delay = item->Has<HistoryMessageForwarded>() ? 500 : 100;
	const auto t = base::unixtime::now();
	const auto ms = crl::now();
	const bool isOnline = App::main()->lastWasOnline();
	const auto otherNotOld = ((cOtherOnline() * 1000LL) + Global::OnlineCloudTimeout() > t * 1000LL);
	const bool otherLaterThanMe = (cOtherOnline() * 1000LL + (ms - App::main()->lastSetOnline()) > t * 1000LL);
	if (!isOnline && otherNotOld && otherLaterThanMe) {
		delay = Global::NotifyCloudDelay();
	} else if (cOtherOnline() >= t) {
		delay = Global::NotifyDefaultDelay();
	}

	auto when = ms + delay;
	if (!skip.silent) {
		_whenAlerts[history].insert(when, notifyBy);
	}
	if (Global::DesktopNotify() && !Platform::Notifications::SkipToast()) {
		auto &whenMap = _whenMaps[history];
		if (whenMap.constFind(item->id) == whenMap.cend()) {
			whenMap.insert(item->id, when);
		}

		auto &addTo = ready ? _waiters : _settingWaiters;
		const auto it = addTo.constFind(history);
		if (it == addTo.cend() || it->when > when) {
			addTo.insert(history, Waiter(item->id, when, notifyBy));
		}
	}
	if (ready) {
		if (!_waitTimer.isActive() || _waitTimer.remainingTime() > delay) {
			_waitTimer.callOnce(delay);
		}
	}
}

void System::clearAll() {
	_manager->clearAll();

	for (auto i = _whenMaps.cbegin(), e = _whenMaps.cend(); i != e; ++i) {
		i.key()->clearNotifications();
	}
	_whenMaps.clear();
	_whenAlerts.clear();
	_waiters.clear();
	_settingWaiters.clear();
}

void System::clearFromHistory(not_null<History*> history) {
	_manager->clearFromHistory(history);

	history->clearNotifications();
	_whenMaps.remove(history);
	_whenAlerts.remove(history);
	_waiters.remove(history);
	_settingWaiters.remove(history);

	_waitTimer.cancel();
	showNext();
}

void System::clearIncomingFromHistory(not_null<History*> history) {
	_manager->clearFromHistory(history);
	history->clearIncomingNotifications();
	_whenAlerts.remove(history);
}

void System::clearFromItem(not_null<HistoryItem*> item) {
	_manager->clearFromItem(item);
}

void System::clearAllFast() {
	_manager->clearAllFast();

	_whenMaps.clear();
	_whenAlerts.clear();
	_waiters.clear();
	_settingWaiters.clear();
}

void System::checkDelayed() {
	for (auto i = _settingWaiters.begin(); i != _settingWaiters.end();) {
		const auto history = i.key();
		const auto peer = history->peer;
		auto loaded = false;
		auto muted = false;
		if (!peer->owner().notifyMuteUnknown(peer)) {
			if (!peer->owner().notifyIsMuted(peer)) {
				loaded = true;
			} else if (const auto from = i.value().notifyBy) {
				if (!peer->owner().notifyMuteUnknown(from)) {
					if (!peer->owner().notifyIsMuted(from)) {
						loaded = true;
					} else {
						loaded = muted = true;
					}
				}
			} else {
				loaded = muted = true;
			}
		}
		if (loaded) {
			const auto fullId = FullMsgId(
				history->channelId(),
				i.value().msg);
			if (const auto item = peer->owner().message(fullId)) {
				if (!item->notificationReady()) {
					loaded = false;
				}
			} else {
				muted = true;
			}
		}
		if (loaded) {
			if (!muted) {
				_waiters.insert(i.key(), i.value());
			}
			i = _settingWaiters.erase(i);
		} else {
			++i;
		}
	}
	_waitTimer.cancel();
	showNext();
}

void System::showGrouped() {
	if (const auto lastItem = session().data().message(_lastHistoryItemId)) {
		_waitForAllGroupedTimer.cancel();
		_manager->showNotification(lastItem, _lastForwardedCount);
		_lastForwardedCount = 0;
		_lastHistoryItemId = FullMsgId();
	}
}

void System::showNext() {
	if (App::quitting()) return;

	const auto isSameGroup = [=](HistoryItem *item) {
		if (!_lastHistoryItemId || !item) {
			return false;
		}
		const auto lastItem = item->history()->owner().message(
			_lastHistoryItemId);
		if (lastItem) {
			return (lastItem->groupId() == item->groupId() || lastItem->author() == item->author());
		}
		return false;
	};

	auto ms = crl::now(), nextAlert = crl::time(0);
	bool alert = false;
	int32 now = base::unixtime::now();
	for (auto i = _whenAlerts.begin(); i != _whenAlerts.end();) {
		while (!i.value().isEmpty() && i.value().begin().key() <= ms) {
			const auto peer = i.key()->peer;
			const auto peerUnknown = peer->owner().notifyMuteUnknown(peer);
			const auto peerAlert = !peerUnknown
				&& !peer->owner().notifyIsMuted(peer);
			const auto from = i.value().begin().value();
			const auto fromUnknown = (!from
				|| peer->owner().notifyMuteUnknown(from));
			const auto fromAlert = !fromUnknown
				&& !peer->owner().notifyIsMuted(from);
			if (peerAlert || fromAlert) {
				alert = true;
			}
			while (!i.value().isEmpty()
				&& i.value().begin().key() <= ms + kMinimalAlertDelay) {
				i.value().erase(i.value().begin());
			}
		}
		if (i.value().isEmpty()) {
			i = _whenAlerts.erase(i);
		} else {
			if (!nextAlert || nextAlert > i.value().begin().key()) {
				nextAlert = i.value().begin().key();
			}
			++i;
		}
	}
	if (alert) {
		Platform::Notifications::FlashBounce();
		if (Global::SoundNotify() && !Platform::Notifications::SkipAudio()) {
			ensureSoundCreated();
			_soundTrack->playOnce();
			emit Media::Player::mixer()->suppressAll(_soundTrack->getLengthMs());
			emit Media::Player::mixer()->faderOnTimer();
		}
	}

	if (_waiters.isEmpty() || !Global::DesktopNotify() || Platform::Notifications::SkipToast()) {
		if (nextAlert) {
			_waitTimer.callOnce(nextAlert - ms);
		}
		return;
	}

	while (true) {
		auto next = 0LL;
		HistoryItem *notifyItem = nullptr;
		History *notifyHistory = nullptr;
		for (auto i = _waiters.begin(); i != _waiters.end();) {
			History *history = i.key();
			if (history->currentNotification() && history->currentNotification()->id != i.value().msg) {
				auto j = _whenMaps.find(history);
				if (j == _whenMaps.end()) {
					history->clearNotifications();
					i = _waiters.erase(i);
					continue;
				}
				do {
					auto k = j.value().constFind(history->currentNotification()->id);
					if (k != j.value().cend()) {
						i.value().msg = k.key();
						i.value().when = k.value();
						break;
					}
					history->skipNotification();
				} while (history->currentNotification());
			}
			if (!history->currentNotification()) {
				_whenMaps.remove(history);
				i = _waiters.erase(i);
				continue;
			}
			auto when = i.value().when;
			if (!notifyItem || next > when) {
				next = when;
				notifyItem = history->currentNotification();
				notifyHistory = history;
			}
			++i;
		}
		if (notifyItem) {
			if (next > ms) {
				if (nextAlert && nextAlert < next) {
					next = nextAlert;
					nextAlert = 0;
				}
				_waitTimer.callOnce(next - ms);
				break;
			} else {
				const auto isForwarded = notifyItem->Has<HistoryMessageForwarded>();
				const auto isAlbum = notifyItem->groupId();

				auto groupedItem = (isForwarded || isAlbum) ? notifyItem : nullptr; // forwarded and album notify grouping
				auto forwardedCount = isForwarded ? 1 : 0;

				const auto history = notifyItem->history();
				const auto j = _whenMaps.find(history);
				if (j == _whenMaps.cend()) {
					history->clearNotifications();
				} else {
					auto nextNotify = (HistoryItem*)nullptr;
					do {
						history->skipNotification();
						if (!history->hasNotification()) {
							break;
						}

						j.value().remove((groupedItem ? groupedItem : notifyItem)->id);
						do {
							const auto k = j.value().constFind(history->currentNotification()->id);
							if (k != j.value().cend()) {
								nextNotify = history->currentNotification();
								_waiters.insert(notifyHistory, Waiter(k.key(), k.value(), 0));
								break;
							}
							history->skipNotification();
						} while (history->hasNotification());
						if (nextNotify) {
							if (groupedItem) {
								const auto canNextBeGrouped = (isForwarded && nextNotify->Has<HistoryMessageForwarded>())
									|| (isAlbum && nextNotify->groupId());
								const auto nextItem = canNextBeGrouped ? nextNotify : nullptr;
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
							}
							nextNotify = nullptr;
						}
					} while (nextNotify);
				}

				if (!_lastHistoryItemId && groupedItem) {
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
					_lastHistoryItemId = groupedItem->fullId();
					_waitForAllGroupedTimer.callOnce(kWaitingForAllGroupedDelay);
				} else {
					// If the current notification is not grouped
					// then there is no reason to wait for the timer
					// to show the previous notification.
					showGrouped();
					_manager->showNotification(notifyItem, forwardedCount);
				}

				if (!history->hasNotification()) {
					_waiters.remove(history);
					_whenMaps.remove(history);
					continue;
				}
			}
		} else {
			break;
		}
	}
	if (nextAlert) {
		_waitTimer.callOnce(nextAlert - ms);
	}
}

void System::ensureSoundCreated() {
	if (_soundTrack) {
		return;
	}

	_soundTrack = Media::Audio::Current().createTrack();
	_soundTrack->fillFromFile(
		session().settings().getSoundPath(qsl("msg_incoming")));
}

void System::updateAll() {
	_manager->updateAll();
}

Manager::DisplayOptions Manager::getNotificationOptions(HistoryItem *item) {
	const auto hideEverything = Core::App().locked()
		|| Global::ScreenIsLocked();

	DisplayOptions result;
	result.hideNameAndPhoto = hideEverything
		|| (Global::NotifyView() > dbinvShowName);
	result.hideMessageText = hideEverything
		|| (Global::NotifyView() > dbinvShowPreview);
	result.hideReplyButton = result.hideMessageText
		|| !item
		|| ((item->out() || item->history()->peer->isSelf())
			&& item->isFromScheduled())
		|| !item->history()->peer->canWrite()
		|| (item->history()->peer->slowmodeSecondsLeft() > 0);
	return result;
}

void Manager::notificationActivated(PeerId peerId, MsgId msgId) {
	onBeforeNotificationActivated(peerId, msgId);
	if (auto window = App::wnd()) {
		auto history = system()->session().data().history(peerId);
		window->showFromTray();
		window->reActivateWindow();
		if (Core::App().locked()) {
			window->setInnerFocus();
			system()->clearAll();
		} else {
			openNotificationMessage(history, msgId);
		}
	}
	onAfterNotificationActivated(peerId, msgId);
}

void Manager::openNotificationMessage(
		not_null<History*> history,
		MsgId messageId) {
	const auto openExactlyMessage = [&] {
		if (history->peer->isUser()
			|| history->peer->isChannel()
			|| !IsServerMsgId(messageId)) {
			return false;
		}
		const auto item = history->owner().message(history->channelId(), messageId);
		if (!item || !item->mentionsMe()) {
			return false;
		}
		return true;
	}();
	//const auto messageFeed = [&] { // #feed
	//	if (const auto channel = history->peer->asChannel()) {
	//		return channel->feed();
	//	}
	//	return (Data::Feed*)nullptr;
	//}();
	if (openExactlyMessage) {
		Ui::showPeerHistory(history, messageId);
	//} else if (messageFeed) { // #feed
	//	App::wnd()->sessionController()->showSection(
	//		HistoryFeed::Memento(messageFeed));
	} else {
		Ui::showPeerHistory(history, ShowAtUnreadMsgId);
	}
	system()->clearFromHistory(history);
}

void Manager::notificationReplied(
		PeerId peerId,
		MsgId msgId,
		const TextWithTags &reply) {
	if (!peerId) return;

	const auto history = system()->session().data().history(peerId);

	auto message = Api::MessageToSend(history);
	message.textWithTags = reply;
	message.action.replyTo = (msgId > 0 && !history->peer->isUser()) ? msgId : 0;
	message.action.clearDraft = false;
	history->session().api().sendMessage(std::move(message));

	const auto item = history->owner().message(history->channelId(), msgId);
	if (item && item->isUnreadMention() && !item->isUnreadMedia()) {
		history->session().api().markMediaRead(item);
	}
}

void NativeManager::doShowNotification(
		not_null<HistoryItem*> item,
		int forwardedCount) {
	const auto options = getNotificationOptions(item);

	const auto peer = item->history()->peer;
	const auto scheduled = !options.hideNameAndPhoto
		&& (item->out() || peer->isSelf())
		&& item->isFromScheduled();
	const auto title = options.hideNameAndPhoto
		? qsl("Telegram Desktop")
		: (scheduled && peer->isSelf())
		? tr::lng_notification_reminder(tr::now)
		: App::peerName(peer);
	const auto subtitle = options.hideNameAndPhoto
		? QString()
		: item->notificationHeader();
	const auto text = options.hideMessageText
		? tr::lng_notification_preview(tr::now)
		: (forwardedCount < 2
			? (item->groupId()
				? tr::lng_in_dlg_album(tr::now)
				: item->notificationText())
			: tr::lng_forward_messages(tr::now, lt_count, forwardedCount));

	doShowNativeNotification(
		item->history()->peer,
		item->id,
		scheduled ? WrapFromScheduled(title) : title,
		subtitle,
		text,
		options.hideNameAndPhoto,
		options.hideReplyButton);
}

System::~System() = default;

QString WrapFromScheduled(const QString &text) {
	return QString::fromUtf8("\xF0\x9F\x93\x85 ") + text;
}

} // namespace Notifications
} // namespace Window
