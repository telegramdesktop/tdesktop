/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer.h"
#include "data/data_pts_waiter.h"

struct MegagroupInfo {
	struct Admin {
		explicit Admin(MTPChatAdminRights rights)
		: rights(rights) {
		}
		Admin(MTPChatAdminRights rights, bool canEdit)
		: rights(rights)
		, canEdit(canEdit) {
		}
		MTPChatAdminRights rights;
		bool canEdit = false;
	};

	struct Restricted {
		explicit Restricted(MTPChatBannedRights rights)
		: rights(rights) {
		}
		MTPChatBannedRights rights;
	};

	std::deque<not_null<UserData*>> lastParticipants;
	base::flat_map<not_null<UserData*>, Admin> lastAdmins;
	base::flat_map<not_null<UserData*>, Restricted> lastRestricted;
	base::flat_set<not_null<PeerData*>> markupSenders;
	base::flat_set<not_null<UserData*>> bots;

	// For admin badges, full admins list.
	base::flat_set<UserId> admins;

	UserData *creator = nullptr; // nullptr means unknown
	int botStatus = 0; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other
	bool joinedMessageFound = false;
	MTPInputStickerSet stickerSet = MTP_inputStickerSetEmpty();

	enum LastParticipantsStatus {
		LastParticipantsUpToDate       = 0x00,
		LastParticipantsCountOutdated  = 0x02,
	};
	mutable int lastParticipantsStatus = LastParticipantsUpToDate;
	int lastParticipantsCount = 0;

	ChatData *migrateFromPtr = nullptr;

};

class ChannelData : public PeerData {
public:
	static constexpr auto kEssentialFlags = 0
		| MTPDchannel::Flag::f_creator
		| MTPDchannel::Flag::f_left
		| MTPDchannel::Flag::f_broadcast
		| MTPDchannel::Flag::f_verified
		| MTPDchannel::Flag::f_megagroup
		| MTPDchannel::Flag::f_restricted
		| MTPDchannel::Flag::f_signatures
		| MTPDchannel::Flag::f_username;
	using Flags = Data::Flags<
		MTPDchannel::Flags,
		kEssentialFlags>;

	static constexpr auto kEssentialFullFlags = 0
		| MTPDchannelFull::Flag::f_can_view_participants
		| MTPDchannelFull::Flag::f_can_set_username
		| MTPDchannelFull::Flag::f_can_set_stickers;
	using FullFlags = Data::Flags<
		MTPDchannelFull::Flags,
		kEssentialFullFlags>;

	using AdminRight = ChatAdminRight;
	using Restriction = ChatRestriction;
	using AdminRights = ChatAdminRights;
	using Restrictions = ChatRestrictions;
	using AdminRightFlags = Data::Flags<AdminRights>;
	using RestrictionFlags = Data::Flags<Restrictions>;

	ChannelData(not_null<Data::Session*> owner, PeerId id);

	void setPhoto(const MTPChatPhoto &photo);
	void setPhoto(PhotoId photoId, const MTPChatPhoto &photo);

	void setName(const QString &name, const QString &username);

	void setFlags(MTPDchannel::Flags which) {
		_flags.set(which);
	}
	void addFlags(MTPDchannel::Flags which) {
		_flags.add(which);
	}
	void removeFlags(MTPDchannel::Flags which) {
		_flags.remove(which);
	}
	auto flags() const {
		return _flags.current();
	}
	auto flagsValue() const {
		return _flags.value();
	}

	void setFullFlags(MTPDchannelFull::Flags which) {
		_fullFlags.set(which);
	}
	void addFullFlags(MTPDchannelFull::Flags which) {
		_fullFlags.add(which);
	}
	void removeFullFlags(MTPDchannelFull::Flags which) {
		_fullFlags.remove(which);
	}
	auto fullFlags() const {
		return _fullFlags.current();
	}
	auto fullFlagsValue() const {
		return _fullFlags.value();
	}

	// Returns true if about text was changed.
	bool setAbout(const QString &newAbout);
	const QString &about() const {
		return _about;
	}

	int membersCount() const {
		return _membersCount;
	}
	void setMembersCount(int newMembersCount);

	int adminsCount() const {
		return _adminsCount;
	}
	void setAdminsCount(int newAdminsCount);

	int restrictedCount() const {
		return _restrictedCount;
	}
	void setRestrictedCount(int newRestrictedCount);

	int kickedCount() const {
		return _kickedCount;
	}
	void setKickedCount(int newKickedCount);

	bool haveLeft() const {
		return flags() & MTPDchannel::Flag::f_left;
	}
	bool amIn() const {
		return !isForbidden() && !haveLeft();
	}
	bool addsSignature() const {
		return flags() & MTPDchannel::Flag::f_signatures;
	}
	bool isForbidden() const {
		return flags() & MTPDchannel_ClientFlag::f_forbidden;
	}
	bool isVerified() const {
		return flags() & MTPDchannel::Flag::f_verified;
	}

	static MTPChatBannedRights KickedRestrictedRights();
	static constexpr auto kRestrictUntilForever = TimeId(INT_MAX);
	static bool IsRestrictedForever(TimeId until) {
		return !until || (until == kRestrictUntilForever);
	}
	void applyEditAdmin(
		not_null<UserData*> user,
		const MTPChatAdminRights &oldRights,
		const MTPChatAdminRights &newRights);
	void applyEditBanned(
		not_null<UserData*> user,
		const MTPChatBannedRights &oldRights,
		const MTPChatBannedRights &newRights);

	bool isGroupAdmin(not_null<UserData*> user) const;

	bool lastParticipantsCountOutdated() const {
		if (!mgInfo
			|| !(mgInfo->lastParticipantsStatus
				& MegagroupInfo::LastParticipantsCountOutdated)) {
			return false;
		}
		if (mgInfo->lastParticipantsCount == membersCount()) {
			mgInfo->lastParticipantsStatus
				&= ~MegagroupInfo::LastParticipantsCountOutdated;
			return false;
		}
		return true;
	}
	bool isMegagroup() const {
		return flags() & MTPDchannel::Flag::f_megagroup;
	}
	bool isBroadcast() const {
		return flags() & MTPDchannel::Flag::f_broadcast;
	}
	bool isPublic() const {
		return flags() & MTPDchannel::Flag::f_username;
	}
	bool amCreator() const {
		return flags() & MTPDchannel::Flag::f_creator;
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

	auto restrictions() const {
		return _restrictions.current();
	}
	auto restrictionsValue() const {
		return _restrictions.value();
	}
	TimeId restrictedUntil() const {
		return _restrictedUntil;
	}
	void setRestrictions(const MTPChatBannedRights &rights);
	bool hasRestrictions() const {
		return (restrictions() != 0);
	}
	bool hasRestrictions(TimeId now) const {
		return hasRestrictions()
			&& (restrictedUntil() > now);
	}

	auto defaultRestrictions() const {
		return _defaultRestrictions.current();
	}
	auto defaultRestrictionsValue() const {
		return _defaultRestrictions.value();
	}
	void setDefaultRestrictions(const MTPChatBannedRights &rights);

	// Like in ChatData.
	bool canWrite() const;
	bool canEditInformation() const;
	bool canEditPermissions() const;
	bool canAddMembers() const;

	bool canBanMembers() const;
	bool canEditMessages() const;
	bool canDeleteMessages() const;
	bool anyoneCanAddMembers() const;
	bool hiddenPreHistory() const;
	bool canAddAdmins() const;
	bool canPublish() const;
	bool canViewMembers() const;
	bool canViewAdmins() const;
	bool canViewBanned() const;
	bool canEditSignatures() const;
	bool canEditPreHistoryHidden() const;
	bool canEditUsername() const;
	bool canEditStickers() const;
	bool canDelete() const;
	bool canEditAdmin(not_null<UserData*> user) const;
	bool canRestrictUser(not_null<UserData*> user) const;

	void setInviteLink(const QString &newInviteLink);
	QString inviteLink() const;
	bool canHaveInviteLink() const;

	void ptsInit(int32 pts) {
		_ptsWaiter.init(pts);
	}
	void ptsReceived(int32 pts) {
		_ptsWaiter.updateAndApply(this, pts, 0);
	}
	bool ptsUpdateAndApply(int32 pts, int32 count) {
		return _ptsWaiter.updateAndApply(this, pts, count);
	}
	bool ptsUpdateAndApply(
		int32 pts,
		int32 count,
		const MTPUpdate &update) {
		return _ptsWaiter.updateAndApply(this, pts, count, update);
	}
	bool ptsUpdateAndApply(
		int32 pts,
		int32 count,
		const MTPUpdates &updates) {
		return _ptsWaiter.updateAndApply(this, pts, count, updates);
	}
	int32 pts() const {
		return _ptsWaiter.current();
	}
	bool ptsInited() const {
		return _ptsWaiter.inited();
	}
	bool ptsRequesting() const {
		return _ptsWaiter.requesting();
	}
	void ptsSetRequesting(bool isRequesting) {
		return _ptsWaiter.setRequesting(isRequesting);
	}
	void ptsWaitingForShortPoll(int32 ms) { // < 0 - not waiting
		return _ptsWaiter.setWaitingForShortPoll(this, ms);
	}
	bool ptsWaitingForSkipped() const {
		return _ptsWaiter.waitingForSkipped();
	}
	bool ptsWaitingForShortPoll() const {
		return _ptsWaiter.waitingForShortPoll();
	}

	QString unavailableReason() const override;
	void setUnavailableReason(const QString &reason);

	MsgId availableMinId() const {
		return _availableMinId;
	}
	void setAvailableMinId(MsgId availableMinId);

	void setFeed(not_null<Data::Feed*> feed);
	void clearFeed();

	Data::Feed *feed() const {
		return _feed;
	}

	// Still public data members.
	uint64 access = 0;

	MTPinputChannel inputChannel;

	QString username;

	int32 date = 0;
	int version = 0;
	std::unique_ptr<MegagroupInfo> mgInfo;

	UserId inviter = 0; // > 0 - user who invited me to channel, < 0 - not in channel
	TimeId inviteDate = 0;

private:
	void flagsUpdated(MTPDchannel::Flags diff);
	void fullFlagsUpdated(MTPDchannelFull::Flags diff);

	bool canEditLastAdmin(not_null<UserData*> user) const;
	void setFeedPointer(Data::Feed *feed);

	Flags _flags = Flags(MTPDchannel_ClientFlag::f_forbidden | 0);
	FullFlags _fullFlags;

	PtsWaiter _ptsWaiter;

	int _membersCount = 1;
	int _adminsCount = 1;
	int _restrictedCount = 0;
	int _kickedCount = 0;
	MsgId _availableMinId = 0;

	RestrictionFlags _defaultRestrictions;
	AdminRightFlags _adminRights;
	RestrictionFlags _restrictions;
	TimeId _restrictedUntil;

	QString _unavailableReason;
	QString _about;

	QString _inviteLink;
	Data::Feed *_feed = nullptr;

	rpl::lifetime _lifetime;

};
