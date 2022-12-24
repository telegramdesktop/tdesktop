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
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"

namespace Data {
namespace {

constexpr auto kMaxNotifyCheckDelay = 24 * 3600 * crl::time(1000);

[[nodiscard]] bool MutedFromUntil(TimeId until, crl::time *changesIn) {
	const auto now = base::unixtime::now();
	const auto result = (until > now) ? (until - now) : 0;
	if (changesIn) {
		*changesIn = (result > 0)
			? std::min(result * crl::time(1000), kMaxNotifyCheckDelay)
			: kMaxNotifyCheckDelay;
	}
	return (result > 0);
}

} // namespace

NotifySettings::NotifySettings(not_null<Session*> owner)
	: _owner(owner)
	, _unmuteByFinishedTimer([=] { unmuteByFinished(); }) {
}

void NotifySettings::request(not_null<PeerData*> peer) {
	if (peer->notify().settingsUnknown()) {
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

void NotifySettings::request(not_null<Data::Thread*> thread) {
	if (const auto topic = thread->asTopic()) {
		if (topic->notify().settingsUnknown()) {
			topic->session().api().requestNotifySettings(
				MTP_inputNotifyForumTopic(
					topic->channel()->input,
					MTP_int(topic->rootId())));
		}
	}
	request(thread->peer());
}

void NotifySettings::apply(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings) {
	notifyPeer.match([&](const MTPDnotifyUsers &) {
		apply(DefaultNotify::User, settings);
	}, [&](const MTPDnotifyChats &) {
		apply(DefaultNotify::Group, settings);
	}, [&](const MTPDnotifyBroadcasts &) {
		apply(DefaultNotify::Broadcast, settings);
	}, [&](const MTPDnotifyPeer &data) {
		apply(peerFromMTP(data.vpeer()), settings);
	}, [&](const MTPDnotifyForumTopic &data) {
		apply(peerFromMTP(data.vpeer()), data.vtop_msg_id().v, settings);
	});
}

void NotifySettings::apply(
		const MTPInputNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings) {
	const auto peerFromInput = [&](const MTPInputPeer &peer) {
		return peer.match([&](const MTPDinputPeerSelf &) {
			return _owner->session().userPeerId();
		}, [](const MTPDinputPeerUser &data) {
			return peerFromUser(data.vuser_id());
		}, [](const MTPDinputPeerChat &data) {
			return peerFromChat(data.vchat_id());
		}, [](const MTPDinputPeerChannel &data) {
			return peerFromChannel(data.vchannel_id());
		}, [](const MTPDinputPeerUserFromMessage &data) -> PeerId {
			Unexpected("From message peer in NotifySettings::apply.");
		}, [](const MTPDinputPeerChannelFromMessage &data) -> PeerId {
			Unexpected("From message peer in NotifySettings::apply.");
		}, [](const MTPDinputPeerEmpty &) -> PeerId {
			Unexpected("Empty peer in NotifySettings::apply.");
		});
	};
	notifyPeer.match([&](const MTPDinputNotifyUsers &) {
		apply(DefaultNotify::User, settings);
	}, [&](const MTPDinputNotifyChats &) {
		apply(DefaultNotify::Group, settings);
	}, [&](const MTPDinputNotifyBroadcasts &) {
		apply(DefaultNotify::Broadcast, settings);
	}, [&](const MTPDinputNotifyPeer &data) {
		apply(peerFromInput(data.vpeer()), settings);
	}, [&](const MTPDinputNotifyForumTopic &data) {
		apply(peerFromInput(data.vpeer()), data.vtop_msg_id().v, settings);
	});
}

void NotifySettings::apply(
		DefaultNotify type,
		const MTPPeerNotifySettings &settings) {
	if (defaultValue(type).settings.change(settings)) {
		updateLocal(type);
		Core::App().notifications().checkDelayed();
	}
}

void NotifySettings::apply(
		PeerId peerId,
		const MTPPeerNotifySettings &settings) {
	if (const auto peer = _owner->peerLoaded(peerId)) {
		apply(peer, settings);
	}
}

void NotifySettings::apply(
		not_null<PeerData*> peer,
		const MTPPeerNotifySettings &settings) {
	if (peer->notify().change(settings)) {
		updateLocal(peer);
		Core::App().notifications().checkDelayed();
	}
}

void NotifySettings::apply(
		PeerId peerId,
		MsgId topicRootId,
		const MTPPeerNotifySettings &settings) {
	if (const auto peer = _owner->peerLoaded(peerId)) {
		if (const auto topic = peer->forumTopicFor(topicRootId)) {
			apply(topic, settings);
		}
	}
}

void NotifySettings::apply(
		not_null<Data::ForumTopic*> topic,
		const MTPPeerNotifySettings &settings) {
	if (topic->notify().change(settings)) {
		updateLocal(topic);
		Core::App().notifications().checkDelayed();
	}
}

void NotifySettings::update(
		not_null<Data::Thread*> thread,
		Data::MuteValue muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound) {
	if (thread->notify().change(muteForSeconds, silentPosts, sound)) {
		updateLocal(thread);
		thread->session().api().updateNotifySettingsDelayed(thread);
	}
}

void NotifySettings::resetToDefault(not_null<Data::Thread*> thread) {
	const auto empty = MTP_peerNotifySettings(
		MTP_flags(0),
		MTPBool(),
		MTPBool(),
		MTPint(),
		MTPNotificationSound(),
		MTPNotificationSound(),
		MTPNotificationSound());
	if (thread->notify().change(empty)) {
		updateLocal(thread);
		thread->session().api().updateNotifySettingsDelayed(thread);
	}
}

void NotifySettings::update(
		not_null<PeerData*> peer,
		Data::MuteValue muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound) {
	if (peer->notify().change(muteForSeconds, silentPosts, sound)) {
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
	if (peer->notify().change(empty)) {
		updateLocal(peer);
		peer->session().api().updateNotifySettingsDelayed(peer);
	}
}

void NotifySettings::forumParentMuteUpdated(not_null<Data::Forum*> forum) {
	forum->enumerateTopics([&](not_null<Data::ForumTopic*> topic) {
		if (!topic->notify().settingsUnknown()) {
			updateLocal(topic);
		}
	});
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
		Data::MuteValue muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound) {
	auto &settings = defaultValue(type).settings;
	if (settings.change(muteForSeconds, silentPosts, sound)) {
		updateLocal(type);
		_owner->session().api().updateNotifySettingsDelayed(type);
	}
}

void NotifySettings::updateLocal(not_null<Data::Thread*> thread) {
	const auto topic = thread->asTopic();
	if (!topic) {
		return updateLocal(thread->peer());
	}
	auto changesIn = crl::time(0);
	const auto muted = isMuted(topic, &changesIn);
	topic->setMuted(muted);
	if (muted) {
		auto &lifetime = _mutedTopics.emplace(
			topic,
			rpl::lifetime()).first->second;
		topic->destroyed() | rpl::start_with_next([=] {
			_mutedTopics.erase(topic);
		}, lifetime);
		unmuteByFinishedDelayed(changesIn);
		Core::App().notifications().clearIncomingFromTopic(topic);
	} else {
		_mutedTopics.erase(topic);
	}
	cacheSound(topic->notify().sound());
}

void NotifySettings::updateLocal(not_null<PeerData*> peer) {
	const auto history = _owner->historyLoaded(peer->id);
	auto changesIn = crl::time(0);
	const auto muted = isMuted(peer, &changesIn);
	const auto changeInHistory = history && (history->muted() != muted);
	if (changeInHistory) {
		history->setMuted(muted);
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
	cacheSound(peer->notify().sound());
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

void NotifySettings::cacheSound(const std::optional<NotifySound> &sound) {
	if (!sound || !sound->id) {
		return;
	} else if (const auto doc = _owner->document(sound->id); !doc->isNull()) {
		cacheSound(doc);
		return;
	}
	_ringtones.pendingIds.push_back(sound->id);
	if (_ringtones.pendingLifetime) {
		return;
	}
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

void NotifySettings::updateLocal(DefaultNotify type) {
	defaultValue(type).updates.fire({});

	const auto goodForUpdate = [&](
			not_null<const PeerData*> peer,
			const PeerNotifySettings &settings) {
		auto &peers = peer->notify();
		return !peers.settingsUnknown()
			&& ((!peers.muteUntil() && settings.muteUntil())
				|| (!peers.silentPosts() && settings.silentPosts())
				|| (!peers.sound() && settings.sound()));
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
	cacheSound(defaultValue(type).settings.sound());
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
		if (history) {
			history->setMuted(muted);
		}
		if (muted) {
			if (!changesInMin || changesInMin > changesIn) {
				changesInMin = changesIn;
			}
			++i;
		} else {
			i = _mutedPeers.erase(i);
		}
	}
	for (auto i = begin(_mutedTopics); i != end(_mutedTopics);) {
		auto changesIn = crl::time(0);
		const auto topic = i->first;
		const auto muted = isMuted(topic, &changesIn);
		topic->setMuted(muted);
		if (muted) {
			if (!changesInMin || changesInMin > changesIn) {
				changesInMin = changesIn;
			}
			++i;
		} else {
			i = _mutedTopics.erase(i);
		}
	}
	if (changesInMin) {
		unmuteByFinishedDelayed(changesInMin);
	}
}

bool NotifySettings::isMuted(
		not_null<const Data::Thread*> thread,
		crl::time *changesIn) const {
	const auto topic = thread->asTopic();
	const auto until = topic ? topic->notify().muteUntil() : std::nullopt;
	return until
		? MutedFromUntil(*until, changesIn)
		: isMuted(thread->peer(), changesIn);
}

bool NotifySettings::isMuted(not_null<const Data::Thread*> thread) const {
	return isMuted(thread, nullptr);
}

NotifySound NotifySettings::sound(
		not_null<const Data::Thread*> thread) const {
	const auto topic = thread->asTopic();
	const auto sound = topic ? topic->notify().sound() : std::nullopt;
	return sound ? *sound : this->sound(thread->peer());
}

bool NotifySettings::muteUnknown(
		not_null<const Data::Thread*> thread) const {
	const auto topic = thread->asTopic();
	return (topic && topic->notify().settingsUnknown())
		|| ((!topic || !topic->notify().muteUntil().has_value())
			&& muteUnknown(thread->peer()));
}

bool NotifySettings::soundUnknown(
		not_null<const Data::Thread*> thread) const {
	const auto topic = thread->asTopic();
	return (topic && topic->notify().settingsUnknown())
		|| ((!topic || !topic->notify().sound().has_value())
			&& soundUnknown(topic->channel()));
}

bool NotifySettings::isMuted(
		not_null<const PeerData*> peer,
		crl::time *changesIn) const {
	if (const auto until = peer->notify().muteUntil()) {
		return MutedFromUntil(*until, changesIn);
	} else if (const auto until = defaultSettings(peer).muteUntil()) {
		return MutedFromUntil(*until, changesIn);
	}
	return true;
}

bool NotifySettings::isMuted(not_null<const PeerData*> peer) const {
	return isMuted(peer, nullptr);
}

bool NotifySettings::silentPosts(not_null<const PeerData*> peer) const {
	if (const auto silent = peer->notify().silentPosts()) {
		return *silent;
	} else if (const auto silent = defaultSettings(peer).silentPosts()) {
		return *silent;
	}
	return false;
}

NotifySound NotifySettings::sound(not_null<const PeerData*> peer) const {
	if (const auto sound = peer->notify().sound()) {
		return *sound;
	} else if (const auto sound = defaultSettings(peer).sound()) {
		return *sound;
	}
	return {};
}

bool NotifySettings::muteUnknown(not_null<const PeerData*> peer) const {
	return peer->notify().settingsUnknown()
		|| (!peer->notify().muteUntil().has_value()
			&& defaultSettings(peer).settingsUnknown());
}

bool NotifySettings::silentPostsUnknown(
		not_null<const PeerData*> peer) const {
	return peer->notify().settingsUnknown()
		|| (!peer->notify().silentPosts().has_value()
			&& defaultSettings(peer).settingsUnknown());
}

bool NotifySettings::soundUnknown(
		not_null<const PeerData*> peer) const {
	return peer->notify().settingsUnknown()
		|| (!peer->notify().sound().has_value()
			&& defaultSettings(peer).settingsUnknown());
}

bool NotifySettings::settingsUnknown(not_null<const PeerData*> peer) const {
	return muteUnknown(peer)
		|| silentPostsUnknown(peer)
		|| soundUnknown(peer);
}

bool NotifySettings::settingsUnknown(
		not_null<const Data::Thread*> thread) const {
	const auto topic = thread->asTopic();
	return muteUnknown(thread)
		|| soundUnknown(thread)
		|| (!topic && silentPostsUnknown(thread->peer()));
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
