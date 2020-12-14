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
		| MTPDchat::Flag::f_call_not_empty
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
		return !isForbidden()
			&& !isDeactivated()
			&& !haveLeft()
			&& !wasKicked();
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

	static AdminRights DefaultAdminRights();

	// Like in ChannelData.
	bool canWrite() const;
	bool canEditInformation() const;
	bool canEditPermissions() const;
	bool canEditUsername() const;
	bool canEditPreHistoryHidden() const;
	bool canAddMembers() const;
	bool canAddAdmins() const;
	bool canBanMembers() const;
	bool canSendPolls() const;
	bool anyoneCanAddMembers() const;

	void applyEditAdmin(not_null<UserData*> user, bool isAdmin);

	void setInviteLink(const QString &newInviteLink);
	[[nodiscard]] QString inviteLink() const {
		return _inviteLink;
	}
	[[nodiscard]] bool canHaveInviteLink() const;
	void refreshBotStatus();

	enum class UpdateStatus {
		Good,
		TooOld,
		Skipped,
	};
	int version() const {
		return _version;
	}
	void setVersion(int version) {
		_version = version;
	}
	UpdateStatus applyUpdateVersion(int version);

	ChannelData *getMigrateToChannel() const;
	void setMigrateToChannel(ChannelData *channel);

	[[nodiscard]] Data::GroupCall *groupCall() const {
		return _call.get();
	}
	void setGroupCall(const MTPInputGroupCall &call);
	void clearGroupCall();

	// Still public data members.
	const MTPint inputChat;

	int count = 0;
	TimeId date = 0;
	UserId creator = 0;

	base::flat_set<not_null<UserData*>> participants;
	base::flat_set<not_null<UserData*>> invitedByMe;
	base::flat_set<not_null<UserData*>> admins;
	std::deque<not_null<UserData*>> lastAuthors;
	base::flat_set<not_null<PeerData*>> markupSenders;
	int botStatus = 0; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other

private:
	Flags _flags;
	FullFlags _fullFlags;
	QString _inviteLink;

	RestrictionFlags _defaultRestrictions;
	AdminRightFlags _adminRights;
	int _version = 0;

	std::unique_ptr<Data::GroupCall> _call;

	ChannelData *_migratedTo = nullptr;
	rpl::lifetime _lifetime;

};

namespace Data {

void ApplyChatUpdate(
	not_null<ChatData*> chat,
	const MTPDupdateChatParticipants &update);
void ApplyChatUpdate(
	not_null<ChatData*> chat,
	const MTPDupdateChatParticipantAdd &update);
void ApplyChatUpdate(
	not_null<ChatData*> chat,
	const MTPDupdateChatParticipantDelete &update);
void ApplyChatUpdate(
	not_null<ChatData*> chat,
	const MTPDupdateChatParticipantAdmin &update);
void ApplyChatUpdate(
	not_null<ChatData*> chat,
	const MTPDupdateChatDefaultBannedRights &update);
void ApplyChatUpdate(
	not_null<ChatData*> chat,
	const MTPDchatFull &update);
void ApplyChatUpdate(
	not_null<ChatData*> chat,
	const MTPChatParticipants &update);

} // namespace Data
