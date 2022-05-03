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

class DocumentMedia;
class Session;

enum class DefaultNotify {
	User,
	Group,
	Broadcast,
};

class NotifySettings final {
public:
	NotifySettings(not_null<Session*> owner);

	void request(not_null<PeerData*> peer);
	void apply(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings);
	void update(
		not_null<PeerData*> peer,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts = std::nullopt,
		std::optional<NotifySound> sound = std::nullopt);
	void resetToDefault(not_null<PeerData*> peer);

	void cacheSound(DocumentId id);
	void cacheSound(not_null<DocumentData*> document);
	[[nodiscard]] std::shared_ptr<DocumentMedia> lookupRingtone(
		DocumentId id) const;

	[[nodiscard]] rpl::producer<> defaultUpdates(DefaultNotify type) const;
	[[nodiscard]] rpl::producer<> defaultUpdates(
		not_null<const PeerData*> peer) const;

	[[nodiscard]] const PeerNotifySettings &defaultSettings(
		DefaultNotify type) const;

	void defaultUpdate(
		DefaultNotify type,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts = std::nullopt,
		std::optional<NotifySound> sound = std::nullopt);

	[[nodiscard]] bool isMuted(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool silentPosts(not_null<const PeerData*> peer) const;
	[[nodiscard]] NotifySound sound(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool muteUnknown(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool silentPostsUnknown(
		not_null<const PeerData*> peer) const;
	[[nodiscard]] bool soundUnknown(not_null<const PeerData*> peer) const;

private:
	struct DefaultValue {
		PeerNotifySettings settings;
		rpl::event_stream<> updates;
	};

	void cacheSound(const std::optional<NotifySound> &sound);

	[[nodiscard]] bool isMuted(
		not_null<const PeerData*> peer,
		crl::time *changesIn) const;

	[[nodiscard]] DefaultValue &defaultValue(DefaultNotify type);
	[[nodiscard]] const DefaultValue &defaultValue(DefaultNotify type) const;
	[[nodiscard]] const PeerNotifySettings &defaultSettings(
		not_null<const PeerData*> peer) const;
	[[nodiscard]] bool settingsUnknown(not_null<const PeerData*> peer) const;

	void unmuteByFinished();
	void unmuteByFinishedDelayed(crl::time delay);
	void updateLocal(not_null<PeerData*> peer);
	void updateLocal(DefaultNotify type);

	const not_null<Session*> _owner;

	DefaultValue _defaultValues[3];
	std::unordered_set<not_null<const PeerData*>> _mutedPeers;
	base::Timer _unmuteByFinishedTimer;

	struct {
		base::flat_map<
			DocumentId,
			std::shared_ptr<DocumentMedia>> views;
		std::vector<DocumentId> pendingIds;
		rpl::lifetime pendingLifetime;
	} _ringtones;

};

} // namespace Data
