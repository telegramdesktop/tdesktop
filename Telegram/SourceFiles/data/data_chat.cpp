/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat.h"

#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/history.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "observer_peer.h"

namespace {

using UpdateFlag = Notify::PeerUpdate::Flag;

} // namespace

ChatData::ChatData(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id)
, inputChat(MTP_int(bareId())) {
}

void ChatData::setPhoto(const MTPChatPhoto &photo) {
	setPhoto(userpicPhotoId(), photo);
}

void ChatData::setPhoto(PhotoId photoId, const MTPChatPhoto &photo) {
	if (photo.type() == mtpc_chatPhoto) {
		const auto &data = photo.c_chatPhoto();
		updateUserpic(photoId, data.vphoto_small);
	} else {
		clearUserpic();
	}
}

auto ChatData::DefaultAdminRights() -> AdminRights {
	using Flag = AdminRight;
	return Flag::f_change_info
		| Flag::f_delete_messages
		| Flag::f_ban_users
		| Flag::f_invite_users
		| Flag::f_pin_messages;
}

bool ChatData::canWrite() const {
	// Duplicated in Data::CanWriteValue().
	return amIn() && !amRestricted(Restriction::f_send_messages);
}

bool ChatData::canEditInformation() const {
	return amIn() && !amRestricted(Restriction::f_change_info);
}

bool ChatData::canEditPermissions() const {
	return amIn()
		&& (amCreator() || (adminRights() & AdminRight::f_ban_users));
}

bool ChatData::canEditUsername() const {
	return amCreator()
		&& (fullFlags() & MTPDchatFull::Flag::f_can_set_username);
}

bool ChatData::canEditPreHistoryHidden() const {
	return amCreator();
}

bool ChatData::canAddMembers() const {
	return amIn() && !amRestricted(Restriction::f_invite_users);
}

bool ChatData::canSendPolls() const {
	return amIn() && !amRestricted(Restriction::f_send_polls);
}

bool ChatData::canAddAdmins() const {
	return amIn() && amCreator();
}

bool ChatData::canBanMembers() const {
	return amCreator()
		|| (adminRights() & AdminRight::f_ban_users);
}

bool ChatData::anyoneCanAddMembers() const {
	return !(defaultRestrictions() & Restriction::f_invite_users);
}

void ChatData::setName(const QString &newName) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), QString());
}

void ChatData::applyEditAdmin(not_null<UserData*> user, bool isAdmin) {
	auto flags = Notify::PeerUpdate::Flag::AdminsChanged
		| Notify::PeerUpdate::Flag::None;
	if (isAdmin) {
		admins.emplace(user);
	} else {
		admins.remove(user);
	}
	Notify::peerUpdatedDelayed(
		this,
		Notify::PeerUpdate::Flag::AdminsChanged);
}

void ChatData::invalidateParticipants() {
	participants.clear();
	admins.clear();
	setAdminRights(MTP_chatAdminRights(MTP_flags(0)));
	//setDefaultRestrictions(MTP_chatBannedRights(MTP_flags(0), MTP_int(0)));
	invitedByMe.clear();
	botStatus = 0;
	Notify::peerUpdatedDelayed(
		this,
		UpdateFlag::MembersChanged | UpdateFlag::AdminsChanged);
}

void ChatData::setInviteLink(const QString &newInviteLink) {
	if (newInviteLink != _inviteLink) {
		_inviteLink = newInviteLink;
		Notify::peerUpdatedDelayed(this, UpdateFlag::InviteLinkChanged);
	}
}

void ChatData::setAdminRights(const MTPChatAdminRights &rights) {
	if (rights.c_chatAdminRights().vflags.v == adminRights()) {
		return;
	}
	_adminRights.set(rights.c_chatAdminRights().vflags.v);
	Notify::peerUpdatedDelayed(
		this,
		(UpdateFlag::RightsChanged
			| UpdateFlag::AdminsChanged
			| UpdateFlag::BannedUsersChanged));
}

void ChatData::setDefaultRestrictions(const MTPChatBannedRights &rights) {
	if (rights.c_chatBannedRights().vflags.v == defaultRestrictions()) {
		return;
	}
	_defaultRestrictions.set(rights.c_chatBannedRights().vflags.v);
	Notify::peerUpdatedDelayed(this, UpdateFlag::RightsChanged);
}

void ChatData::refreshBotStatus() {
	if (participants.empty()) {
		botStatus = 0;
	} else {
		const auto bot = ranges::find_if(participants, &UserData::isBot);
		botStatus = (bot == end(participants)) ? -1 : 2;
	}
}

auto ChatData::applyUpdateVersion(int version) -> UpdateStatus {
	if (_version > version) {
		return UpdateStatus::TooOld;
	} else if (_version + 1 < version) {
		invalidateParticipants();
		session().api().requestPeer(this);
		return UpdateStatus::Skipped;
	}
	setVersion(version);
	return UpdateStatus::Good;
}

ChannelData *ChatData::getMigrateToChannel() const {
	return _migratedTo;
}

void ChatData::setMigrateToChannel(ChannelData *channel) {
	if (_migratedTo != channel) {
		_migratedTo = channel;
		if (channel->amIn()) {
			Notify::peerUpdatedDelayed(this, UpdateFlag::MigrationChanged);
		}
	}
}

namespace Data {

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatParticipants &update) {
	ApplyChatParticipants(chat, update.vparticipants);
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatParticipantAdd &update) {
	if (chat->applyUpdateVersion(update.vversion.v)
		!= ChatData::UpdateStatus::Good) {
		return;
	} else if (chat->count < 0) {
		return;
	}
	const auto user = chat->owner().userLoaded(update.vuser_id.v);
	if (!user
		|| (!chat->participants.empty()
			&& chat->participants.contains(user))) {
		chat->invalidateParticipants();
		++chat->count;
		return;
	}
	if (chat->participants.empty()) {
		if (chat->count > 0) { // If the count is known.
			++chat->count;
		}
		chat->botStatus = 0;
	} else {
		chat->participants.emplace(user);
		if (update.vinviter_id.v == chat->session().userId()) {
			chat->invitedByMe.insert(user);
		} else {
			chat->invitedByMe.remove(user);
		}
		++chat->count;
		if (user->isBot()) {
			chat->botStatus = 2;
			if (!user->botInfo->inited) {
				chat->session().api().requestFullPeer(user);
			}
		}
	}
	Notify::peerUpdatedDelayed(
		chat,
		Notify::PeerUpdate::Flag::MembersChanged);
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatParticipantDelete &update) {
	if (chat->applyUpdateVersion(update.vversion.v)
		!= ChatData::UpdateStatus::Good) {
		return;
	} else if (chat->count <= 0) {
		return;
	}
	const auto user = chat->owner().userLoaded(update.vuser_id.v);
	if (!user
		|| (!chat->participants.empty()
			&& !chat->participants.contains(user))) {
		chat->invalidateParticipants();
		--chat->count;
		return;
	}
	if (chat->participants.empty()) {
		if (chat->count > 0) {
			chat->count--;
		}
		chat->botStatus = 0;
	} else {
		chat->participants.erase(user);
		chat->count--;
		chat->invitedByMe.remove(user);
		chat->admins.remove(user);
		if (user->isSelf()) {
			chat->setAdminRights(MTP_chatAdminRights(MTP_flags(0)));
		}
		if (const auto history = chat->owner().historyLoaded(chat)) {
			if (history->lastKeyboardFrom == user->id) {
				history->clearLastKeyboard();
			}
		}
		if (chat->botStatus > 0 && user->botInfo) {
			chat->refreshBotStatus();
		}
	}
	Notify::peerUpdatedDelayed(
		chat,
		Notify::PeerUpdate::Flag::MembersChanged);
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatParticipantAdmin &update) {
	if (chat->applyUpdateVersion(update.vversion.v)
		!= ChatData::UpdateStatus::Good) {
		return;
	}

	const auto user = chat->owner().userLoaded(update.vuser_id.v);
	if (!user) {
		chat->invalidateParticipants();
		return;
	}
	if (user->isSelf()) {
		chat->setAdminRights(MTP_chatAdminRights(mtpIsTrue(update.vis_admin)
			? MTP_flags(ChatData::DefaultAdminRights())
			: MTP_flags(0)));
	}
	if (mtpIsTrue(update.vis_admin)) {
		if (chat->noParticipantInfo()) {
			chat->session().api().requestFullPeer(chat);
		} else {
			chat->admins.emplace(user);
		}
	} else {
		chat->admins.erase(user);
	}
	Notify::peerUpdatedDelayed(
		chat,
		Notify::PeerUpdate::Flag::AdminsChanged);
}

void ApplyChatUpdate(
		not_null<ChatData*> chat,
		const MTPDupdateChatDefaultBannedRights &update) {
	if (chat->applyUpdateVersion(update.vversion.v)
		!= ChatData::UpdateStatus::Good) {
		return;
	}
	chat->setDefaultRestrictions(update.vdefault_banned_rights);
}

void ApplyChatParticipants(
		not_null<ChatData*> chat,
		const MTPChatParticipants &participants) {
	participants.match([&](const MTPDchatParticipantsForbidden &data) {
		if (data.has_self_participant()) {
			// data.vself_participant.
		}
		chat->count = -1;
		chat->invalidateParticipants();
	}, [&](const MTPDchatParticipants &data) {
		const auto status = chat->applyUpdateVersion(data.vversion.v);
		if (status == ChatData::UpdateStatus::TooOld) {
			return;
		}
		// Even if we skipped some updates, we got current participants
		// and we've requested peer from API to have current rights.
		chat->setVersion(data.vversion.v);

		const auto &list = data.vparticipants.v;
		chat->count = list.size();
		chat->participants.clear();
		chat->invitedByMe.clear();
		chat->admins.clear();
		chat->setAdminRights(MTP_chatAdminRights(MTP_flags(0)));
		const auto selfUserId = chat->session().userId();
		for (const auto &participant : list) {
			const auto userId = participant.match([&](const auto &data) {
				return data.vuser_id.v;
			});
			const auto user = chat->owner().userLoaded(userId);
			if (!user) {
				chat->invalidateParticipants();
				break;
			}

			chat->participants.emplace(user);

			const auto inviterId = participant.match([&](
					const MTPDchatParticipantCreator &data) {
				return 0;
			}, [&](const auto &data) {
				return data.vinviter_id.v;
			});
			if (inviterId == selfUserId) {
				chat->invitedByMe.insert(user);
			}

			participant.match([&](const MTPDchatParticipantCreator &data) {
				chat->creator = userId;
			}, [&](const MTPDchatParticipantAdmin &data) {
				chat->admins.emplace(user);
				if (user->isSelf()) {
					chat->setAdminRights(MTP_chatAdminRights(
						MTP_flags(ChatData::DefaultAdminRights())));
				}
			}, [](const MTPDchatParticipant &) {
			});
		}
		if (chat->participants.empty()) {
			return;
		}
		if (const auto history = chat->owner().historyLoaded(chat)) {
			if (history->lastKeyboardFrom) {
				const auto i = ranges::find(
					chat->participants,
					history->lastKeyboardFrom,
					&UserData::id);
				if (i == end(chat->participants)) {
					history->clearLastKeyboard();
				}
			}
		}
		chat->refreshBotStatus();
		Notify::peerUpdatedDelayed(
			chat,
			Notify::PeerUpdate::Flag::MembersChanged
			| Notify::PeerUpdate::Flag::AdminsChanged);
	});
}

} // namespace Data
