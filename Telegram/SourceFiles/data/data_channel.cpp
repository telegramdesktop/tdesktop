/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_channel.h"

#include "data/data_peer_values.h"
#include "data/data_channel_admins.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_feed.h"
#include "observer_peer.h"
#include "auth_session.h"

namespace {

using UpdateFlag = Notify::PeerUpdate::Flag;

} // namespace

ChannelData::ChannelData(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id)
, inputChannel(MTP_inputChannel(MTP_int(bareId()), MTP_long(0))) {
	Data::PeerFlagValue(
		this,
		MTPDchannel::Flag::f_megagroup
	) | rpl::start_with_next([this](bool megagroup) {
		if (megagroup) {
			if (!mgInfo) {
				mgInfo = std::make_unique<MegagroupInfo>();
			}
		} else if (mgInfo) {
			mgInfo = nullptr;
		}
	}, _lifetime);
}

void ChannelData::setPhoto(const MTPChatPhoto &photo) {
	setPhoto(userpicPhotoId(), photo);
}

void ChannelData::setPhoto(PhotoId photoId, const MTPChatPhoto &photo) {
	if (photo.type() == mtpc_chatPhoto) {
		const auto &data = photo.c_chatPhoto();
		updateUserpic(photoId, data.vphoto_small);
	} else {
		clearUserpic();
	}
}

void ChannelData::setName(const QString &newName, const QString &newUsername) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), newUsername);
}

bool ChannelData::setAbout(const QString &newAbout) {
	if (_about == newAbout) {
		return false;
	}
	_about = newAbout;
	Notify::peerUpdatedDelayed(this, UpdateFlag::AboutChanged);
	return true;
}

void ChannelData::setInviteLink(const QString &newInviteLink) {
	if (newInviteLink != _inviteLink) {
		_inviteLink = newInviteLink;
		Notify::peerUpdatedDelayed(this, UpdateFlag::InviteLinkChanged);
	}
}

QString ChannelData::inviteLink() const {
	return _inviteLink;
}

bool ChannelData::canHaveInviteLink() const {
	return (adminRights() & AdminRight::f_invite_users)
		|| amCreator();
}

void ChannelData::setMembersCount(int newMembersCount) {
	if (_membersCount != newMembersCount) {
		if (isMegagroup() && !mgInfo->lastParticipants.empty()) {
			mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
			mgInfo->lastParticipantsCount = membersCount();
		}
		_membersCount = newMembersCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::MembersChanged);
	}
}

void ChannelData::setAdminsCount(int newAdminsCount) {
	if (_adminsCount != newAdminsCount) {
		_adminsCount = newAdminsCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::AdminsChanged);
	}
}

void ChannelData::setRestrictedCount(int newRestrictedCount) {
	if (_restrictedCount != newRestrictedCount) {
		_restrictedCount = newRestrictedCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::BannedUsersChanged);
	}
}

void ChannelData::setKickedCount(int newKickedCount) {
	if (_kickedCount != newKickedCount) {
		_kickedCount = newKickedCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::BannedUsersChanged);
	}
}

MTPChatBannedRights ChannelData::KickedRestrictedRights() {
	using Flag = MTPDchatBannedRights::Flag;
	auto flags = Flag::f_view_messages | Flag::f_send_messages | Flag::f_send_media | Flag::f_embed_links | Flag::f_send_stickers | Flag::f_send_gifs | Flag::f_send_games | Flag::f_send_inline;
	return MTP_chatBannedRights(MTP_flags(flags), MTP_int(std::numeric_limits<int32>::max()));
}

void ChannelData::applyEditAdmin(not_null<UserData*> user, const MTPChatAdminRights &oldRights, const MTPChatAdminRights &newRights) {
	auto flags = Notify::PeerUpdate::Flag::AdminsChanged | Notify::PeerUpdate::Flag::None;
	if (mgInfo) {
		// If rights are empty - still add participant? TODO check
		if (!base::contains(mgInfo->lastParticipants, user)) {
			mgInfo->lastParticipants.push_front(user);
			setMembersCount(membersCount() + 1);
			if (user->botInfo && !mgInfo->bots.contains(user)) {
				mgInfo->bots.insert(user);
				if (mgInfo->botStatus != 0 && mgInfo->botStatus < 2) {
					mgInfo->botStatus = 2;
				}
			}
		}
		// If rights are empty - still remove restrictions? TODO check
		if (mgInfo->lastRestricted.contains(user)) {
			mgInfo->lastRestricted.remove(user);
			if (restrictedCount() > 0) {
				setRestrictedCount(restrictedCount() - 1);
			}
		}

		auto userId = peerToUser(user->id);
		auto it = mgInfo->lastAdmins.find(user);
		if (newRights.c_chatAdminRights().vflags.v != 0) {
			auto lastAdmin = MegagroupInfo::Admin { newRights };
			lastAdmin.canEdit = true;
			if (it == mgInfo->lastAdmins.cend()) {
				mgInfo->lastAdmins.emplace(user, lastAdmin);
				setAdminsCount(adminsCount() + 1);
			} else {
				it->second = lastAdmin;
			}
			Data::ChannelAdminChanges(this).feed(userId, true);
		} else {
			if (it != mgInfo->lastAdmins.cend()) {
				mgInfo->lastAdmins.erase(it);
				if (adminsCount() > 0) {
					setAdminsCount(adminsCount() - 1);
				}
			}
			Data::ChannelAdminChanges(this).feed(userId, false);
		}
	}
	if (oldRights.c_chatAdminRights().vflags.v && !newRights.c_chatAdminRights().vflags.v) {
		// We removed an admin.
		if (adminsCount() > 1) {
			setAdminsCount(adminsCount() - 1);
		}
		if (!isMegagroup() && user->botInfo && membersCount() > 1) {
			// Removing bot admin removes it from channel.
			setMembersCount(membersCount() - 1);
		}
	} else if (!oldRights.c_chatAdminRights().vflags.v && newRights.c_chatAdminRights().vflags.v) {
		// We added an admin.
		setAdminsCount(adminsCount() + 1);
		updateFullForced();
	}
	Notify::peerUpdatedDelayed(this, flags);
}

void ChannelData::applyEditBanned(not_null<UserData*> user, const MTPChatBannedRights &oldRights, const MTPChatBannedRights &newRights) {
	auto flags = Notify::PeerUpdate::Flag::BannedUsersChanged | Notify::PeerUpdate::Flag::None;
	auto isKicked = (newRights.c_chatBannedRights().vflags.v & MTPDchatBannedRights::Flag::f_view_messages);
	auto isRestricted = !isKicked && (newRights.c_chatBannedRights().vflags.v != 0);
	if (mgInfo) {
		// If rights are empty - still remove admin? TODO check
		if (mgInfo->lastAdmins.contains(user)) {
			mgInfo->lastAdmins.remove(user);
			if (adminsCount() > 1) {
				setAdminsCount(adminsCount() - 1);
			} else {
				flags |= Notify::PeerUpdate::Flag::AdminsChanged;
			}
		}
		auto it = mgInfo->lastRestricted.find(user);
		if (isRestricted) {
			if (it == mgInfo->lastRestricted.cend()) {
				mgInfo->lastRestricted.emplace(user, MegagroupInfo::Restricted { newRights });
				setRestrictedCount(restrictedCount() + 1);
			} else {
				it->second.rights = newRights;
			}
		} else {
			if (it != mgInfo->lastRestricted.cend()) {
				mgInfo->lastRestricted.erase(it);
				if (restrictedCount() > 0) {
					setRestrictedCount(restrictedCount() - 1);
				}
			}
			if (isKicked) {
				auto i = ranges::find(mgInfo->lastParticipants, user);
				if (i != mgInfo->lastParticipants.end()) {
					mgInfo->lastParticipants.erase(i);
				}
				if (membersCount() > 1) {
					setMembersCount(membersCount() - 1);
				} else {
					mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
					mgInfo->lastParticipantsCount = 0;
				}
				setKickedCount(kickedCount() + 1);
				if (mgInfo->bots.contains(user)) {
					mgInfo->bots.remove(user);
					if (mgInfo->bots.empty() && mgInfo->botStatus > 0) {
						mgInfo->botStatus = -1;
					}
				}
				flags |= Notify::PeerUpdate::Flag::MembersChanged;
				owner().removeMegagroupParticipant(this, user);
			}
		}
		Data::ChannelAdminChanges(this).feed(peerToUser(user->id), false);
	} else {
		if (isKicked) {
			if (membersCount() > 1) {
				setMembersCount(membersCount() - 1);
				flags |= Notify::PeerUpdate::Flag::MembersChanged;
			}
			setKickedCount(kickedCount() + 1);
		}
	}
	Notify::peerUpdatedDelayed(this, flags);
}

bool ChannelData::isGroupAdmin(not_null<UserData*> user) const {
	if (auto info = mgInfo.get()) {
		return info->admins.contains(peerToUser(user->id));
	}
	return false;
}

QString ChannelData::unavailableReason() const {
	return _unavailableReason;
}

void ChannelData::setUnavailableReason(const QString &text) {
	if (_unavailableReason != text) {
		_unavailableReason = text;
		Notify::peerUpdatedDelayed(
			this,
			Notify::PeerUpdate::Flag::UnavailableReasonChanged);
	}
}

void ChannelData::setAvailableMinId(MsgId availableMinId) {
	if (_availableMinId != availableMinId) {
		_availableMinId = availableMinId;
		if (pinnedMessageId() <= _availableMinId) {
			clearPinnedMessage();
		}
	}
}

void ChannelData::setFeed(not_null<Data::Feed*> feed) {
	setFeedPointer(feed);
}

void ChannelData::clearFeed() {
	setFeedPointer(nullptr);
}

void ChannelData::setFeedPointer(Data::Feed *feed) {
	if (_feed != feed) {
		const auto was = _feed;
		_feed = feed;
		if (was) {
			was->unregisterOne(this);
		}
		if (_feed) {
			_feed->registerOne(this);
		}
	}
}

bool ChannelData::canBanMembers() const {
	return (adminRights() & AdminRight::f_ban_users)
		|| amCreator();
}

bool ChannelData::canEditMessages() const {
	return (adminRights() & AdminRight::f_edit_messages)
		|| amCreator();
}

bool ChannelData::canDeleteMessages() const {
	return (adminRights() & AdminRight::f_delete_messages)
		|| amCreator();
}

bool ChannelData::anyoneCanAddMembers() const {
	// #TODO groups
	return false;// (flags() & MTPDchannel::Flag::f_democracy);
}

bool ChannelData::hiddenPreHistory() const {
	return (fullFlags() & MTPDchannelFull::Flag::f_hidden_prehistory);
}

bool ChannelData::canAddMembers() const {
	return (adminRights() & AdminRight::f_invite_users)
		|| amCreator()
		|| (anyoneCanAddMembers()
			&& amIn()
			&& !hasRestrictions());
}

bool ChannelData::canAddAdmins() const {
	return (adminRights() & AdminRight::f_add_admins)
		|| amCreator();
}

bool ChannelData::canPublish() const {
	return (adminRights() & AdminRight::f_post_messages)
		|| amCreator();
}

bool ChannelData::canWrite() const {
	// Duplicated in Data::CanWriteValue().
	return amIn()
		&& (canPublish()
			|| (!isBroadcast()
				&& !restricted(Restriction::f_send_messages)));
}

bool ChannelData::canViewMembers() const {
	return fullFlags()
		& MTPDchannelFull::Flag::f_can_view_participants;
}

bool ChannelData::canViewAdmins() const {
	return (isMegagroup() || hasAdminRights() || amCreator());
}

bool ChannelData::canViewBanned() const {
	return (hasAdminRights() || amCreator());
}

bool ChannelData::canEditInformation() const {
	return (adminRights() & AdminRight::f_change_info)
		|| amCreator();
}

bool ChannelData::canEditInvites() const {
	return canEditInformation();
}

bool ChannelData::canEditSignatures() const {
	return canEditInformation();
}

bool ChannelData::canEditPreHistoryHidden() const {
	return canEditInformation();
}

bool ChannelData::canEditUsername() const {
	return amCreator()
		&& (fullFlags()
			& MTPDchannelFull::Flag::f_can_set_username);
}

bool ChannelData::canEditStickers() const {
	return (fullFlags()
		& MTPDchannelFull::Flag::f_can_set_stickers);
}

bool ChannelData::canDelete() const {
	constexpr auto kDeleteChannelMembersLimit = 1000;
	return amCreator()
		&& (membersCount() <= kDeleteChannelMembersLimit);
}

bool ChannelData::canEditLastAdmin(not_null<UserData*> user) const {
	// Duplicated in ParticipantsBoxController::canEditAdmin :(
	if (mgInfo) {
		auto i = mgInfo->lastAdmins.find(user);
		if (i != mgInfo->lastAdmins.cend()) {
			return i->second.canEdit;
		}
		return (user != mgInfo->creator);
	}
	return false;
}

bool ChannelData::canEditAdmin(not_null<UserData*> user) const {
	// Duplicated in ParticipantsBoxController::canEditAdmin :(
	if (user->isSelf()) {
		return false;
	} else if (amCreator()) {
		return true;
	} else if (!canEditLastAdmin(user)) {
		return false;
	}
	return adminRights() & AdminRight::f_add_admins;
}

bool ChannelData::canRestrictUser(not_null<UserData*> user) const {
	// Duplicated in ParticipantsBoxController::canRestrictUser :(
	if (user->isSelf()) {
		return false;
	} else if (amCreator()) {
		return true;
	} else if (!canEditLastAdmin(user)) {
		return false;
	}
	return adminRights() & AdminRight::f_ban_users;
}

void ChannelData::setAdminRights(const MTPChatAdminRights &rights) {
	if (rights.c_chatAdminRights().vflags.v == adminRights()) {
		return;
	}
	_adminRights.set(rights.c_chatAdminRights().vflags.v);
	if (isMegagroup()) {
		const auto self = session().user();
		if (hasAdminRights()) {
			if (!amCreator()) {
				auto me = MegagroupInfo::Admin { rights };
				me.canEdit = false;
				mgInfo->lastAdmins.emplace(self, me);
			}
			mgInfo->lastRestricted.remove(self);
		} else {
			mgInfo->lastAdmins.remove(self);
		}

		auto amAdmin = hasAdminRights() || amCreator();
		Data::ChannelAdminChanges(this).feed(session().userId(), amAdmin);
	}
	Notify::peerUpdatedDelayed(this, UpdateFlag::ChannelRightsChanged | UpdateFlag::AdminsChanged | UpdateFlag::BannedUsersChanged);
}

void ChannelData::setRestrictedRights(const MTPChatBannedRights &rights) {
	if (rights.c_chatBannedRights().vflags.v == restrictions()
		&& rights.c_chatBannedRights().vuntil_date.v == _restrictedUntill) {
		return;
	}
	_restrictedUntill = rights.c_chatBannedRights().vuntil_date.v;
	_restrictions.set(rights.c_chatBannedRights().vflags.v);
	if (isMegagroup()) {
		const auto self = session().user();
		if (hasRestrictions()) {
			if (!amCreator()) {
				auto me = MegagroupInfo::Restricted { rights };
				mgInfo->lastRestricted.emplace(self, me);
			}
			mgInfo->lastAdmins.remove(self);
			Data::ChannelAdminChanges(this).feed(session().userId(), false);
		} else {
			mgInfo->lastRestricted.remove(self);
		}
	}
	Notify::peerUpdatedDelayed(this, UpdateFlag::ChannelRightsChanged | UpdateFlag::AdminsChanged | UpdateFlag::BannedUsersChanged);
}
