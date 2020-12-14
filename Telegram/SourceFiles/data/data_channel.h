/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer.h"
#include "data/data_pts_waiter.h"
#include "data/data_location.h"

struct ChannelLocation {
	QString address;
	Data::LocationPoint point;

	friend inline bool operator==(
			const ChannelLocation &a,
			const ChannelLocation &b) {
		return a.address.isEmpty()
			? b.address.isEmpty()
			: (a.address == b.address && a.point == b.point);
	}
	friend inline bool operator!=(
			const ChannelLocation &a,
			const ChannelLocation &b) {
		return !(a == b);
	}
};

class MegagroupInfo {
public:
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

	ChatData *getMigrateFromChat() const;
	void setMigrateFromChat(ChatData *chat);

	const ChannelLocation *getLocation() const;
	void setLocation(const ChannelLocation &location);

	std::deque<not_null<UserData*>> lastParticipants;
	base::flat_map<not_null<UserData*>, Admin> lastAdmins;
	base::flat_map<not_null<UserData*>, Restricted> lastRestricted;
	base::flat_set<not_null<PeerData*>> markupSenders;
	base::flat_set<not_null<UserData*>> bots;

	// For admin badges, full admins list with ranks.
	base::flat_map<UserId, QString> admins;

	UserData *creator = nullptr; // nullptr means unknown
	QString creatorRank;
	int botStatus = 0; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other
	bool joinedMessageFound = false;
	MTPInputStickerSet stickerSet = MTP_inputStickerSetEmpty();

	enum LastParticipantsStatus {
		LastParticipantsUpToDate       = 0x00,
		LastParticipantsOnceReceived   = 0x01,
		LastParticipantsCountOutdated  = 0x02,
	};
	mutable int lastParticipantsStatus = LastParticipantsUpToDate;
	int lastParticipantsCount = 0;

private:
	ChatData *_migratedFrom = nullptr;
	ChannelLocation _location;

};

class ChannelData : public PeerData {
public:
	static constexpr auto kEssentialFlags = 0
		| MTPDchannel::Flag::f_creator
		| MTPDchannel::Flag::f_left
		| MTPDchannel_ClientFlag::f_forbidden
		| MTPDchannel::Flag::f_broadcast
		| MTPDchannel::Flag::f_verified
		| MTPDchannel::Flag::f_scam
		| MTPDchannel::Flag::f_megagroup
		| MTPDchannel::Flag::f_restricted
		| MTPDchannel::Flag::f_signatures
		| MTPDchannel::Flag::f_username
		| MTPDchannel::Flag::f_call_not_empty
		| MTPDchannel::Flag::f_slowmode_enabled;
	using Flags = Data::Flags<
		MTPDchannel::Flags,
		kEssentialFlags>;

	static constexpr auto kEssentialFullFlags = 0
		| MTPDchannelFull::Flag::f_can_view_participants
		| MTPDchannelFull::Flag::f_can_set_username
		| MTPDchannelFull::Flag::f_can_set_stickers
		| MTPDchannelFull::Flag::f_location
		| MTPDchannelFull::Flag::f_slowmode_seconds
		| MTPDchannelFull::Flag::f_slowmode_next_send_date;
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
	void setAccessHash(uint64 accessHash);

	void setFlags(MTPDchannel::Flags which) {
		_flags.set(which);
	}
	void addFlags(MTPDchannel::Flags which) {
		_flags.add(which);
	}
	void removeFlags(MTPDchannel::Flags which) {
		_flags.remove(which);
	}
	[[nodiscard]] auto flags() const {
		return _flags.current();
	}
	[[nodiscard]] auto flagsValue() const {
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
	[[nodiscard]] auto fullFlags() const {
		return _fullFlags.current();
	}
	[[nodiscard]] auto fullFlagsValue() const {
		return _fullFlags.value();
	}

	[[nodiscard]] int membersCount() const {
		return std::max(_membersCount, 1);
	}
	void setMembersCount(int newMembersCount);
	[[nodiscard]] bool membersCountKnown() const {
		return (_membersCount >= 0);
	}

	[[nodiscard]] int adminsCount() const {
		return _adminsCount;
	}
	void setAdminsCount(int newAdminsCount);

	[[nodiscard]] int restrictedCount() const {
		return _restrictedCount;
	}
	void setRestrictedCount(int newRestrictedCount);

	[[nodiscard]] int kickedCount() const {
		return _kickedCount;
	}
	void setKickedCount(int newKickedCount);

	[[nodiscard]] bool haveLeft() const {
		return flags() & MTPDchannel::Flag::f_left;
	}
	[[nodiscard]] bool amIn() const {
		return !isForbidden() && !haveLeft();
	}
	[[nodiscard]] bool addsSignature() const {
		return flags() & MTPDchannel::Flag::f_signatures;
	}
	[[nodiscard]] bool isForbidden() const {
		return flags() & MTPDchannel_ClientFlag::f_forbidden;
	}
	[[nodiscard]] bool isVerified() const {
		return flags() & MTPDchannel::Flag::f_verified;
	}
	[[nodiscard]] bool isScam() const {
		return flags() & MTPDchannel::Flag::f_scam;
	}

	static MTPChatBannedRights KickedRestrictedRights();
	static constexpr auto kRestrictUntilForever = TimeId(INT_MAX);
	[[nodiscard]] static bool IsRestrictedForever(TimeId until) {
		return !until || (until == kRestrictUntilForever);
	}
	void applyEditAdmin(
		not_null<UserData*> user,
		const MTPChatAdminRights &oldRights,
		const MTPChatAdminRights &newRights,
		const QString &rank);
	void applyEditBanned(
		not_null<UserData*> user,
		const MTPChatBannedRights &oldRights,
		const MTPChatBannedRights &newRights);

	void markForbidden();

	[[nodiscard]] bool isGroupAdmin(not_null<UserData*> user) const;
	[[nodiscard]] bool lastParticipantsRequestNeeded() const;
	[[nodiscard]] bool isMegagroup() const {
		return flags() & MTPDchannel::Flag::f_megagroup;
	}
	[[nodiscard]] bool isBroadcast() const {
		return flags() & MTPDchannel::Flag::f_broadcast;
	}
	[[nodiscard]] bool hasUsername() const {
		return flags() & MTPDchannel::Flag::f_username;
	}
	[[nodiscard]] bool hasLocation() const {
		return fullFlags() & MTPDchannelFull::Flag::f_location;
	}
	[[nodiscard]] bool isPublic() const {
		return hasUsername() || hasLocation();
	}
	[[nodiscard]] bool amCreator() const {
		return flags() & MTPDchannel::Flag::f_creator;
	}

	[[nodiscard]] auto adminRights() const {
		return _adminRights.current();
	}
	[[nodiscard]] auto adminRightsValue() const {
		return _adminRights.value();
	}
	void setAdminRights(const MTPChatAdminRights &rights);
	[[nodiscard]] bool hasAdminRights() const {
		return (adminRights() != 0);
	}

	[[nodiscard]] auto restrictions() const {
		return _restrictions.current();
	}
	[[nodiscard]] auto restrictionsValue() const {
		return _restrictions.value();
	}
	[[nodiscard]] TimeId restrictedUntil() const {
		return _restrictedUntil;
	}
	void setRestrictions(const MTPChatBannedRights &rights);
	[[nodiscard]] bool hasRestrictions() const {
		return (restrictions() != 0);
	}
	[[nodiscard]] bool hasRestrictions(TimeId now) const {
		return hasRestrictions()
			&& (restrictedUntil() > now);
	}

	[[nodiscard]] auto defaultRestrictions() const {
		return _defaultRestrictions.current();
	}
	[[nodiscard]] auto defaultRestrictionsValue() const {
		return _defaultRestrictions.value();
	}
	void setDefaultRestrictions(const MTPChatBannedRights &rights);

	// Like in ChatData.
	[[nodiscard]] bool canWrite() const;
	[[nodiscard]] bool canEditInformation() const;
	[[nodiscard]] bool canEditPermissions() const;
	[[nodiscard]] bool canEditUsername() const;
	[[nodiscard]] bool canEditPreHistoryHidden() const;
	[[nodiscard]] bool canAddMembers() const;
	[[nodiscard]] bool canAddAdmins() const;
	[[nodiscard]] bool canBanMembers() const;
	[[nodiscard]] bool canSendPolls() const;
	[[nodiscard]] bool anyoneCanAddMembers() const;

	[[nodiscard]] bool canEditMessages() const;
	[[nodiscard]] bool canDeleteMessages() const;
	[[nodiscard]] bool hiddenPreHistory() const;
	[[nodiscard]] bool canPublish() const;
	[[nodiscard]] bool canViewMembers() const;
	[[nodiscard]] bool canViewAdmins() const;
	[[nodiscard]] bool canViewBanned() const;
	[[nodiscard]] bool canEditSignatures() const;
	[[nodiscard]] bool canEditStickers() const;
	[[nodiscard]] bool canDelete() const;
	[[nodiscard]] bool canEditAdmin(not_null<UserData*> user) const;
	[[nodiscard]] bool canRestrictUser(not_null<UserData*> user) const;

	void setInviteLink(const QString &newInviteLink);
	[[nodiscard]] QString inviteLink() const {
		return _inviteLink;
	}
	[[nodiscard]] bool canHaveInviteLink() const;

	void setLocation(const MTPChannelLocation &data);
	[[nodiscard]] const ChannelLocation *getLocation() const;

	void setLinkedChat(ChannelData *linked);
	[[nodiscard]] ChannelData *linkedChat() const;
	[[nodiscard]] bool linkedChatKnown() const;

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
	[[nodiscard]] int32 pts() const {
		return _ptsWaiter.current();
	}
	[[nodiscard]] bool ptsInited() const {
		return _ptsWaiter.inited();
	}
	[[nodiscard]] bool ptsRequesting() const {
		return _ptsWaiter.requesting();
	}
	void ptsSetRequesting(bool isRequesting) {
		return _ptsWaiter.setRequesting(isRequesting);
	}
	// < 0 - not waiting
	void ptsWaitingForShortPoll(int32 ms) {
		return _ptsWaiter.setWaitingForShortPoll(this, ms);
	}
	[[nodiscard]] bool ptsWaitingForSkipped() const {
		return _ptsWaiter.waitingForSkipped();
	}
	[[nodiscard]] bool ptsWaitingForShortPoll() const {
		return _ptsWaiter.waitingForShortPoll();
	}

	void setUnavailableReasons(
		std::vector<Data::UnavailableReason> &&reason);

	[[nodiscard]] MsgId availableMinId() const {
		return _availableMinId;
	}
	void setAvailableMinId(MsgId availableMinId);

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

	[[nodiscard]] ChatData *getMigrateFromChat() const;
	void setMigrateFromChat(ChatData *chat);

	[[nodiscard]] int slowmodeSeconds() const;
	void setSlowmodeSeconds(int seconds);
	[[nodiscard]] TimeId slowmodeLastMessage() const;
	void growSlowmodeLastMessage(TimeId when);

	void setInvitePeek(const QString &hash, TimeId expires);
	void clearInvitePeek();
	[[nodiscard]] TimeId invitePeekExpires() const;
	[[nodiscard]] QString invitePeekHash() const;
	void privateErrorReceived();

	[[nodiscard]] Data::GroupCall *groupCall() const {
		return _call.get();
	}
	void migrateCall(std::unique_ptr<Data::GroupCall> call);
	void setGroupCall(const MTPInputGroupCall &call);
	void clearGroupCall();

	// Still public data members.
	uint64 access = 0;

	MTPinputChannel inputChannel = MTP_inputChannelEmpty();

	QString username;

	int32 date = 0;
	std::unique_ptr<MegagroupInfo> mgInfo;

	// > 0 - user who invited me to channel, < 0 - not in channel.
	UserId inviter = 0;
	TimeId inviteDate = 0;

private:
	struct InvitePeek {
		QString hash;
		TimeId expires = 0;
	};

	auto unavailableReasons() const
		-> const std::vector<Data::UnavailableReason> & override;
	bool canEditLastAdmin(not_null<UserData*> user) const;

	Flags _flags = Flags(MTPDchannel_ClientFlag::f_forbidden | 0);
	FullFlags _fullFlags;

	PtsWaiter _ptsWaiter;

	int _membersCount = -1;
	int _adminsCount = 1;
	int _restrictedCount = 0;
	int _kickedCount = 0;
	MsgId _availableMinId = 0;
	int _version = 0;

	RestrictionFlags _defaultRestrictions;
	AdminRightFlags _adminRights;
	RestrictionFlags _restrictions;
	TimeId _restrictedUntil;

	std::vector<Data::UnavailableReason> _unavailableReasons;
	std::unique_ptr<InvitePeek> _invitePeek;
	QString _inviteLink;
	std::optional<ChannelData*> _linkedChat;

	std::unique_ptr<Data::GroupCall> _call;

	int _slowmodeSeconds = 0;
	TimeId _slowmodeLastMessage = 0;

	rpl::lifetime _lifetime;

};

namespace Data {

void ApplyMigration(
	not_null<ChatData*> chat,
	not_null<ChannelData*> channel);

void ApplyChannelUpdate(
	not_null<ChannelData*> channel,
	const MTPDupdateChatDefaultBannedRights &update);

void ApplyChannelUpdate(
	not_null<ChannelData*> channel,
	const MTPDchannelFull &update);

void ApplyMegagroupAdmins(
	not_null<ChannelData*> channel,
	const MTPDchannels_channelParticipants &data);

} // namespace Data
