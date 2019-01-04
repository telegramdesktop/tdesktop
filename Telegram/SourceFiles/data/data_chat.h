/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer.h"

class ChatData : public PeerData {
public:
	static constexpr auto kEssentialFlags = 0
		| MTPDchat::Flag::f_creator
		| MTPDchat::Flag::f_kicked
		| MTPDchat::Flag::f_left
		| MTPDchat::Flag::f_deactivated
		| MTPDchat::Flag::f_migrated_to
		| MTPDchat::Flag::f_admin_rights
		| MTPDchat::Flag::f_default_banned_rights;
	using Flags = Data::Flags<
		MTPDchat::Flags,
		kEssentialFlags>;

	ChatData(not_null<Data::Session*> owner, PeerId id);

	void setPhoto(const MTPChatPhoto &photo);
	void setPhoto(PhotoId photoId, const MTPChatPhoto &photo);

	void setName(const QString &newName);

	void invalidateParticipants();
	bool noParticipantInfo() const {
		return (count > 0 || amIn()) && participants.empty();
	}

	MTPint inputChat;

	ChannelData *migrateToPtr = nullptr;

	int count = 0;
	TimeId date = 0;
	int version = 0;
	UserId creator = 0;

	void setFlags(MTPDchat::Flags which) {
		_flags.set(which);
	}
	void addFlags(MTPDchat::Flags which) {
		_flags.add(which);
	}
	void removeFlags(MTPDchat::Flags which) {
		_flags.remove(which);
	}
	auto flags() const {
		return _flags.current();
	}
	auto flagsValue() const {
		return _flags.value();
	}

	bool isForbidden() const {
		return flags() & MTPDchat_ClientFlag::f_forbidden;
	}
	bool amIn() const {
		return !isForbidden() && !haveLeft() && !wasKicked();
	}
	bool canEditInformation() const;
	bool canWrite() const {
		// Duplicated in Data::CanWriteValue().
		return !isDeactivated() && amIn();
	}
	bool haveLeft() const {
		return flags() & MTPDchat::Flag::f_left;
	}
	bool wasKicked() const {
		return flags() & MTPDchat::Flag::f_kicked;
	}
	bool amCreator() const {
		return flags() & MTPDchat::Flag::f_creator;
	}
	bool isDeactivated() const {
		return flags() & MTPDchat::Flag::f_deactivated;
	}
	bool isMigrated() const {
		return flags() & MTPDchat::Flag::f_migrated_to;
	}
	base::flat_map<not_null<UserData*>, int> participants;
	base::flat_set<not_null<UserData*>> invitedByMe;
	base::flat_set<not_null<UserData*>> admins;
	std::deque<not_null<UserData*>> lastAuthors;
	base::flat_set<not_null<PeerData*>> markupSenders;
	int botStatus = 0; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other
//	ImagePtr photoFull;

	void setInviteLink(const QString &newInviteLink);
	QString inviteLink() const {
		return _inviteLink;
	}

private:
	void flagsUpdated(MTPDchat::Flags diff);

	Flags _flags;
	QString _inviteLink;

};
