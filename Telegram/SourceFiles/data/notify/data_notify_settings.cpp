/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/notify/data_notify_settings.h"

#include "apiwrap.h"
#include "api/api_ringtones.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
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

void NotifySettings::request(not_null<PeerData*> peer) {
	if (peer->notifySettingsUnknown()) {
		peer->session().api().requestNotifySettings(
			MTP_inputNotifyPeer(peer->input));
	}
	if (defaultSettings(peer).settingsUnknown()) {
		peer->session().api().requestNotifySettings(peer->isUser()
			? MTP_inputNotifyUsers()
			: (peer->isChat() || peer->isMegagroup())
			? MTP_inputNotifyChats()
			: MTP_inputNotifyBroadcasts());
	}
}

void NotifySettings::apply(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings) {
	const auto set = [&](DefaultNotify type) {
		if (defaultValue(type).settings.change(settings)) {
			updateLocal(type);
		}
	};
	switch (notifyPeer.type()) {
	case mtpc_notifyUsers: set(DefaultNotify::User); break;
	case mtpc_notifyChats: set(DefaultNotify::Group); break;
	case mtpc_notifyBroadcasts: set(DefaultNotify::Broadcast); break;
	case mtpc_notifyPeer: {
		const auto &data = notifyPeer.c_notifyPeer();
		if (const auto peer = _owner->peerLoaded(peerFromMTP(data.vpeer()))) {
			if (peer->notifyChange(settings)) {
				updateLocal(peer);
			}
		}
	} break;
	}
}

void NotifySettings::update(
		not_null<PeerData*> peer,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound) {
	if (peer->notifyChange(muteForSeconds, silentPosts, sound)) {
		updateLocal(peer);
		peer->session().api().updateNotifySettingsDelayed(peer);
	}
}

void NotifySettings::resetToDefault(not_null<PeerData*> peer) {
	const auto empty = MTP_peerNotifySettings(
		MTP_flags(0),
		MTPBool(),
		MTPBool(),
		MTPint(),
		MTPNotificationSound(),
		MTPNotificationSound(),
		MTPNotificationSound());
	if (peer->notifyChange(empty)) {
		updateLocal(peer);
		peer->session().api().updateNotifySettingsDelayed(peer);
	}
}

auto NotifySettings::defaultValue(DefaultNotify type)
-> DefaultValue & {
	const auto index = static_cast<int>(type);
	Assert(index >= 0 && index < base::array_size(_defaultValues));
	return _defaultValues[index];
}

auto NotifySettings::defaultValue(DefaultNotify type) const
-> const DefaultValue & {
	const auto index = static_cast<int>(type);
	Assert(index >= 0 && index < base::array_size(_defaultValues));
	return _defaultValues[index];
}

const PeerNotifySettings &NotifySettings::defaultSettings(
		not_null<const PeerData*> peer) const {
	return defaultSettings(peer->isUser()
		? DefaultNotify::User
		: (peer->isChat() || peer->isMegagroup())
		? DefaultNotify::Group
		: DefaultNotify::Broadcast);
}

const PeerNotifySettings &NotifySettings::defaultSettings(
		DefaultNotify type) const {
	return defaultValue(type).settings;
}

void NotifySettings::defaultUpdate(
		DefaultNotify type,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound) {
	auto &settings = defaultValue(type).settings;
	if (settings.change(muteForSeconds, silentPosts, sound)) {
		updateLocal(type);
		_owner->session().api().updateDefaultNotifySettingsDelayed(type);
	}
}

void NotifySettings::updateLocal(not_null<PeerData*> peer) {
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

	if (const auto sound = peer->notifySound(); sound && sound->id) {
		if (const auto doc = _owner->document(sound->id); !doc->isNull()) {
			cacheSound(doc);
		} else {
			_ringtones.pendingIds.push_back(sound->id);
			if (!_ringtones.pendingLifetime) {
				// Not requested yet.
				_owner->session().api().ringtones().listUpdates(
				) | rpl::start_with_next([=] {
					for (const auto id : base::take(_ringtones.pendingIds)) {
						cacheSound(id);
					}
					_ringtones.pendingLifetime.destroy();
				}, _ringtones.pendingLifetime);
				_owner->session().api().ringtones().requestList();
			}
		}
	}
}

void NotifySettings::cacheSound(DocumentId id) {
	cacheSound(_owner->document(id));
}

void NotifySettings::cacheSound(not_null<DocumentData*> document) {
	if (document->isNull()) {
		return;
	}
	const auto view = document->createMediaView();
	_ringtones.views.emplace(document->id, view);
	document->forceToCache(true);
	document->save(Data::FileOriginRingtones(), QString());
}

void NotifySettings::updateLocal(DefaultNotify type) {
	defaultValue(type).updates.fire({});

	const auto goodForUpdate = [&](
			not_null<const PeerData*> peer,
			const PeerNotifySettings &settings) {
		return !peer->notifySettingsUnknown()
			&& ((!peer->notifyMuteUntil() && settings.muteUntil())
				|| (!peer->notifySilentPosts() && settings.silentPosts())
				|| (!peer->notifySound() && settings.sound()));
	};

	const auto callback = [&](not_null<PeerData*> peer) {
		if (goodForUpdate(peer, defaultSettings(type))) {
			updateLocal(peer);
		}
	};
	switch (type) {
	case DefaultNotify::User: _owner->enumerateUsers(callback); break;
	case DefaultNotify::Group: _owner->enumerateGroups(callback); break;
	case DefaultNotify::Broadcast:
		_owner->enumerateBroadcasts(callback);
		break;
	}
}

std::shared_ptr<DocumentMedia> NotifySettings::lookupRingtone(
		DocumentId id) const {
	if (!id) {
		return nullptr;
	}
	const auto it = _ringtones.views.find(id);
	return (it == end(_ringtones.views)) ? nullptr : it->second;
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
	const auto &settings = defaultSettings(peer);
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
	const auto &settings = defaultSettings(peer);
	if (const auto silent = settings.silentPosts()) {
		return *silent;
	}
	return false;
}

NotifySound NotifySettings::sound(not_null<const PeerData*> peer) const {
	if (const auto sound = peer->notifySound()) {
		return *sound;
	}
	const auto &settings = defaultSettings(peer);
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
	return defaultSettings(peer).settingsUnknown();
}

bool NotifySettings::silentPostsUnknown(
		not_null<const PeerData*> peer) const {
	if (peer->notifySettingsUnknown()) {
		return true;
	} else if (const auto nonDefault = peer->notifySilentPosts()) {
		return false;
	}
	return defaultSettings(peer).settingsUnknown();
}

bool NotifySettings::soundUnknown(
		not_null<const PeerData*> peer) const {
	if (peer->notifySettingsUnknown()) {
		return true;
	} else if (const auto nonDefault = peer->notifySound()) {
		return false;
	}
	return defaultSettings(peer).settingsUnknown();
}

bool NotifySettings::settingsUnknown(not_null<const PeerData*> peer) const {
	return muteUnknown(peer)
		|| silentPostsUnknown(peer)
		|| soundUnknown(peer);
}

rpl::producer<> NotifySettings::defaultUpdates(DefaultNotify type) const {
	return defaultValue(type).updates.events();
}

rpl::producer<> NotifySettings::defaultUpdates(
		not_null<const PeerData*> peer) const {
	return defaultUpdates(peer->isUser()
		? DefaultNotify::User
		: (peer->isChat() || peer->isMegagroup())
		? DefaultNotify::Group
		: DefaultNotify::Broadcast);
}

} // namespace Data
