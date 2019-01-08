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

	static constexpr auto kEssentialFullFlags = 0
		| MTPDchatFull::Flag::f_can_set_username;
	using FullFlags = Data::Flags<
		MTPDchatFull::Flags,
		kEssentialFullFlags>;

	using AdminRight = ChatAdminRight;
	using Restriction = ChatRestriction;
	using AdminRights = ChatAdminRights;
	using Restrictions = ChatRestrictions;
	using AdminRightFlags = Data::Flags<AdminRights>;
	using RestrictionFlags = Data::Flags<Restrictions>;

	ChatData(not_null<Data::Session*> owner, PeerId id);

	void setPhoto(const MTPChatPhoto &photo);
	void setPhoto(PhotoId photoId, const MTPChatPhoto &photo);

	void setName(const QString &newName);

	void invalidateParticipants();
	bool noParticipantInfo() const {
		return (count > 0 || amIn()) && participants.empty();
	}

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

	void setFullFlags(MTPDchatFull::Flags which) {
		_fullFlags.set(which);
	}
	void addFullFlags(MTPDchatFull::Flags which) {
		_fullFlags.add(which);
	}
	void removeFullFlags(MTPDchatFull::Flags which) {
		_fullFlags.remove(which);
	}
	auto fullFlags() const {
		return _fullFlags.current();
	}
	auto fullFlagsValue() const {
		return _fullFlags.value();
	}

	auto adminRights() const {
		return _adminRights.current();
	}
	auto adminRightsValue() const {
		return _adminRights.value();
	}
	void setAdminRights(const MTPChatAdminRights &rights);
	bool hasAdminRights() const {
		return (adminRights() != 0);
	}

	auto defaultRestrictions() const {
		return _defaultRestrictions.current();
	}
	auto defaultRestrictionsValue() const {
		return _defaultRestrictions.value();
	}
	void setDefaultRestrictions(const MTPChatBannedRights &rights);

	bool isForbidden() const {
		return flags() & MTPDchat_ClientFlag::f_forbidden;
	}
	bool amIn() const {
		return !isForbidden() && !haveLeft() && !wasKicked();
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

	// Like in ChatData.
	bool canWrite() const;
	bool canEditInformation() const;
	bool canEditPermissions() const;
	bool canEditUsername() const;
	bool canEditPreHistoryHidden() const;
	bool canAddMembers() const;

	void setInviteLink(const QString &newInviteLink);
	QString inviteLink() const {
		return _inviteLink;
	}

	// Still public data members.
	MTPint inputChat;

	ChannelData *migrateToPtr = nullptr;

	int count = 0;
	TimeId date = 0;
	int version = 0;
	UserId creator = 0;

	base::flat_map<not_null<UserData*>, int> participants;
	base::flat_set<not_null<UserData*>> invitedByMe;
	base::flat_set<not_null<UserData*>> admins;
	std::deque<not_null<UserData*>> lastAuthors;
	base::flat_set<not_null<PeerData*>> markupSenders;
	int botStatus = 0; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other
//	ImagePtr photoFull;

private:
	[[nodiscard]] bool actionsUnavailable() const;

	Flags _flags;
	FullFlags _fullFlags;
	QString _inviteLink;

	RestrictionFlags _defaultRestrictions;
	AdminRightFlags _adminRights;

};
