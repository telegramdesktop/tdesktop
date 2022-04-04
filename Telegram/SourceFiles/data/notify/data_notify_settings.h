/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/notify/data_peer_notify_settings.h"

#include "base/timer.h"

class PeerData;

namespace Data {

class Session;

class NotifySettings final {
public:
	NotifySettings(not_null<Session*> owner);

	void requestNotifySettings(not_null<PeerData*> peer);
	void applyNotifySetting(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings);
	void updateNotifySettings(
		not_null<PeerData*> peer,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts = std::nullopt,
		std::optional<NotifySound> sound = std::nullopt);
	void resetNotifySettingsToDefault(not_null<PeerData*> peer);

	[[nodiscard]] rpl::producer<> defaultUserNotifyUpdates() const;
	[[nodiscard]] rpl::producer<> defaultChatNotifyUpdates() const;
	[[nodiscard]] rpl::producer<> defaultBroadcastNotifyUpdates() const;
	[[nodiscard]] rpl::producer<> defaultNotifyUpdates(
		not_null<const PeerData*> peer) const;

	[[nodiscard]] bool isMuted(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool silentPosts(not_null<const PeerData*> peer) const;
	[[nodiscard]] NotifySound sound(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool muteUnknown(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool silentPostsUnknown(
		not_null<const PeerData*> peer) const;
	[[nodiscard]] bool soundUnknown(not_null<const PeerData*> peer) const;

private:
	[[nodiscard]] bool isMuted(
		not_null<const PeerData*> peer,
		crl::time *changesIn) const;

	[[nodiscard]] const PeerNotifySettings &defaultNotifySettings(
		not_null<const PeerData*> peer) const;
	[[nodiscard]] bool settingsUnknown(not_null<const PeerData*> peer) const;

	void unmuteByFinished();
	void unmuteByFinishedDelayed(crl::time delay);
	void updateNotifySettingsLocal(not_null<PeerData*> peer);

	const not_null<Session*> _owner;

	PeerNotifySettings _defaultUserNotifySettings;
	PeerNotifySettings _defaultChatNotifySettings;
	PeerNotifySettings _defaultBroadcastNotifySettings;
	rpl::event_stream<> _defaultUserNotifyUpdates;
	rpl::event_stream<> _defaultChatNotifyUpdates;
	rpl::event_stream<> _defaultBroadcastNotifyUpdates;
	std::unordered_set<not_null<const PeerData*>> _mutedPeers;
	base::Timer _unmuteByFinishedTimer;

};

} // namespace Data
