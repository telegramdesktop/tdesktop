/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "window/notifications_manager.h"

#include "platform/platform_notifications_manager.h"
#include "window/notifications_manager_default.h"
#include "media/media_audio_track.h"
#include "media/media_audio.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
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
		} else if (type == ChangeType::IncludeMuted) {
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
	if (App::quitting() || !history->currentNotification() || !AuthSession::Exists()) return;

	auto notifyByFrom = (!history->peer->isUser() && item->mentionsMe()) ? item->from() : nullptr;

	if (item->isSilent()) {
		history->popNotification(item);
		return;
	}

	auto haveSetting = !history->peer->notifySettingsUnknown();
	if (haveSetting) {
		if (history->peer->isMuted()) {
			if (notifyByFrom) {
				haveSetting = !item->from()->notifySettingsUnknown();
				if (haveSetting) {
					if (notifyByFrom->isMuted()) {
						history->popNotification(item);
						return;
					}
				} else {
					Auth().api().requestNotifySetting(notifyByFrom);
				}
			} else {
				history->popNotification(item);
				return;
			}
		}
	} else {
		if (notifyByFrom && notifyByFrom->notifySettingsUnknown()) {
			Auth().api().requestNotifySetting(notifyByFrom);
		}
		Auth().api().requestNotifySetting(history->peer);
	}
	if (!item->notificationReady()) {
		haveSetting = false;
	}

	int delay = item->Has<HistoryMessageForwarded>() ? 500 : 100, t = unixtime();
	auto ms = getms(true);
	bool isOnline = App::main()->lastWasOnline(), otherNotOld = ((cOtherOnline() * 1000LL) + Global::OnlineCloudTimeout() > t * 1000LL);
	bool otherLaterThanMe = (cOtherOnline() * 1000LL + (ms - App::main()->lastSetOnline()) > t * 1000LL);
	if (!isOnline && otherNotOld && otherLaterThanMe) {
		delay = Global::NotifyCloudDelay();
	} else if (cOtherOnline() >= t) {
		delay = Global::NotifyDefaultDelay();
	}

	auto when = ms + delay;
	_whenAlerts[history].insert(when, notifyByFrom);
	if (Global::DesktopNotify() && !Platform::Notifications::SkipToast()) {
		auto &whenMap = _whenMaps[history];
		if (whenMap.constFind(item->id) == whenMap.cend()) {
			whenMap.insert(item->id, when);
		}

		auto &addTo = haveSetting ? _waiters : _settingWaiters;
		auto it = addTo.constFind(history);
		if (it == addTo.cend() || it->when > when) {
			addTo.insert(history, Waiter(item->id, when, notifyByFrom));
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
		if (!peer->notifySettingsUnknown()) {
			if (!peer->isMuted()) {
				loaded = true;
			} else if (const auto from = i.value().notifyByFrom) {
				if (!from->notifySettingsUnknown()) {
					if (!from->isMuted()) {
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
			const auto peerUnknown = peer->notifySettingsUnknown();
			const auto peerAlert = peerUnknown ? false : !peer->isMuted();
			const auto from = i.value().begin().value();
			const auto fromUnknown = (!from || from->notifySettingsUnknown());
			const auto fromAlert = fromUnknown ? false : !from->isMuted();
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
								if (nextForwarded && forwardedItem->author() == nextForwarded->author() && qAbs(int64(nextForwarded->date.toTime_t()) - int64(forwardedItem->date.toTime_t())) < 2) {
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
	_soundTrack->fillFromFile(Auth().data().getSoundPath(qsl("msg_incoming")));
}

void System::updateAll() {
	_manager->updateAll();
}

Manager::DisplayOptions Manager::getNotificationOptions(HistoryItem *item) {
	auto hideEverything = (App::passcoded() || Global::ScreenIsLocked());

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
		if (App::passcoded()) {
			window->setInnerFocus();
			system()->clearAll();
		} else {
			auto tomsg = !history->peer->isUser() && (msgId > 0);
			if (tomsg) {
				auto item = App::histItemById(peerToChannel(peerId), msgId);
				if (!item || !item->mentionsMe()) {
					tomsg = false;
				}
			}
			Ui::showPeerHistory(history, tomsg ? msgId : ShowAtUnreadMsgId);
			system()->clearFromHistory(history);
		}
	}
	onAfterNotificationActivated(peerId, msgId);
}

void Manager::notificationReplied(
		PeerId peerId,
		MsgId msgId,
		const QString &reply) {
	if (!peerId) return;

	auto history = App::history(peerId);

	auto message = MainWidget::MessageToSend(history);
	message.textWithTags = { reply, TextWithTags::Tags() };
	message.replyTo = (msgId > 0 && !history->peer->isUser()) ? msgId : 0;
	message.clearDraft = false;
	if (auto main = App::main()) {
		main->sendMessage(message);
	}
}

void NativeManager::doShowNotification(HistoryItem *item, int forwardedCount) {
	auto options = getNotificationOptions(item);

	QString title = options.hideNameAndPhoto ? qsl("Telegram Desktop") : item->history()->peer->name;
	QString subtitle = options.hideNameAndPhoto ? QString() : item->notificationHeader();
	QString text = options.hideMessageText ? lang(lng_notification_preview) : (forwardedCount < 2 ? item->notificationText() : lng_forward_messages(lt_count, forwardedCount));

	doShowNativeNotification(item->history()->peer, item->id, title, subtitle, text, options.hideNameAndPhoto, options.hideReplyButton);
}

System::~System() = default;

} // namespace Notifications
} // namespace Window
