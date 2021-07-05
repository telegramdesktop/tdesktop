/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer.h"

enum class ChatDataFlag {
	Left = (1 << 0),
	Kicked = (1 << 1),
	Creator = (1 << 2),
	Deactivated = (1 << 3),
	Forbidden = (1 << 4),
	CallActive = (1 << 5),
	CallNotEmpty = (1 << 6),
	CanSetUsername = (1 << 7),
};
inline constexpr bool is_flag_type(ChatDataFlag) { return true; };
using ChatDataFlags = base::flags<ChatDataFlag>;

class ChatData : public PeerData {
public:
	using Flag = ChatDataFlag;
	using Flags = Data::Flags<ChatDataFlags>;

	using AdminRight = ChatAdminRight;
	using Restriction = ChatRestriction;
	using AdminRights = ChatAdminRights;
	using Restrictions = ChatRestrictions;
	using AdminRightFlags = Data::Flags<AdminRights>;
	using RestrictionFlags = Data::Flags<Restrictions>;

	ChatData(not_null<Data::Session*> owner, PeerId id);

	void setName(const QString &newName);
	void setPhoto(const MTPChatPhoto &photo);

	void invalidateParticipants();
	[[nodiscard]] bool noParticipantInfo() const {
		return (count > 0 || amIn()) && participants.empty();
	}

	void setFlags(ChatDataFlags which) {
		_flags.set(which);
	}
	void addFlags(ChatDataFlags which) {
		_flags.add(which);
	}
	void removeFlags(ChatDataFlags which) {
		_flags.remove(which);
	}
	[[nodiscard]] auto flags() const {
		return _flags.current();
	}
	[[nodiscard]] auto flagsValue() const {
		return _flags.value();
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

	[[nodiscard]] auto defaultRestrictions() const {
		return _defaultRestrictions.current();
	}
	[[nodiscard]] auto defaultRestrictionsValue() const {
		return _defaultRestrictions.value();
	}
	void setDefaultRestrictions(ChatRestrictions rights);

	[[nodiscard]] bool isForbidden() const {
		return flags() & Flag::Forbidden;
	}
	[[nodiscard]] bool amIn() const {
		return !isForbidden()
			&& !isDeactivated()
			&& !haveLeft()
			&& !wasKicked();
	}
	[[nodiscard]] bool haveLeft() const {
		return flags() & ChatDataFlag::Left;
	}
	[[nodiscard]] bool wasKicked() const {
		return flags() & ChatDataFlag::Kicked;
	}
	[[nodiscard]] bool amCreator() const {
		return flags() & ChatDataFlag::Creator;
	}
	[[nodiscard]] bool isDeactivated() const {
		return flags() & ChatDataFlag::Deactivated;
	}
	[[nodiscard]] bool isMigrated() const {
		return (_migratedTo != nullptr);
	}

	[[nodiscard]] ChatAdminRightsInfo defaultAdminRights(
		not_null<UserData*> user);

	// Like in ChannelData.
	[[nodiscard]] bool canWrite() const;
	[[nodiscard]] bool canEditInformation() const;
	[[nodiscard]] bool canEditPermissions() const;
	[[nodiscard]] bool canEditUsername() const;
	[[nodiscard]] bool canEditPreHistoryHidden() const;
	[[nodiscard]] bool canDeleteMessages() const;
	[[nodiscard]] bool canAddMembers() const;
	[[nodiscard]] bool canAddAdmins() const;
	[[nodiscard]] bool canBanMembers() const;
	[[nodiscard]] bool canSendPolls() const;
	[[nodiscard]] bool anyoneCanAddMembers() const;

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
	void setGroupCall(
		const MTPInputGroupCall &call,
		TimeId scheduleDate = 0);
	void clearGroupCall();
	void setGroupCallDefaultJoinAs(PeerId peerId);
	[[nodiscard]] PeerId groupCallDefaultJoinAs() const;

	void setBotCommands(const MTPVector<MTPBotInfo> &data);
	void setBotCommands(
		UserId botId,
		const MTPVector<MTPBotCommand> &data);
	[[nodiscard]] auto botCommands() const
		-> const base::flat_map<UserId, std::vector<BotCommand>> & {
		return _botCommands;
	}

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
	QString _inviteLink;

	RestrictionFlags _defaultRestrictions;
	AdminRightFlags _adminRights;
	int _version = 0;

	std::unique_ptr<Data::GroupCall> _call;
	PeerId _callDefaultJoinAs = 0;
	base::flat_map<UserId, std::vector<BotCommand>> _botCommands;

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
