/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/notify/data_notify_settings.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"

namespace Data {

namespace {

constexpr auto kMaxNotifyCheckDelay = 24 * 3600 * crl::time(1000);

} // namespace

NotifySettings::NotifySettings(not_null<Session*> owner)
: _owner(owner)
, _unmuteByFinishedTimer([=] { unmuteByFinished(); }) {
}

void NotifySettings::requestNotifySettings(not_null<PeerData*> peer) {
	if (peer->notifySettingsUnknown()) {
		peer->session().api().requestNotifySettings(
			MTP_inputNotifyPeer(peer->input));
	}
	if (defaultNotifySettings(peer).settingsUnknown()) {
		peer->session().api().requestNotifySettings(peer->isUser()
			? MTP_inputNotifyUsers()
			: (peer->isChat() || peer->isMegagroup())
			? MTP_inputNotifyChats()
			: MTP_inputNotifyBroadcasts());
	}
}

void NotifySettings::applyNotifySetting(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings) {
	const auto goodForUpdate = [&](
			not_null<const PeerData*> peer,
			const PeerNotifySettings &settings) {
		return !peer->notifySettingsUnknown()
			&& ((!peer->notifyMuteUntil() && settings.muteUntil())
				|| (!peer->notifySilentPosts() && settings.silentPosts())
				|| (!peer->notifySound() && settings.sound()));
	};

	switch (notifyPeer.type()) {
	case mtpc_notifyUsers: {
		if (_defaultUserNotifySettings.change(settings)) {
			_defaultUserNotifyUpdates.fire({});

			_owner->enumerateUsers([&](not_null<UserData*> user) {
				if (goodForUpdate(user, _defaultUserNotifySettings)) {
					updateNotifySettingsLocal(user);
				}
			});
		}
	} break;
	case mtpc_notifyChats: {
		if (_defaultChatNotifySettings.change(settings)) {
			_defaultChatNotifyUpdates.fire({});

			_owner->enumerateGroups([&](not_null<PeerData*> peer) {
				if (goodForUpdate(peer, _defaultChatNotifySettings)) {
					updateNotifySettingsLocal(peer);
				}
			});
		}
	} break;
	case mtpc_notifyBroadcasts: {
		if (_defaultBroadcastNotifySettings.change(settings)) {
			_defaultBroadcastNotifyUpdates.fire({});

			_owner->enumerateChannels([&](not_null<ChannelData*> channel) {
				if (goodForUpdate(channel, _defaultBroadcastNotifySettings)) {
					updateNotifySettingsLocal(channel);
				}
			});
		}
	} break;
	case mtpc_notifyPeer: {
		const auto &data = notifyPeer.c_notifyPeer();
		if (const auto peer = _owner->peerLoaded(peerFromMTP(data.vpeer()))) {
			if (peer->notifyChange(settings)) {
				updateNotifySettingsLocal(peer);
			}
		}
	} break;
	}
}

void NotifySettings::updateNotifySettings(
		not_null<PeerData*> peer,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound) {
	if (peer->notifyChange(muteForSeconds, silentPosts, sound)) {
		updateNotifySettingsLocal(peer);
		peer->session().api().updateNotifySettingsDelayed(peer);
	}
}

void NotifySettings::resetNotifySettingsToDefault(not_null<PeerData*> peer) {
	const auto empty = MTP_peerNotifySettings(
		MTP_flags(0),
		MTPBool(),
		MTPBool(),
		MTPint(),
		MTPNotificationSound(),
		MTPNotificationSound(),
		MTPNotificationSound());
	if (peer->notifyChange(empty)) {
		updateNotifySettingsLocal(peer);
		peer->session().api().updateNotifySettingsDelayed(peer);
	}
}

const PeerNotifySettings &NotifySettings::defaultNotifySettings(
		not_null<const PeerData*> peer) const {
	return peer->isUser()
		? _defaultUserNotifySettings
		: (peer->isChat() || peer->isMegagroup())
		? _defaultChatNotifySettings
		: _defaultBroadcastNotifySettings;
}

void NotifySettings::updateNotifySettingsLocal(not_null<PeerData*> peer) {
	const auto history = _owner->historyLoaded(peer->id);
	auto changesIn = crl::time(0);
	const auto muted = isMuted(peer, &changesIn);
	if (history && history->changeMute(muted)) {
		// Notification already sent.
	} else {
		peer->session().changes().peerUpdated(
			peer,
			PeerUpdate::Flag::Notifications);
	}

	if (muted) {
		_mutedPeers.emplace(peer);
		unmuteByFinishedDelayed(changesIn);
		if (history) {
			Core::App().notifications().clearIncomingFromHistory(history);
		}
	} else {
		_mutedPeers.erase(peer);
	}
}

void NotifySettings::unmuteByFinishedDelayed(crl::time delay) {
	accumulate_min(delay, kMaxNotifyCheckDelay);
	if (!_unmuteByFinishedTimer.isActive()
		|| _unmuteByFinishedTimer.remainingTime() > delay) {
		_unmuteByFinishedTimer.callOnce(delay);
	}
}

void NotifySettings::unmuteByFinished() {
	auto changesInMin = crl::time(0);
	for (auto i = begin(_mutedPeers); i != end(_mutedPeers);) {
		const auto history = _owner->historyLoaded((*i)->id);
		auto changesIn = crl::time(0);
		const auto muted = isMuted(*i, &changesIn);
		if (muted) {
			if (history) {
				history->changeMute(true);
			}
			if (!changesInMin || changesInMin > changesIn) {
				changesInMin = changesIn;
			}
			++i;
		} else {
			if (history) {
				history->changeMute(false);
			}
			i = _mutedPeers.erase(i);
		}
	}
	if (changesInMin) {
		unmuteByFinishedDelayed(changesInMin);
	}
}

bool NotifySettings::isMuted(
		not_null<const PeerData*> peer,
		crl::time *changesIn) const {
	const auto resultFromUntil = [&](TimeId until) {
		const auto now = base::unixtime::now();
		const auto result = (until > now) ? (until - now) : 0;
		if (changesIn) {
			*changesIn = (result > 0)
				? std::min(result * crl::time(1000), kMaxNotifyCheckDelay)
				: kMaxNotifyCheckDelay;
		}
		return (result > 0);
	};
	if (const auto until = peer->notifyMuteUntil()) {
		return resultFromUntil(*until);
	}
	const auto &settings = defaultNotifySettings(peer);
	if (const auto until = settings.muteUntil()) {
		return resultFromUntil(*until);
	}
	return true;
}

bool NotifySettings::isMuted(not_null<const PeerData*> peer) const {
	return isMuted(peer, nullptr);
}

bool NotifySettings::silentPosts(not_null<const PeerData*> peer) const {
	if (const auto silent = peer->notifySilentPosts()) {
		return *silent;
	}
	const auto &settings = defaultNotifySettings(peer);
	if (const auto silent = settings.silentPosts()) {
		return *silent;
	}
	return false;
}

NotifySound NotifySettings::sound(not_null<const PeerData*> peer) const {
	if (const auto sound = peer->notifySound()) {
		return *sound;
	}
	const auto &settings = defaultNotifySettings(peer);
	if (const auto sound = settings.sound()) {
		return *sound;
	}
	return {};
}

bool NotifySettings::muteUnknown(not_null<const PeerData*> peer) const {
	if (peer->notifySettingsUnknown()) {
		return true;
	} else if (const auto nonDefault = peer->notifyMuteUntil()) {
		return false;
	}
	return defaultNotifySettings(peer).settingsUnknown();
}

bool NotifySettings::silentPostsUnknown(
		not_null<const PeerData*> peer) const {
	if (peer->notifySettingsUnknown()) {
		return true;
	} else if (const auto nonDefault = peer->notifySilentPosts()) {
		return false;
	}
	return defaultNotifySettings(peer).settingsUnknown();
}

bool NotifySettings::soundUnknown(
		not_null<const PeerData*> peer) const {
	if (peer->notifySettingsUnknown()) {
		return true;
	} else if (const auto nonDefault = peer->notifySound()) {
		return false;
	}
	return defaultNotifySettings(peer).settingsUnknown();
}

bool NotifySettings::settingsUnknown(not_null<const PeerData*> peer) const {
	return muteUnknown(peer)
		|| silentPostsUnknown(peer)
		|| soundUnknown(peer);
}

rpl::producer<> NotifySettings::defaultUserNotifyUpdates() const {
	return _defaultUserNotifyUpdates.events();
}

rpl::producer<> NotifySettings::defaultChatNotifyUpdates() const {
	return _defaultChatNotifyUpdates.events();
}

rpl::producer<> NotifySettings::defaultBroadcastNotifyUpdates() const {
	return _defaultBroadcastNotifyUpdates.events();
}

rpl::producer<> NotifySettings::defaultNotifyUpdates(
		not_null<const PeerData*> peer) const {
	return peer->isUser()
		? defaultUserNotifyUpdates()
		: (peer->isChat() || peer->isMegagroup())
		? defaultChatNotifyUpdates()
		: defaultBroadcastNotifyUpdates();
}

} // namespace Data
