/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include <rpl/filter.h>
#include <rpl/then.h>
#include <rpl/range.h>
#include "base/observer.h"
#include "base/flags.h"

namespace Notify {

// Generic notifications about updates of some PeerData.
// You can subscribe to them by Notify::registerPeerObserver().
// 0x0000FFFFU for general peer updates (valid for any peer).
// 0xFFFF0000U for specific peer updates (valid for user / chat / channel).
struct PeerUpdate {
	PeerUpdate(PeerData *updated = nullptr) : peer(updated) {
	}
	PeerData *peer;

	enum class Flag : uint32 {
		None                      = 0,

		// Common flags
		NameChanged               = (1 << 0),
		UsernameChanged           = (1 << 1),
		PhotoChanged              = (1 << 2),
		AboutChanged              = (1 << 3),
		NotificationsEnabled      = (1 << 4),
		MigrationChanged          = (1 << 6),
		ChatPinnedChanged         = (1 << 7),
		RestrictionReasonChanged  = (1 << 8),
		UnreadViewChanged         = (1 << 9),
		PinnedMessageChanged      = (1 << 10),
		OccupiedChanged           = (1 << 11),

		// For chats and channels
		InviteLinkChanged         = (1 << 12),
		MembersChanged            = (1 << 13),
		AdminsChanged             = (1 << 14),
		BannedUsersChanged        = (1 << 15),
		UnreadMentionsChanged     = (1 << 16),

		// For users
		UserCanShareContact       = (1 << 17),
		UserIsContact             = (1 << 18),
		UserPhoneChanged          = (1 << 19),
		UserIsBlocked             = (1 << 20),
		BotCommandsChanged        = (1 << 21),
		UserOnlineChanged         = (1 << 22),
		BotCanAddToGroups         = (1 << 23),
		UserCommonChatsChanged    = (1 << 24),
		UserHasCalls              = (1 << 25),

		// For chats
		ChatCanEdit               = (1 << 17),

		// For channels
		ChannelAmIn               = (1 << 17),
		ChannelRightsChanged      = (1 << 18),
		ChannelStickersChanged    = (1 << 19),
		ChannelPromotedChanged    = (1 << 20),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	Flags flags = 0;

	// NameChanged data
	base::flat_set<QChar> oldNameFirstLetters;

};

void peerUpdatedDelayed(const PeerUpdate &update);
inline void peerUpdatedDelayed(PeerData *peer, PeerUpdate::Flags events) {
	PeerUpdate update(peer);
	update.flags = events;
	peerUpdatedDelayed(update);
}
void peerUpdatedSendDelayed();

class PeerUpdatedHandler {
public:
	template <typename Lambda>
	PeerUpdatedHandler(PeerUpdate::Flags events, Lambda &&handler) : _events(events), _handler(std::move(handler)) {
	}
	void operator()(const PeerUpdate &update) const {
		if (update.flags & _events) {
			_handler(update);
		}
	}

private:
	PeerUpdate::Flags _events;
	Fn<void(const PeerUpdate&)> _handler;

};
base::Observable<PeerUpdate, PeerUpdatedHandler> &PeerUpdated();

rpl::producer<PeerUpdate> PeerUpdateViewer(
	PeerUpdate::Flags flags);

rpl::producer<PeerUpdate> PeerUpdateViewer(
	not_null<PeerData*> peer,
	PeerUpdate::Flags flags);

rpl::producer<PeerUpdate> PeerUpdateValue(
	not_null<PeerData*> peer,
	PeerUpdate::Flags flags);

} // namespace Notify
