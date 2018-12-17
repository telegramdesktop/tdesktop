/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_manager.h"

#include "platform/platform_notifications_manager.h"
#include "window/notifications_manager_default.h"
#include "media/media_audio_track.h"
#include "media/media_audio.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/feed/history_feed_section.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "messenger.h"
#include "apiwrap.h"
#include "auth_session.h"

namespace Window {
namespace Notifications {
namespace {

// not more than one sound in 500ms from one peer - grouping
constexpr auto kMinimalAlertDelay = TimeMs(500);

} // namespace

System::System(AuthSession *session) : _authSession(session) {
	createManager();

	_waitTimer.setTimeoutHandler([this] {
		showNext();
	});

	subscribe(settingsChanged(), [this](ChangeType type) {
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

void System::schedule(History *history, HistoryItem *item) {
	if (App::quitting()
		|| !history->currentNotification()
		|| !AuthSession::Exists()) return;

	const auto notifyBy = (!history->peer->isUser() && item->mentionsMe())
		? item->from().get()
		: nullptr;

	if (item->isSilent()) {
		history->popNotification(item);
		return;
	}

	Auth().data().requestNotifySettings(history->peer);
	if (notifyBy) {
		Auth().data().requestNotifySettings(notifyBy);
	}
	auto haveSetting = !Auth().data().notifyMuteUnknown(history->peer);
	if (haveSetting && Auth().data().notifyIsMuted(history->peer)) {
		if (notifyBy) {
			haveSetting = !Auth().data().notifyMuteUnknown(notifyBy);
			if (haveSetting) {
				if (Auth().data().notifyIsMuted(notifyBy)) {
					history->popNotification(item);
					return;
				}
			}
		} else {
			history->popNotification(item);
			return;
		}
	}
	if (!item->notificationReady()) {
		haveSetting = false;
	}

	auto delay = item->Has<HistoryMessageForwarded>() ? 500 : 100;
	auto t = unixtime();
	auto ms = getms(true);
	bool isOnline = App::main()->lastWasOnline(), otherNotOld = ((cOtherOnline() * 1000LL) + Global::OnlineCloudTimeout() > t * 1000LL);
	bool otherLaterThanMe = (cOtherOnline() * 1000LL + (ms - App::main()->lastSetOnline()) > t * 1000LL);
	if (!isOnline && otherNotOld && otherLaterThanMe) {
		delay = Global::NotifyCloudDelay();
	} else if (cOtherOnline() >= t) {
		delay = Global::NotifyDefaultDelay();
	}

	auto when = ms + delay;
	_whenAlerts[history].insert(when, notifyBy);
	if (Global::DesktopNotify() && !Platform::Notifications::SkipToast()) {
		auto &whenMap = _whenMaps[history];
		if (whenMap.constFind(item->id) == whenMap.cend()) {
			whenMap.insert(item->id, when);
		}

		auto &addTo = haveSetting ? _waiters : _settingWaiters;
		auto it = addTo.constFind(history);
		if (it == addTo.cend() || it->when > when) {
			addTo.insert(history, Waiter(item->id, when, notifyBy));
		}
	}
	if (haveSetting) {
		if (!_waitTimer.isActive() || _waitTimer.remainingTime() > delay) {
			_waitTimer.start(delay);
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

void System::clearFromHistory(History *history) {
	_manager->clearFromHistory(history);

	history->clearNotifications();
	_whenMaps.remove(history);
	_whenAlerts.remove(history);
	_waiters.remove(history);
	_settingWaiters.remove(history);

	_waitTimer.stop();
	showNext();
}

void System::clearFromItem(HistoryItem *item) {
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
		if (!Auth().data().notifyMuteUnknown(peer)) {
			if (!Auth().data().notifyIsMuted(peer)) {
				loaded = true;
			} else if (const auto from = i.value().notifyBy) {
				if (!Auth().data().notifyMuteUnknown(from)) {
					if (!Auth().data().notifyIsMuted(from)) {
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
			if (const auto item = App::histItemById(fullId)) {
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
	_waitTimer.stop();
	showNext();
}

void System::showNext() {
	if (App::quitting()) return;

	auto ms = getms(true), nextAlert = 0LL;
	bool alert = false;
	int32 now = unixtime();
	for (auto i = _whenAlerts.begin(); i != _whenAlerts.end();) {
		while (!i.value().isEmpty() && i.value().begin().key() <= ms) {
			const auto peer = i.key()->peer;
			const auto peerUnknown = Auth().data().notifyMuteUnknown(peer);
			const auto peerAlert = !peerUnknown
				&& !Auth().data().notifyIsMuted(peer);
			const auto from = i.value().begin().value();
			const auto fromUnknown = (!from
				|| Auth().data().notifyMuteUnknown(from));
			const auto fromAlert = !fromUnknown
				&& !Auth().data().notifyIsMuted(from);
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
			_waitTimer.start(nextAlert - ms);
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
				_waitTimer.start(next - ms);
				break;
			} else {
				auto forwardedItem = notifyItem->Has<HistoryMessageForwarded>() ? notifyItem : nullptr; // forwarded notify grouping
				auto forwardedCount = 1;

				auto ms = getms(true);
				auto history = notifyItem->history();
				auto j = _whenMaps.find(history);
				if (j == _whenMaps.cend()) {
					history->clearNotifications();
				} else {
					auto nextNotify = (HistoryItem*)nullptr;
					do {
						history->skipNotification();
						if (!history->hasNotification()) {
							break;
						}

						j.value().remove((forwardedItem ? forwardedItem : notifyItem)->id);
						do {
							auto k = j.value().constFind(history->currentNotification()->id);
							if (k != j.value().cend()) {
								nextNotify = history->currentNotification();
								_waiters.insert(notifyHistory, Waiter(k.key(), k.value(), 0));
								break;
							}
							history->skipNotification();
						} while (history->hasNotification());
						if (nextNotify) {
							if (forwardedItem) {
								auto nextForwarded = nextNotify->Has<HistoryMessageForwarded>() ? nextNotify : nullptr;
								if (nextForwarded
									&& forwardedItem->author() == nextForwarded->author()
									&& qAbs(int64(nextForwarded->date()) - int64(forwardedItem->date())) < 2) {
									forwardedItem = nextForwarded;
									++forwardedCount;
								} else {
									nextNotify = nullptr;
								}
							} else {
								nextNotify = nullptr;
							}
						}
					} while (nextNotify);
				}

				_manager->showNotification(notifyItem, forwardedCount);

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
		_waitTimer.start(nextAlert - ms);
	}
}

void System::ensureSoundCreated() {
	if (_soundTrack) {
		return;
	}

	_soundTrack = Media::Audio::Current().createTrack();
	_soundTrack->fillFromFile(
		Auth().settings().getSoundPath(qsl("msg_incoming")));
}

void System::updateAll() {
	_manager->updateAll();
}

Manager::DisplayOptions Manager::getNotificationOptions(HistoryItem *item) {
	const auto hideEverything = Messenger::Instance().locked()
		|| Global::ScreenIsLocked();

	DisplayOptions result;
	result.hideNameAndPhoto = hideEverything || (Global::NotifyView() > dbinvShowName);
	result.hideMessageText = hideEverything || (Global::NotifyView() > dbinvShowPreview);
	result.hideReplyButton = result.hideMessageText || !item || !item->history()->peer->canWrite();
	return result;
}

void Manager::notificationActivated(PeerId peerId, MsgId msgId) {
	onBeforeNotificationActivated(peerId, msgId);
	if (auto window = App::wnd()) {
		auto history = App::history(peerId);
		window->showFromTray();
		window->reActivateWindow();
		if (Messenger::Instance().locked()) {
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
		const auto item = App::histItemById(history->channelId(), messageId);
		if (!item || !item->mentionsMe()) {
			return false;
		}
		return true;
	}();
	const auto messageFeed = [&] {
		if (const auto channel = history->peer->asChannel()) {
			return channel->feed();
		}
		return (Data::Feed*)nullptr;
	}();
	if (openExactlyMessage) {
		Ui::showPeerHistory(history, messageId);
	} else if (messageFeed) {
		App::wnd()->controller()->showSection(
			HistoryFeed::Memento(messageFeed));
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

	const auto history = App::history(peerId);

	auto message = ApiWrap::MessageToSend(history);
	message.textWithTags = reply;
	message.replyTo = (msgId > 0 && !history->peer->isUser()) ? msgId : 0;
	message.clearDraft = false;
	Auth().api().sendMessage(std::move(message));
}

void NativeManager::doShowNotification(HistoryItem *item, int forwardedCount) {
	const auto options = getNotificationOptions(item);

	const auto title = options.hideNameAndPhoto ? qsl("Telegram Desktop") : item->history()->peer->name;
	const auto subtitle = options.hideNameAndPhoto ? QString() : item->notificationHeader();
	const auto text = options.hideMessageText
		? lang(lng_notification_preview)
		: (forwardedCount < 2
			? item->notificationText()
			: lng_forward_messages(lt_count, forwardedCount));

	doShowNativeNotification(
		item->history()->peer,
		item->id,
		title,
		subtitle,
		text,
		options.hideNameAndPhoto,
		options.hideReplyButton);
}

System::~System() = default;

} // namespace Notifications
} // namespace Window
