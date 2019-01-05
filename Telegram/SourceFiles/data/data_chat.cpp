/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat.h"

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

bool ChatData::actionsUnavailable() const {
	return isDeactivated() || !amIn();
}

bool ChatData::canWrite() const {
	// Duplicated in Data::CanWriteValue().
	return !actionsUnavailable()
		&& !amRestricted(ChatRestriction::f_send_messages);
}

bool ChatData::canEditInformation() const {
	return !actionsUnavailable()
		&& !amRestricted(ChatRestriction::f_change_info);
}

bool ChatData::canAddMembers() const {
	return !actionsUnavailable()
		&& !amRestricted(ChatRestriction::f_invite_users);
}

void ChatData::setName(const QString &newName) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), QString());
}

void ChatData::invalidateParticipants() {
	// #TODO groups
	participants.clear();
	admins.clear();
	//removeFlags(MTPDchat::Flag::f_admin);
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
