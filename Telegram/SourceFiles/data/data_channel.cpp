/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_channel.h"

#include "data/data_peer_values.h"
#include "data/data_changes.h"
#include "data/data_channel_admins.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_location.h"
#include "data/data_histories.h"
#include "data/data_group_call.h"
#include "base/unixtime.h"
#include "history/history.h"
#include "main/main_session.h"
#include "api/api_chat_invite.h"
#include "apiwrap.h"

namespace {

using UpdateFlag = Data::PeerUpdate::Flag;

} // namespace

ChatData *MegagroupInfo::getMigrateFromChat() const {
	return _migratedFrom;
}

void MegagroupInfo::setMigrateFromChat(ChatData *chat) {
	_migratedFrom = chat;
}

const ChannelLocation *MegagroupInfo::getLocation() const {
	return _location.address.isEmpty() ? nullptr : &_location;
}

void MegagroupInfo::setLocation(const ChannelLocation &location) {
	_location = location;
}

ChannelData::ChannelData(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id)
, inputChannel(MTP_inputChannel(MTP_int(bareId()), MTP_long(0)))
, _ptsWaiter(&owner->session().updates()) {
	_flags.changes(
	) | rpl::start_with_next([=](const Flags::Change &change) {
		if (change.diff
			& (MTPDchannel::Flag::f_left | MTPDchannel_ClientFlag::f_forbidden)) {
			if (const auto chat = getMigrateFromChat()) {
				session().changes().peerUpdated(chat, UpdateFlag::Migration);
				session().changes().peerUpdated(this, UpdateFlag::Migration);
			}
		}
		if (change.diff & MTPDchannel::Flag::f_megagroup) {
			if (change.value & MTPDchannel::Flag::f_megagroup) {
				if (!mgInfo) {
					mgInfo = std::make_unique<MegagroupInfo>();
				}
			} else if (mgInfo) {
				mgInfo = nullptr;
			}
		}
		if (change.diff & MTPDchannel::Flag::f_call_not_empty) {
			if (const auto history = this->owner().historyLoaded(this)) {
				history->updateChatListEntry();
			}
		}
	}, _lifetime);
}

void ChannelData::setPhoto(const MTPChatPhoto &photo) {
	setPhoto(userpicPhotoId(), photo);
}

void ChannelData::setPhoto(PhotoId photoId, const MTPChatPhoto &photo) {
	photo.match([&](const MTPDchatPhoto & data) {
		updateUserpic(photoId, data.vdc_id().v, data.vphoto_small());
	}, [&](const MTPDchatPhotoEmpty &) {
		clearUserpic();
	});
}

void ChannelData::setName(const QString &newName, const QString &newUsername) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), newUsername);
}

void ChannelData::setAccessHash(uint64 accessHash) {
	access = accessHash;
	input = MTP_inputPeerChannel(MTP_int(bareId()), MTP_long(accessHash));
	inputChannel = MTP_inputChannel(MTP_int(bareId()), MTP_long(accessHash));
}

void ChannelData::setInviteLink(const QString &newInviteLink) {
	if (newInviteLink != _inviteLink) {
		_inviteLink = newInviteLink;
		session().changes().peerUpdated(this, UpdateFlag::InviteLink);
	}
}

bool ChannelData::canHaveInviteLink() const {
	return amCreator()
		|| (adminRights() & AdminRight::f_invite_users);
}

void ChannelData::setLocation(const MTPChannelLocation &data) {
	if (!mgInfo) {
		return;
	}
	const auto was = mgInfo->getLocation();
	const auto wasValue = was ? *was : ChannelLocation();
	data.match([&](const MTPDchannelLocation &data) {
		data.vgeo_point().match([&](const MTPDgeoPoint &point) {
			mgInfo->setLocation({
				qs(data.vaddress()),
				Data::LocationPoint(point)
			});
		}, [&](const MTPDgeoPointEmpty &) {
			mgInfo->setLocation(ChannelLocation());
		});
	}, [&](const MTPDchannelLocationEmpty &) {
		mgInfo->setLocation(ChannelLocation());
	});
	const auto now = mgInfo->getLocation();
	const auto nowValue = now ? *now : ChannelLocation();
	if (was != now || (was && wasValue != nowValue)) {
		session().changes().peerUpdated(
			this,
			UpdateFlag::ChannelLocation);
	}
}

const ChannelLocation *ChannelData::getLocation() const {
	return mgInfo ? mgInfo->getLocation() : nullptr;
}

void ChannelData::setLinkedChat(ChannelData *linked) {
	if (_linkedChat != linked) {
		_linkedChat = linked;
		if (const auto history = owner().historyLoaded(this)) {
			history->forceFullResize();
		}
		session().changes().peerUpdated(this, UpdateFlag::ChannelLinkedChat);
	}
}

ChannelData *ChannelData::linkedChat() const {
	return _linkedChat.value_or(nullptr);
}

bool ChannelData::linkedChatKnown() const {
	return _linkedChat.has_value();
}

void ChannelData::setMembersCount(int newMembersCount) {
	if (_membersCount != newMembersCount) {
		if (isMegagroup() && !mgInfo->lastParticipants.empty()) {
			mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
			mgInfo->lastParticipantsCount = membersCount();
		}
		_membersCount = newMembersCount;
		session().changes().peerUpdated(this, UpdateFlag::Members);
	}
}

void ChannelData::setAdminsCount(int newAdminsCount) {
	if (_adminsCount != newAdminsCount) {
		_adminsCount = newAdminsCount;
		session().changes().peerUpdated(this, UpdateFlag::Admins);
	}
}

void ChannelData::setRestrictedCount(int newRestrictedCount) {
	if (_restrictedCount != newRestrictedCount) {
		_restrictedCount = newRestrictedCount;
		session().changes().peerUpdated(this, UpdateFlag::BannedUsers);
	}
}

void ChannelData::setKickedCount(int newKickedCount) {
	if (_kickedCount != newKickedCount) {
		_kickedCount = newKickedCount;
		session().changes().peerUpdated(this, UpdateFlag::BannedUsers);
	}
}

MTPChatBannedRights ChannelData::KickedRestrictedRights() {
	using Flag = MTPDchatBannedRights::Flag;
	const auto flags = Flag::f_view_messages
		| Flag::f_send_messages
		| Flag::f_send_media
		| Flag::f_embed_links
		| Flag::f_send_stickers
		| Flag::f_send_gifs
		| Flag::f_send_games
		| Flag::f_send_inline;
	return MTP_chatBannedRights(
		MTP_flags(flags),
		MTP_int(std::numeric_limits<int32>::max()));
}

void ChannelData::applyEditAdmin(
		not_null<UserData*> user,
		const MTPChatAdminRights &oldRights,
		const MTPChatAdminRights &newRights,
		const QString &rank) {
	if (mgInfo) {
		// If rights are empty - still add participant? TODO check
		if (!base::contains(mgInfo->lastParticipants, user)) {
			mgInfo->lastParticipants.push_front(user);
			setMembersCount(membersCount() + 1);
			if (user->isBot() && !mgInfo->bots.contains(user)) {
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
		if (newRights.c_chatAdminRights().vflags().v != 0) {
			auto lastAdmin = MegagroupInfo::Admin { newRights };
			lastAdmin.canEdit = true;
			if (it == mgInfo->lastAdmins.cend()) {
				mgInfo->lastAdmins.emplace(user, lastAdmin);
				setAdminsCount(adminsCount() + 1);
			} else {
				it->second = lastAdmin;
			}
			Data::ChannelAdminChanges(this).add(userId, rank);
		} else {
			if (it != mgInfo->lastAdmins.cend()) {
				mgInfo->lastAdmins.erase(it);
				if (adminsCount() > 0) {
					setAdminsCount(adminsCount() - 1);
				}
			}
			Data::ChannelAdminChanges(this).remove(userId);
		}
	}
	if (oldRights.c_chatAdminRights().vflags().v && !newRights.c_chatAdminRights().vflags().v) {
		// We removed an admin.
		if (adminsCount() > 1) {
			setAdminsCount(adminsCount() - 1);
		}
		if (!isMegagroup() && user->isBot() && membersCount() > 1) {
			// Removing bot admin removes it from channel.
			setMembersCount(membersCount() - 1);
		}
	} else if (!oldRights.c_chatAdminRights().vflags().v && newRights.c_chatAdminRights().vflags().v) {
		// We added an admin.
		setAdminsCount(adminsCount() + 1);
		updateFullForced();
	}
	session().changes().peerUpdated(this, UpdateFlag::Admins);
}

void ChannelData::applyEditBanned(not_null<UserData*> user, const MTPChatBannedRights &oldRights, const MTPChatBannedRights &newRights) {
	auto flags = UpdateFlag::BannedUsers | UpdateFlag::None;
	auto isKicked = (newRights.c_chatBannedRights().vflags().v & MTPDchatBannedRights::Flag::f_view_messages);
	auto isRestricted = !isKicked && (newRights.c_chatBannedRights().vflags().v != 0);
	if (mgInfo) {
		// If rights are empty - still remove admin? TODO check
		if (mgInfo->lastAdmins.contains(user)) {
			mgInfo->lastAdmins.remove(user);
			if (adminsCount() > 1) {
				setAdminsCount(adminsCount() - 1);
			} else {
				flags |= UpdateFlag::Admins;
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
				flags |= UpdateFlag::Members;
				owner().removeMegagroupParticipant(this, user);
			}
		}
		Data::ChannelAdminChanges(this).remove(peerToUser(user->id));
	} else {
		if (isKicked) {
			if (membersCount() > 1) {
				setMembersCount(membersCount() - 1);
				flags |= UpdateFlag::Members;
			}
			setKickedCount(kickedCount() + 1);
		}
	}
	session().changes().peerUpdated(this, flags);
}

void ChannelData::markForbidden() {
	owner().processChat(MTP_channelForbidden(
		MTP_flags(isMegagroup()
			? MTPDchannelForbidden::Flag::f_megagroup
			: MTPDchannelForbidden::Flag::f_broadcast),
		MTP_int(bareId()),
		MTP_long(access),
		MTP_string(name),
		MTPint()));
}

bool ChannelData::isGroupAdmin(not_null<UserData*> user) const {
	if (auto info = mgInfo.get()) {
		return info->admins.contains(peerToUser(user->id));
	}
	return false;
}

bool ChannelData::lastParticipantsRequestNeeded() const {
	if (!mgInfo) {
		return false;
	} else if (mgInfo->lastParticipantsCount == membersCount()) {
		mgInfo->lastParticipantsStatus
			&= ~MegagroupInfo::LastParticipantsCountOutdated;
	}
	return mgInfo->lastParticipants.empty()
		|| !(mgInfo->lastParticipantsStatus
			& MegagroupInfo::LastParticipantsOnceReceived)
		|| (mgInfo->lastParticipantsStatus
			& MegagroupInfo::LastParticipantsCountOutdated);
}

auto ChannelData::unavailableReasons() const
-> const std::vector<Data::UnavailableReason> & {
	return _unavailableReasons;
}

void ChannelData::setUnavailableReasons(
		std::vector<Data::UnavailableReason> &&reasons) {
	if (_unavailableReasons != reasons) {
		_unavailableReasons = std::move(reasons);
		session().changes().peerUpdated(this, UpdateFlag::UnavailableReason);
	}
}

void ChannelData::setAvailableMinId(MsgId availableMinId) {
	if (_availableMinId != availableMinId) {
		_availableMinId = availableMinId;
	}
}

bool ChannelData::canBanMembers() const {
	return amCreator()
		|| (adminRights() & AdminRight::f_ban_users);
}

bool ChannelData::canEditMessages() const {
	return amCreator()
		|| (adminRights() & AdminRight::f_edit_messages);
}

bool ChannelData::canDeleteMessages() const {
	return amCreator()
		|| (adminRights() & AdminRight::f_delete_messages);
}

bool ChannelData::anyoneCanAddMembers() const {
	return !(defaultRestrictions() & Restriction::f_invite_users);
}

bool ChannelData::hiddenPreHistory() const {
	return (fullFlags() & MTPDchannelFull::Flag::f_hidden_prehistory);
}

bool ChannelData::canAddMembers() const {
	return isMegagroup()
		? !amRestricted(ChatRestriction::f_invite_users)
		: ((adminRights() & AdminRight::f_invite_users) || amCreator());
}

bool ChannelData::canSendPolls() const {
	return canWrite() && !amRestricted(ChatRestriction::f_send_polls);
}

bool ChannelData::canAddAdmins() const {
	return amCreator()
		|| (adminRights() & AdminRight::f_add_admins);
}

bool ChannelData::canPublish() const {
	return amCreator()
		|| (adminRights() & AdminRight::f_post_messages);
}

bool ChannelData::canWrite() const {
	// Duplicated in Data::CanWriteValue().
	const auto allowed = amIn() || (flags() & MTPDchannel::Flag::f_has_link);
	return allowed && (canPublish()
			|| (!isBroadcast()
				&& !amRestricted(Restriction::f_send_messages)));
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
	return isMegagroup()
		? !amRestricted(Restriction::f_change_info)
		: ((adminRights() & AdminRight::f_change_info) || amCreator());
}

bool ChannelData::canEditPermissions() const {
	return isMegagroup()
		&& ((adminRights() & AdminRight::f_ban_users) || amCreator());
}

bool ChannelData::canEditSignatures() const {
	return isChannel() && canEditInformation();
}

bool ChannelData::canEditPreHistoryHidden() const {
	return isMegagroup()
		&& ((adminRights() & AdminRight::f_ban_users) || amCreator())
		&& (!isPublic() || canEditUsername());
}

bool ChannelData::canEditUsername() const {
	return amCreator()
		&& (fullFlags() & MTPDchannelFull::Flag::f_can_set_username);
}

bool ChannelData::canEditStickers() const {
	return (fullFlags() & MTPDchannelFull::Flag::f_can_set_stickers);
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
	if (rights.c_chatAdminRights().vflags().v == adminRights()) {
		return;
	}
	_adminRights.set(rights.c_chatAdminRights().vflags().v);
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
	}
	session().changes().peerUpdated(
		this,
		UpdateFlag::Rights | UpdateFlag::Admins | UpdateFlag::BannedUsers);
}

void ChannelData::setRestrictions(const MTPChatBannedRights &rights) {
	if (rights.c_chatBannedRights().vflags().v == restrictions()
		&& rights.c_chatBannedRights().vuntil_date().v == _restrictedUntil) {
		return;
	}
	_restrictedUntil = rights.c_chatBannedRights().vuntil_date().v;
	_restrictions.set(rights.c_chatBannedRights().vflags().v);
	if (isMegagroup()) {
		const auto self = session().user();
		if (hasRestrictions()) {
			if (!amCreator()) {
				auto me = MegagroupInfo::Restricted { rights };
				mgInfo->lastRestricted.emplace(self, me);
			}
			mgInfo->lastAdmins.remove(self);
			Data::ChannelAdminChanges(this).remove(session().userId());
		} else {
			mgInfo->lastRestricted.remove(self);
		}
	}
	session().changes().peerUpdated(
		this,
		UpdateFlag::Rights | UpdateFlag::Admins | UpdateFlag::BannedUsers);
}

void ChannelData::setDefaultRestrictions(const MTPChatBannedRights &rights) {
	if (rights.c_chatBannedRights().vflags().v == defaultRestrictions()) {
		return;
	}
	_defaultRestrictions.set(rights.c_chatBannedRights().vflags().v);
	session().changes().peerUpdated(this, UpdateFlag::Rights);
}

auto ChannelData::applyUpdateVersion(int version) -> UpdateStatus {
	if (_version > version) {
		return UpdateStatus::TooOld;
	} else if (_version + 1 < version) {
		session().api().requestPeer(this);
		return UpdateStatus::Skipped;
	}
	setVersion(version);
	return UpdateStatus::Good;
}

ChatData *ChannelData::getMigrateFromChat() const {
	if (const auto info = mgInfo.get()) {
		return info->getMigrateFromChat();
	}
	return nullptr;
}

void ChannelData::setMigrateFromChat(ChatData *chat) {
	Expects(mgInfo != nullptr);

	const auto info = mgInfo.get();
	if (chat != info->getMigrateFromChat()) {
		info->setMigrateFromChat(chat);
		if (amIn()) {
			session().changes().peerUpdated(this, UpdateFlag::Migration);
		}
	}
}

int ChannelData::slowmodeSeconds() const {
	return _slowmodeSeconds;
}

void ChannelData::setSlowmodeSeconds(int seconds) {
	if (_slowmodeSeconds == seconds) {
		return;
	}
	_slowmodeSeconds = seconds;
	session().changes().peerUpdated(this, UpdateFlag::Slowmode);
}

TimeId ChannelData::slowmodeLastMessage() const {
	return (hasAdminRights() || amCreator()) ? 0 : _slowmodeLastMessage;
}

void ChannelData::growSlowmodeLastMessage(TimeId when) {
	const auto now = base::unixtime::now();
	accumulate_min(when, now);
	if (_slowmodeLastMessage > now) {
		_slowmodeLastMessage = when;
	} else if (_slowmodeLastMessage >= when) {
		return;
	} else {
		_slowmodeLastMessage = when;
	}
	session().changes().peerUpdated(this, UpdateFlag::Slowmode);
}

void ChannelData::setInvitePeek(const QString &hash, TimeId expires) {
	if (!_invitePeek) {
		_invitePeek = std::make_unique<InvitePeek>();
	}
	_invitePeek->hash = hash;
	_invitePeek->expires = expires;
}

void ChannelData::clearInvitePeek() {
	_invitePeek = nullptr;
}

TimeId ChannelData::invitePeekExpires() const {
	return _invitePeek ? _invitePeek->expires : 0;
}

QString ChannelData::invitePeekHash() const {
	return _invitePeek ? _invitePeek->hash : QString();
}

void ChannelData::privateErrorReceived() {
	if (const auto expires = invitePeekExpires()) {
		const auto hash = invitePeekHash();
		for (const auto window : session().windows()) {
			clearInvitePeek();
			Api::CheckChatInvite(window, hash, this);
			return;
		}
		_invitePeek->expires = base::unixtime::now();
	} else {
		markForbidden();
	}
}

void ChannelData::migrateCall(std::unique_ptr<Data::GroupCall> call) {
	Expects(_call == nullptr);
	Expects(call != nullptr);

	_call = std::move(call);
	_call->setPeer(this);
	session().changes().peerUpdated(this, UpdateFlag::GroupCall);
	addFlags(MTPDchannel::Flag::f_call_active);
}

void ChannelData::setGroupCall(const MTPInputGroupCall &call) {
	call.match([&](const MTPDinputGroupCall &data) {
		if (_call && _call->id() == data.vid().v) {
			return;
		} else if (!_call && !data.vid().v) {
			return;
		} else if (!data.vid().v) {
			clearGroupCall();
			return;
		}
		const auto hasCall = (_call != nullptr);
		if (hasCall) {
			owner().unregisterGroupCall(_call.get());
		}
		_call = std::make_unique<Data::GroupCall>(
			this,
			data.vid().v,
			data.vaccess_hash().v);
		owner().registerGroupCall(_call.get());
		session().changes().peerUpdated(this, UpdateFlag::GroupCall);
		addFlags(MTPDchannel::Flag::f_call_active);
	});
}

void ChannelData::clearGroupCall() {
	if (!_call) {
		return;
	}
	owner().unregisterGroupCall(_call.get());
	_call = nullptr;
	session().changes().peerUpdated(this, UpdateFlag::GroupCall);
	removeFlags(MTPDchannel::Flag::f_call_active
		| MTPDchannel::Flag::f_call_not_empty);
}

namespace Data {

void ApplyMigration(
		not_null<ChatData*> chat,
		not_null<ChannelData*> channel) {
	Expects(channel->isMegagroup());

	chat->setMigrateToChannel(channel);
	channel->setMigrateFromChat(chat);
}

void ApplyChannelUpdate(
		not_null<ChannelData*> channel,
		const MTPDupdateChatDefaultBannedRights &update) {
	if (channel->applyUpdateVersion(update.vversion().v)
		!= ChannelData::UpdateStatus::Good) {
		return;
	}
	channel->setDefaultRestrictions(update.vdefault_banned_rights());
}

void ApplyChannelUpdate(
		not_null<ChannelData*> channel,
		const MTPDchannelFull &update) {
	const auto session = &channel->session();

	channel->setAvailableMinId(update.vavailable_min_id().value_or_empty());
	auto canViewAdmins = channel->canViewAdmins();
	auto canViewMembers = channel->canViewMembers();
	auto canEditStickers = channel->canEditStickers();

	if (const auto call = update.vcall()) {
		channel->setGroupCall(*call);
	} else {
		channel->clearGroupCall();
	}

	channel->setFullFlags(update.vflags().v);
	channel->setUserpicPhoto(update.vchat_photo());
	if (const auto migratedFrom = update.vmigrated_from_chat_id()) {
		channel->addFlags(MTPDchannel::Flag::f_megagroup);
		const auto chat = channel->owner().chat(migratedFrom->v);
		Data::ApplyMigration(chat, channel);
	}
	for (const auto &item : update.vbot_info().v) {
		auto &owner = channel->owner();
		item.match([&](const MTPDbotInfo &info) {
			if (const auto user = owner.userLoaded(info.vuser_id().v)) {
				user->setBotInfo(item);
				session->api().fullPeerUpdated().notify(user);
			}
		});
	}
	channel->setAbout(qs(update.vabout()));
	channel->setMembersCount(update.vparticipants_count().value_or_empty());
	channel->setAdminsCount(update.vadmins_count().value_or_empty());
	channel->setRestrictedCount(update.vbanned_count().value_or_empty());
	channel->setKickedCount(update.vkicked_count().value_or_empty());
	channel->setSlowmodeSeconds(update.vslowmode_seconds().value_or_empty());
	if (const auto next = update.vslowmode_next_send_date()) {
		channel->growSlowmodeLastMessage(
			next->v - channel->slowmodeSeconds());
	}
	channel->setInviteLink(update.vexported_invite().match([&](
			const MTPDchatInviteExported &data) {
		return qs(data.vlink());
	}, [&](const MTPDchatInviteEmpty &) {
		return QString();
	}));
	if (const auto location = update.vlocation()) {
		channel->setLocation(*location);
	} else {
		channel->setLocation(MTP_channelLocationEmpty());
	}
	if (const auto chat = update.vlinked_chat_id()) {
		channel->setLinkedChat(channel->owner().channelLoaded(chat->v));
	} else {
		channel->setLinkedChat(nullptr);
	}
	if (const auto history = channel->owner().historyLoaded(channel)) {
		if (const auto available = update.vavailable_min_id()) {
			history->clearUpTill(available->v);
		}
		const auto folderId = update.vfolder_id().value_or_empty();
		const auto folder = folderId
			? channel->owner().folderLoaded(folderId)
			: nullptr;
		auto &histories = channel->owner().histories();
		if (folder && history->folder() != folder) {
			// If history folder is unknown or not synced, request both.
			histories.requestDialogEntry(history);
			histories.requestDialogEntry(folder);
		} else if (!history->folderKnown()
			|| channel->pts() != update.vpts().v) {
			histories.requestDialogEntry(history);
		} else {
			history->applyDialogFields(
				history->folder(),
				update.vunread_count().v,
				update.vread_inbox_max_id().v,
				update.vread_outbox_max_id().v);
		}
	}
	if (const auto pinned = update.vpinned_msg_id()) {
		SetTopPinnedMessageId(channel, pinned->v);
	}
	if (channel->isMegagroup()) {
		const auto stickerSet = update.vstickerset();
		const auto set = stickerSet ? &stickerSet->c_stickerSet() : nullptr;
		const auto newSetId = (set ? set->vid().v : 0);
		const auto oldSetId = (channel->mgInfo->stickerSet.type() == mtpc_inputStickerSetID)
			? channel->mgInfo->stickerSet.c_inputStickerSetID().vid().v
			: 0;
		const auto stickersChanged = (canEditStickers != channel->canEditStickers())
			|| (oldSetId != newSetId);
		if (oldSetId != newSetId) {
			channel->mgInfo->stickerSet = set
				? MTP_inputStickerSetID(set->vid(), set->vaccess_hash())
				: MTP_inputStickerSetEmpty();
		}
		if (stickersChanged) {
			session->changes().peerUpdated(channel, UpdateFlag::StickersSet);
		}
	}
	channel->fullUpdated();

	if (canViewAdmins != channel->canViewAdmins()
		|| canViewMembers != channel->canViewMembers()) {
		session->changes().peerUpdated(channel, UpdateFlag::Rights);
	}

	session->api().applyNotifySettings(
		MTP_inputNotifyPeer(channel->input),
		update.vnotify_settings());

	// For clearUpTill() call.
	channel->owner().sendHistoryChangeNotifications();
}

void ApplyMegagroupAdmins(
		not_null<ChannelData*> channel,
		const MTPDchannels_channelParticipants &data) {
	Expects(channel->isMegagroup());

	channel->owner().processUsers(data.vusers());

	const auto &list = data.vparticipants().v;
	const auto i = ranges::find(
		list,
		mtpc_channelParticipantCreator,
		&MTPChannelParticipant::type);
	if (i != list.end()) {
		const auto &data = i->c_channelParticipantCreator();
		const auto userId = data.vuser_id().v;
		channel->mgInfo->creator = channel->owner().userLoaded(userId);
		channel->mgInfo->creatorRank = qs(data.vrank().value_or_empty());
	} else {
		channel->mgInfo->creator = nullptr;
		channel->mgInfo->creatorRank = QString();
	}

	auto adding = base::flat_map<UserId, QString>();
	auto admins = ranges::make_subrange(
		list.begin(), list.end()
	) | ranges::view::transform([](const MTPChannelParticipant &p) {
		const auto userId = p.match([](const auto &data) {
			return data.vuser_id().v;
		});
		const auto rank = p.match([](const MTPDchannelParticipantAdmin &data) {
			return qs(data.vrank().value_or_empty());
		}, [](const MTPDchannelParticipantCreator &data) {
			return qs(data.vrank().value_or_empty());
		}, [](const auto &data) {
			return QString();
		});
		return std::make_pair(userId, rank);
	});
	for (const auto &[userId, rank] : admins) {
		adding.emplace(userId, rank);
	}
	if (channel->mgInfo->creator) {
		adding.emplace(
			peerToUser(channel->mgInfo->creator->id),
			channel->mgInfo->creatorRank);
	}
	auto removing = channel->mgInfo->admins;
	if (removing.empty() && adding.empty()) {
		// Add some admin-placeholder so we don't DDOS
		// server with admins list requests.
		LOG(("API Error: Got empty admins list from server."));
		adding.emplace(0, QString());
	}

	Data::ChannelAdminChanges changes(channel);
	for (const auto &[addingId, rank] : adding) {
		if (!removing.remove(addingId)) {
			changes.add(addingId, rank);
		}
	}
	for (const auto &[removingId, rank] : removing) {
		changes.remove(removingId);
	}
}

} // namespace Data
