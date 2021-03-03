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
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_user.h"
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
#include "facades.h"
#include "app.h"

#include <QtGui/QWindow>

namespace Window {
namespace Notifications {
namespace {

// not more than one sound in 500ms from one peer - grouping
constexpr auto kMinimalAlertDelay = crl::time(500);
constexpr auto kWaitingForAllGroupedDelay = crl::time(1000);

#ifdef Q_OS_MAC
constexpr auto kSystemAlertDuration = crl::time(1000);
#else // !Q_OS_MAC
constexpr auto kSystemAlertDuration = crl::time(0);
#endif // Q_OS_MAC

} // namespace

System::System()
: _waitTimer([=] { showNext(); })
, _waitForAllGroupedTimer([=] { showGrouped(); }) {
	subscribe(settingsChanged(), [=](ChangeType type) {
		if (type == ChangeType::DesktopEnabled) {
			clearAll();
		} else if (type == ChangeType::ViewParams) {
			updateAll();
		} else if (type == ChangeType::IncludeMuted
			|| type == ChangeType::CountMessages) {
			Core::App().domain().notifyUnreadBadgeChanged();
		}
	});
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

System::SkipState System::skipNotification(
		not_null<HistoryItem*> item) const {
	const auto history = item->history();
	const auto notifyBy = item->specialNotificationPeer();
	if (App::quitting() || !history->currentNotification()) {
		return { SkipState::Skip };
	} else if (!Core::App().settings().notifyFromAll()
		&& &history->session().account() != &Core::App().domain().active()) {
		return { SkipState::Skip };
	}
	const auto scheduled = item->out() && item->isFromScheduled();

	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (forwarded->imported) {
			return { SkipState::Skip };
		}
	}

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

	auto when = ms + delay;
	if (!skip.silent) {
		_whenAlerts[history].emplace(when, notifyBy);
	}
	if (Core::App().settings().desktopNotify()
		&& !Platform::Notifications::SkipToast()) {
		auto &whenMap = _whenMaps[history];
		if (whenMap.find(item->id) == whenMap.end()) {
			whenMap.emplace(item->id, when);
		}

		auto &addTo = ready ? _waiters : _settingWaiters;
		const auto it = addTo.find(history);
		if (it == addTo.end() || it->second.when > when) {
			addTo.emplace(history, Waiter{
				.msg = item->id,
				.when = when,
				.notifyBy = notifyBy
			});
		}
	}
	if (ready) {
		if (!_waitTimer.isActive() || _waitTimer.remainingTime() > delay) {
			_waitTimer.callOnce(delay);
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
		const auto history = i->first;
		const auto peer = history->peer;
		auto loaded = false;
		auto muted = false;
		if (!peer->owner().notifyMuteUnknown(peer)) {
			if (!peer->owner().notifyIsMuted(peer)) {
				loaded = true;
			} else if (const auto from = i->second.notifyBy) {
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
				i->second.msg);
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
				_waiters.emplace(i->first, i->second);
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
	Expects(_manager != nullptr);

	if (const auto session = findSession(_lastHistorySessionId)) {
		if (const auto lastItem = session->data().message(_lastHistoryItemId)) {
			_waitForAllGroupedTimer.cancel();
			_manager->showNotification(lastItem, _lastForwardedCount);
			_lastForwardedCount = 0;
			_lastHistoryItemId = FullMsgId();
			_lastHistorySessionId = 0;
		}
	}
}

void System::showNext() {
	Expects(_manager != nullptr);

	if (App::quitting()) {
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
	bool alert = false;
	int32 now = base::unixtime::now();
	for (auto i = _whenAlerts.begin(); i != _whenAlerts.end();) {
		while (!i->second.empty() && i->second.begin()->first <= ms) {
			const auto peer = i->first->peer;
			const auto peerUnknown = peer->owner().notifyMuteUnknown(peer);
			const auto peerAlert = !peerUnknown
				&& !peer->owner().notifyIsMuted(peer);
			const auto from = i->second.begin()->second;
			const auto fromUnknown = (!from
				|| peer->owner().notifyMuteUnknown(from));
			const auto fromAlert = !fromUnknown
				&& !peer->owner().notifyIsMuted(from);
			if (peerAlert || fromAlert) {
				alert = true;
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
	if (alert) {
		if (settings.flashBounceNotify() && !Platform::Notifications::SkipFlashBounce()) {
			if (const auto window = Core::App().activeWindow()) {
				if (const auto handle = window->widget()->windowHandle()) {
					handle->alert(kSystemAlertDuration);
					// (handle, SLOT(_q_clearAlert())); in the future.
				}
			}
		}
		if (settings.soundNotify() && !Platform::Notifications::SkipAudio()) {
			ensureSoundCreated();
			_soundTrack->playOnce();
			Media::Player::mixer()->suppressAll(_soundTrack->getLengthMs());
			Media::Player::mixer()->faderOnTimer();
		}
	}

	if (_waiters.empty() || !settings.desktopNotify() || Platform::Notifications::SkipToast()) {
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
			const auto history = i->first;
			if (history->currentNotification() && history->currentNotification()->id != i->second.msg) {
				auto j = _whenMaps.find(history);
				if (j == _whenMaps.end()) {
					history->clearNotifications();
					i = _waiters.erase(i);
					continue;
				}
				do {
					auto k = j->second.find(history->currentNotification()->id);
					if (k != j->second.cend()) {
						i->second.msg = k->first;
						i->second.when = k->second;
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
			auto when = i->second.when;
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

						j->second.remove((groupedItem ? groupedItem : notifyItem)->id);
						do {
							const auto k = j->second.find(history->currentNotification()->id);
							if (k != j->second.cend()) {
								nextNotify = history->currentNotification();
								_waiters.emplace(notifyHistory, Waiter{
									.msg = k->first,
									.when = k->second
								});
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
		Core::App().settings().getSoundPath(qsl("msg_incoming")));
}

void System::updateAll() {
	if (_manager) {
		_manager->updateAll();
	}
}

Manager::DisplayOptions Manager::GetNotificationOptions(HistoryItem *item) {
	const auto hideEverything = Core::App().passcodeLocked()
		|| Global::ScreenIsLocked();

	const auto view = Core::App().settings().notifyView();
	DisplayOptions result;
	result.hideNameAndPhoto = hideEverything || (view > dbinvShowName);
	result.hideMessageText = hideEverything || (view > dbinvShowPreview);
	result.hideReplyButton = result.hideMessageText
		|| !item
		|| ((item->out() || item->history()->peer->isSelf())
			&& item->isFromScheduled())
		|| !item->history()->peer->canWrite()
		|| (item->history()->peer->slowmodeSecondsLeft() > 0);
	return result;
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

void Manager::notificationActivated(NotificationId id) {
	onBeforeNotificationActivated(id);
	if (const auto session = system()->findSession(id.full.sessionId)) {
		if (session->windows().empty()) {
			Core::App().domain().activate(&session->account());
		}
		if (!session->windows().empty()) {
			const auto window = session->windows().front();
			const auto history = session->data().history(id.full.peerId);
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

	auto message = Api::MessageToSend(history);
	message.textWithTags = reply;
	message.action.replyTo = (id.msgId > 0 && !history->peer->isUser())
		? id.msgId
		: 0;
	message.action.clearDraft = false;
	history->session().api().sendMessage(std::move(message));

	const auto item = history->owner().message(
		history->channelId(),
		id.msgId);
	if (item && item->isUnreadMention() && !item->isUnreadMedia()) {
		history->session().api().markMediaRead(item);
	}
}

void NativeManager::doShowNotification(
		not_null<HistoryItem*> item,
		int forwardedCount) {
	const auto options = GetNotificationOptions(item);

	const auto peer = item->history()->peer;
	const auto scheduled = !options.hideNameAndPhoto
		&& (item->out() || peer->isSelf())
		&& item->isFromScheduled();
	const auto title = options.hideNameAndPhoto
		? qsl("Telegram Desktop")
		: (scheduled && peer->isSelf())
		? tr::lng_notification_reminder(tr::now)
		: peer->name;
	const auto fullTitle = addTargetAccountName(title, &peer->session());
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

	// #TODO optimize
	auto userpicView = item->history()->peer->createUserpicView();
	doShowNativeNotification(
		item->history()->peer,
		userpicView,
		item->id,
		scheduled ? WrapFromScheduled(fullTitle) : fullTitle,
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
