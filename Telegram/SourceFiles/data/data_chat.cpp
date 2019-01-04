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

bool ChatData::canEditInformation() const {
	// #TODO groups
	return !isDeactivated()
		/*&& ((adminRights() & AdminRight::f_change_info) || amCreator())*/;
}

void ChatData::setName(const QString &newName) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), QString());
}

void ChatData::invalidateParticipants() {
	// #TODO groups
	auto wasCanEdit = canEditInformation();
	participants.clear();
	admins.clear();
	//removeFlags(MTPDchat::Flag::f_admin);
	invitedByMe.clear();
	botStatus = 0;
	if (wasCanEdit != canEditInformation()) {
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::ChatCanEdit);
	}
	Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::MembersChanged | Notify::PeerUpdate::Flag::AdminsChanged);
}

void ChatData::setInviteLink(const QString &newInviteLink) {
	if (newInviteLink != _inviteLink) {
		_inviteLink = newInviteLink;
		Notify::peerUpdatedDelayed(this, UpdateFlag::InviteLinkChanged);
	}
}
