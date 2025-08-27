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
#include "data/data_chat_participant_status.h"
#include "data/data_peer_bot_commands.h"
#include "data/data_user_names.h"

class ChannelData;

namespace Data {
class Forum;
class SavedMessages;
} // namespace Data

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

enum class ChannelDataFlag : uint64 {
	Left = (1ULL << 0),
	Creator = (1ULL << 1),
	Forbidden = (1ULL << 2),
	CallActive = (1ULL << 3),
	CallNotEmpty = (1ULL << 4),
	Signatures = (1ULL << 5),
	Verified = (1ULL << 6),
	Scam = (1ULL << 7),
	Fake = (1ULL << 8),
	Megagroup = (1ULL << 9),
	Broadcast = (1ULL << 10),
	Gigagroup = (1ULL << 11),
	Username = (1ULL << 12),
	Location = (1ULL << 13),
	CanSetUsername = (1ULL << 14),
	CanSetStickers = (1ULL << 15),
	PreHistoryHidden = (1ULL << 16),
	CanViewParticipants = (1ULL << 17),
	HasLink = (1ULL << 18),
	SlowmodeEnabled = (1ULL << 19),
	NoForwards = (1ULL << 20),
	JoinToWrite = (1ULL << 21),
	RequestToJoin = (1ULL << 22),
	Forum = (1ULL << 23),
	AntiSpam = (1ULL << 24),
	ParticipantsHidden = (1ULL << 25),
	StoriesHidden = (1ULL << 26),
	HasActiveStories = (1ULL << 27),
	HasUnreadStories = (1ULL << 28),
	CanGetStatistics = (1ULL << 29),
	ViewAsMessages = (1ULL << 30),
	SimilarExpanded = (1ULL << 31),
	CanViewRevenue = (1ULL << 32),
	PaidMediaAllowed = (1ULL << 33),
	CanViewCreditsRevenue = (1ULL << 34),
	SignatureProfiles = (1ULL << 35),
	StargiftsAvailable = (1ULL << 36),
	PaidMessagesAvailable = (1ULL << 37),
	AutoTranslation = (1ULL << 38),
	Monoforum = (1ULL << 39),
	MonoforumAdmin = (1ULL << 40),
	MonoforumDisabled = (1ULL << 41),
	ForumTabs = (1ULL << 42),
	HasStarsPerMessage = (1ULL << 43),
	StarsPerMessageKnown = (1ULL << 44),
};
inline constexpr bool is_flag_type(ChannelDataFlag) { return true; };
using ChannelDataFlags = base::flags<ChannelDataFlag>;

class MegagroupInfo {
public:
	MegagroupInfo();
	~MegagroupInfo();

	struct Admin {
		explicit Admin(ChatAdminRightsInfo rights)
		: rights(rights) {
		}
		Admin(ChatAdminRightsInfo rights, bool canEdit)
		: rights(rights)
		, canEdit(canEdit) {
		}
		ChatAdminRightsInfo rights;
		bool canEdit = false;
	};

	struct Restricted {
		explicit Restricted(ChatRestrictionsInfo rights)
		: rights(rights) {
		}
		ChatRestrictionsInfo rights;
	};

	ChatData *getMigrateFromChat() const;
	void setMigrateFromChat(ChatData *chat);

	const ChannelLocation *getLocation() const;
	void setLocation(const ChannelLocation &location);

	Data::ChatBotCommands::Changed setBotCommands(
		const std::vector<Data::BotCommands> &commands);
	[[nodiscard]] const Data::ChatBotCommands &botCommands() const {
		return _botCommands;
	}

	void ensureForum(not_null<ChannelData*> that);
	[[nodiscard]] Data::Forum *forum() const;
	[[nodiscard]] std::unique_ptr<Data::Forum> takeForumData();

	void ensureMonoforum(not_null<ChannelData*> that);
	[[nodiscard]] Data::SavedMessages *monoforum() const;
	[[nodiscard]] std::unique_ptr<Data::SavedMessages> takeMonoforumData();

	std::deque<not_null<UserData*>> lastParticipants;
	base::flat_map<not_null<UserData*>, Admin> lastAdmins;
	base::flat_map<not_null<UserData*>, Restricted> lastRestricted;
	base::flat_set<not_null<PeerData*>> markupSenders;
	base::flat_set<not_null<UserData*>> bots;
	rpl::event_stream<bool> unrestrictedByBoostsChanges;

	// For admin badges, full admins list with ranks.
	base::flat_map<UserId, QString> admins;

	UserData *creator = nullptr; // nullptr means unknown
	QString creatorRank;
	int botStatus = 0; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other
	bool joinedMessageFound = false;
	bool adminsLoaded = false;
	StickerSetIdentifier stickerSet;
	StickerSetIdentifier emojiSet;

	enum LastParticipantsStatus {
		LastParticipantsUpToDate       = 0x00,
		LastParticipantsOnceReceived   = 0x01,
		LastParticipantsCountOutdated  = 0x02,
	};
	mutable int lastParticipantsStatus = LastParticipantsUpToDate;
	int lastParticipantsCount = 0;
	int boostsApplied = 0;
	int boostsUnrestrict = 0;

	int slowmodeSeconds = 0;
	TimeId slowmodeLastMessage = 0;

private:
	ChatData *_migratedFrom = nullptr;
	ChannelLocation _location;
	Data::ChatBotCommands _botCommands;
	std::unique_ptr<Data::Forum> _forum;
	std::unique_ptr<Data::SavedMessages> _monoforum;

	friend class ChannelData;

};

class ChannelData final : public PeerData {
public:
	using Flag = ChannelDataFlag;
	using Flags = Data::Flags<ChannelDataFlags>;

	using AdminRight = ChatAdminRight;
	using Restriction = ChatRestriction;
	using AdminRights = ChatAdminRights;
	using Restrictions = ChatRestrictions;
	using AdminRightFlags = Data::Flags<AdminRights>;
	using RestrictionFlags = Data::Flags<Restrictions>;

	ChannelData(not_null<Data::Session*> owner, PeerId id);

	void setName(const QString &name, const QString &username);
	void setUsername(const QString &username);
	void setUsernames(const Data::Usernames &newUsernames);
	void setPhoto(const MTPChatPhoto &photo);
	void setAccessHash(uint64 accessHash);

	void setFlags(ChannelDataFlags which);
	void addFlags(ChannelDataFlags which);
	void removeFlags(ChannelDataFlags which);
	[[nodiscard]] auto flags() const {
		return _flags.current();
	}
	[[nodiscard]] auto flagsValue() const {
		return _flags.value();
	}

	[[nodiscard]] QString username() const;
	[[nodiscard]] QString editableUsername() const;
	[[nodiscard]] const std::vector<QString> &usernames() const;
	[[nodiscard]] bool isUsernameEditable(QString username) const;

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

	[[nodiscard]] int pendingRequestsCount() const {
		return _pendingRequestsCount;
	}
	[[nodiscard]] const std::vector<UserId> &recentRequesters() const {
		return _recentRequesters;
	}
	void setPendingRequestsCount(
		int count,
		const QVector<MTPlong> &recentRequesters);
	void setPendingRequestsCount(
		int count,
		std::vector<UserId> recentRequesters);

	[[nodiscard]] bool haveLeft() const {
		return flags() & Flag::Left;
	}
	[[nodiscard]] bool amIn() const {
		return !isForbidden() && !haveLeft();
	}
	[[nodiscard]] bool addsSignature() const {
		return flags() & Flag::Signatures;
	}
	[[nodiscard]] bool signatureProfiles() const {
		return flags() & Flag::SignatureProfiles;
	}
	[[nodiscard]] bool isForbidden() const {
		return flags() & Flag::Forbidden;
	}
	[[nodiscard]] bool isVerified() const {
		return flags() & Flag::Verified;
	}
	[[nodiscard]] bool isScam() const {
		return flags() & Flag::Scam;
	}
	[[nodiscard]] bool isFake() const {
		return flags() & Flag::Fake;
	}
	[[nodiscard]] bool hasStoriesHidden() const {
		return flags() & Flag::StoriesHidden;
	}
	[[nodiscard]] bool viewForumAsMessages() const {
		return flags() & Flag::ViewAsMessages;
	}
	[[nodiscard]] bool stargiftsAvailable() const {
		return flags() & Flag::StargiftsAvailable;
	}
	[[nodiscard]] bool paidMessagesAvailable() const {
		return flags() & Flag::PaidMessagesAvailable;
	}
	[[nodiscard]] bool hasStarsPerMessage() const {
		return flags() & Flag::HasStarsPerMessage;
	}
	[[nodiscard]] bool starsPerMessageKnown() const {
		return flags() & Flag::StarsPerMessageKnown;
	}
	[[nodiscard]] bool useSubsectionTabs() const;

	[[nodiscard]] static ChatRestrictionsInfo KickedRestrictedRights(
		not_null<PeerData*> participant);
	static constexpr auto kRestrictUntilForever = TimeId(INT_MAX);
	[[nodiscard]] static bool IsRestrictedForever(TimeId until) {
		return !until || (until == kRestrictUntilForever);
	}
	void applyEditAdmin(
		not_null<UserData*> user,
		ChatAdminRightsInfo oldRights,
		ChatAdminRightsInfo newRights,
		const QString &rank);
	void applyEditBanned(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo oldRights,
		ChatRestrictionsInfo newRights);
	void setViewAsMessagesFlag(bool enabled);

	void markForbidden();

	[[nodiscard]] bool isGroupAdmin(not_null<UserData*> user) const;
	[[nodiscard]] bool lastParticipantsRequestNeeded() const;
	[[nodiscard]] bool isMegagroup() const {
		return flags() & Flag::Megagroup;
	}
	[[nodiscard]] bool isBroadcast() const {
		return flags() & Flag::Broadcast;
	}
	[[nodiscard]] bool isGigagroup() const {
		return flags() & Flag::Gigagroup;
	}
	[[nodiscard]] bool isForum() const {
		return flags() & Flag::Forum;
	}
	[[nodiscard]] bool isMonoforum() const {
		return flags() & Flag::Monoforum;
	}
	[[nodiscard]] bool hasUsername() const {
		return flags() & Flag::Username;
	}
	[[nodiscard]] bool hasLocation() const {
		return flags() & Flag::Location;
	}
	[[nodiscard]] bool isPublic() const {
		return hasUsername() || hasLocation();
	}
	[[nodiscard]] bool amCreator() const {
		return flags() & Flag::Creator;
	}
	[[nodiscard]] bool joinToWrite() const {
		return flags() & Flag::JoinToWrite;
	}
	[[nodiscard]] bool requestToJoin() const {
		return flags() & Flag::RequestToJoin;
	}
	[[nodiscard]] bool antiSpamMode() const {
		return flags() & Flag::AntiSpam;
	}
	[[nodiscard]] bool autoTranslation() const {
		return flags() & Flag::AutoTranslation;
	}

	[[nodiscard]] auto adminRights() const {
		return _adminRights.current();
	}
	[[nodiscard]] auto adminRightsValue() const {
		return _adminRights.value();
	}
	void setAdminRights(ChatAdminRights rights);
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
	void setRestrictions(ChatRestrictionsInfo rights);
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
	void setDefaultRestrictions(ChatRestrictions rights);

	// Like in ChatData.
	[[nodiscard]] bool allowsForwarding() const;
	[[nodiscard]] bool canEditInformation() const;
	[[nodiscard]] bool canEditPermissions() const;
	[[nodiscard]] bool canEditUsername() const;
	[[nodiscard]] bool canEditPreHistoryHidden() const;
	[[nodiscard]] bool canAddMembers() const;
	[[nodiscard]] bool canAddAdmins() const;
	[[nodiscard]] bool canBanMembers() const;
	[[nodiscard]] bool anyoneCanAddMembers() const;

	[[nodiscard]] bool canPostMessages() const;
	[[nodiscard]] bool canEditMessages() const;
	[[nodiscard]] bool canDeleteMessages() const;
	[[nodiscard]] bool canPostStories() const;
	[[nodiscard]] bool canEditStories() const;
	[[nodiscard]] bool canDeleteStories() const;
	[[nodiscard]] bool canPostPaidMedia() const;
	[[nodiscard]] bool canAccessMonoforum() const;
	[[nodiscard]] bool hiddenPreHistory() const;
	[[nodiscard]] bool canViewMembers() const;
	[[nodiscard]] bool canViewAdmins() const;
	[[nodiscard]] bool canViewBanned() const;
	[[nodiscard]] bool canEditSignatures() const;
	[[nodiscard]] bool canEditAutoTranslate() const;
	[[nodiscard]] bool canEditStickers() const;
	[[nodiscard]] bool canEditEmoji() const;
	[[nodiscard]] bool canDelete() const;
	[[nodiscard]] bool canEditAdmin(not_null<UserData*> user) const;
	[[nodiscard]] bool canRestrictParticipant(
		not_null<PeerData*> participant) const;

	void setBotVerifyDetails(Ui::BotVerifyDetails details);
	void setBotVerifyDetailsIcon(DocumentId iconId);
	[[nodiscard]] Ui::BotVerifyDetails *botVerifyDetails() const {
		return _botVerifyDetails.get();
	}

	void setInviteLink(const QString &newInviteLink);
	[[nodiscard]] QString inviteLink() const {
		return _inviteLink;
	}
	[[nodiscard]] bool canHaveInviteLink() const;

	void setLocation(const MTPChannelLocation &data);
	[[nodiscard]] const ChannelLocation *getLocation() const;

	void setDiscussionLink(ChannelData *link);
	[[nodiscard]] ChannelData *discussionLink() const;
	[[nodiscard]] bool discussionLinkKnown() const;

	void setMonoforumLink(ChannelData *link);
	[[nodiscard]] ChannelData *monoforumLink() const;
	[[nodiscard]] bool monoforumDisabled() const;

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
	void ptsSetWaitingForShortPoll(int32 ms) {
		return _ptsWaiter.setWaitingForShortPoll(this, ms);
	}
	[[nodiscard]] bool ptsWaitingForSkipped() const {
		return _ptsWaiter.waitingForSkipped();
	}
	[[nodiscard]] bool ptsWaitingForShortPoll() const {
		return _ptsWaiter.waitingForShortPoll();
	}

	[[nodiscard]] MsgId availableMinId() const {
		return _availableMinId;
	}
	void setAvailableMinId(MsgId availableMinId);

	[[nodiscard]] ChatData *getMigrateFromChat() const;
	void setMigrateFromChat(ChatData *chat);

	[[nodiscard]] int slowmodeSeconds() const;
	void setSlowmodeSeconds(int seconds);
	[[nodiscard]] TimeId slowmodeLastMessage() const;
	void growSlowmodeLastMessage(TimeId when);

	void setStarsPerMessage(int stars);
	[[nodiscard]] int starsPerMessage() const;
	[[nodiscard]] int commonStarsPerMessage() const;

	[[nodiscard]] int peerGiftsCount() const;
	void setPeerGiftsCount(int count);

	[[nodiscard]] int boostsApplied() const;
	[[nodiscard]] int boostsUnrestrict() const;
	[[nodiscard]] bool unrestrictedByBoosts() const;
	[[nodiscard]] rpl::producer<bool> unrestrictedByBoostsValue() const;
	void setBoostsUnrestrict(int applied, int unrestrict);

	void setInvitePeek(const QString &hash, TimeId expires);
	void clearInvitePeek();
	[[nodiscard]] TimeId invitePeekExpires() const;
	[[nodiscard]] QString invitePeekHash() const;
	void privateErrorReceived();

	[[nodiscard]] Data::GroupCall *groupCall() const {
		return _call.get();
	}
	void migrateCall(std::unique_ptr<Data::GroupCall> call);
	void setGroupCall(
		const MTPInputGroupCall &call,
		TimeId scheduleDate = 0,
		bool rtmp = false);
	void clearGroupCall();
	void setGroupCallDefaultJoinAs(PeerId peerId);
	[[nodiscard]] PeerId groupCallDefaultJoinAs() const;

	void setAllowedReactions(Data::AllowedReactions value);
	[[nodiscard]] const Data::AllowedReactions &allowedReactions() const;

	[[nodiscard]] bool hasActiveStories() const;
	[[nodiscard]] bool hasUnreadStories() const;
	void setStoriesState(StoriesState state);

	[[nodiscard]] Data::Forum *forum() const {
		return mgInfo ? mgInfo->forum() : nullptr;
	}
	[[nodiscard]] Data::SavedMessages *monoforum() const {
		return mgInfo ? mgInfo->monoforum() : nullptr;
	}

	void processTopics(const MTPVector<MTPForumTopic> &topics);

	[[nodiscard]] int levelHint() const;
	void updateLevelHint(int levelHint);

	[[nodiscard]] TimeId subscriptionUntilDate() const;
	void updateSubscriptionUntilDate(TimeId subscriptionUntilDate);

	// Still public data members.
	uint64 access = 0;

	MTPinputChannel inputChannel = MTP_inputChannelEmpty();

	int32 date = 0;
	std::unique_ptr<MegagroupInfo> mgInfo;

	// > 0 - user who invited me to channel, < 0 - not in channel.
	UserId inviter = 0;
	TimeId inviteDate = 0;
	bool inviteViaRequest = false;

private:
	struct InvitePeek {
		QString hash;
		TimeId expires = 0;
	};

	auto unavailableReasons() const
		-> const std::vector<Data::UnavailableReason> & override;
	bool canEditLastAdmin(not_null<UserData*> user) const;

	void setUnavailableReasonsList(
		std::vector<Data::UnavailableReason> &&reasons) override;

	Flags _flags = ChannelDataFlags(Flag::Forbidden);

	PtsWaiter _ptsWaiter;

	Data::UsernamesInfo _username;

	std::vector<UserId> _recentRequesters;
	MsgId _availableMinId = 0;

	RestrictionFlags _defaultRestrictions;
	AdminRightFlags _adminRights;
	RestrictionFlags _restrictions;
	TimeId _restrictedUntil;
	TimeId _subscriptionUntilDate;

	std::vector<Data::UnavailableReason> _unavailableReasons;
	std::unique_ptr<InvitePeek> _invitePeek;
	QString _inviteLink;

	ChannelData *_discussionLink = nullptr;
	ChannelData *_monoforumLink = nullptr;
	bool _discussionLinkKnown = false;

	int _peerGiftsCount = 0;
	int _membersCount = -1;
	int _adminsCount = 1;
	int _restrictedCount = 0;
	int _kickedCount = 0;
	int _pendingRequestsCount = 0;
	int _levelHint = 0;
	int _starsPerMessage = 0;

	Data::AllowedReactions _allowedReactions;

	std::unique_ptr<Data::GroupCall> _call;
	PeerId _callDefaultJoinAs = 0;

	std::unique_ptr<Ui::BotVerifyDetails> _botVerifyDetails;

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

} // namespace Data
