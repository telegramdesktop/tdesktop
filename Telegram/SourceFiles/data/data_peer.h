/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "data/data_types.h"
#include "data/data_flags.h"
#include "data/data_notify_settings.h"

namespace Ui {
class EmptyUserpic;
} // namespace Ui

class PeerData;
class UserData;
class ChatData;
class ChannelData;

namespace Data {

int PeerColorIndex(PeerId peerId);
int PeerColorIndex(int32 bareId);
style::color PeerUserpicColor(PeerId peerId);

} // namespace Data

class PeerClickHandler : public ClickHandler {
public:
	PeerClickHandler(not_null<PeerData*> peer);
	void onClick(Qt::MouseButton button) const override;

	not_null<PeerData*> peer() const {
		return _peer;
	}

private:
	not_null<PeerData*> _peer;

};

class PeerData {
protected:
	PeerData(const PeerId &id);
	PeerData(const PeerData &other) = delete;
	PeerData &operator=(const PeerData &other) = delete;

public:
	virtual ~PeerData();

	bool isUser() const {
		return peerIsUser(id);
	}
	bool isChat() const {
		return peerIsChat(id);
	}
	bool isChannel() const {
		return peerIsChannel(id);
	}
	bool isSelf() const {
		return (input.type() == mtpc_inputPeerSelf);
	}
	bool isVerified() const;
	bool isMegagroup() const;

	TimeMs notifyMuteFinishesIn() const {
		return _notify.muteFinishesIn();
	}
	bool notifyChange(const MTPPeerNotifySettings &settings) {
		return _notify.change(settings);
	}
	bool notifyChange(
			Data::NotifySettings::MuteChange mute,
			Data::NotifySettings::SilentPostsChange silent,
			int muteForSeconds) {
		return _notify.change(mute, silent, muteForSeconds);
	}
	bool notifySettingsUnknown() const {
		return _notify.settingsUnknown();
	}
	bool notifySilentPosts() const {
		return _notify.silentPosts();
	}
	MTPinputPeerNotifySettings notifySerialize() const {
		return _notify.serialize();
	}
	bool isMuted() const {
		return (notifyMuteFinishesIn() > 0);
	}

	bool canWrite() const;
	UserData *asUser();
	const UserData *asUser() const;
	ChatData *asChat();
	const ChatData *asChat() const;
	ChannelData *asChannel();
	const ChannelData *asChannel() const;
	ChannelData *asMegagroup();
	const ChannelData *asMegagroup() const;

	ChatData *migrateFrom() const;
	ChannelData *migrateTo() const;

	void updateFull();
	void updateFullForced();
	void fullUpdated();
	bool wasFullUpdated() const {
		return (_lastFullUpdate != 0);
	}

	const Text &dialogName() const;
	const QString &shortName() const;
	QString userName() const;

	const PeerId id;
	int32 bareId() const {
		return int32(uint32(id & 0xFFFFFFFFULL));
	}

	QString name;
	Text nameText;

	using NameWords = base::flat_set<QString>;
	using NameFirstChars = base::flat_set<QChar>;
	const NameWords &nameWords() const {
		return _nameWords;
	}
	const NameFirstChars &nameFirstChars() const {
		return _nameFirstChars;
	}

	enum LoadedStatus {
		NotLoaded = 0x00,
		MinimalLoaded = 0x01,
		FullLoaded = 0x02,
	};
	LoadedStatus loadedStatus = NotLoaded;
	MTPinputPeer input;

	void setUserpic(
		PhotoId photoId,
		const StorageImageLocation &location,
		ImagePtr userpic);
	void setUserpicPhoto(const MTPPhoto &data);
	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const;
	void paintUserpicLeft(
			Painter &p,
			int x,
			int y,
			int w,
			int size) const {
		paintUserpic(p, rtl() ? (w - x - size) : x, y, size);
	}
	void paintUserpicRounded(
		Painter &p,
		int x,
		int y,
		int size) const;
	void paintUserpicSquare(
		Painter &p,
		int x,
		int y,
		int size) const;
	void loadUserpic(bool loadFirst = false, bool prior = true) {
		_userpic->load(loadFirst, prior);
	}
	bool userpicLoaded() const {
		return _userpic->loaded();
	}
	bool useEmptyUserpic() const {
		return _userpicLocation.isNull()
			|| !_userpic
			|| !_userpic->loaded();
	}
	StorageKey userpicUniqueKey() const;
	void saveUserpic(const QString &path, int size) const;
	void saveUserpicRounded(const QString &path, int size) const;
	QPixmap genUserpic(int size) const;
	QPixmap genUserpicRounded(int size) const;
	StorageImageLocation userpicLocation() const {
		return _userpicLocation;
	}
	bool userpicPhotoUnknown() const {
		return (_userpicPhotoId == kUnknownPhotoId);
	}
	PhotoId userpicPhotoId() const {
		return userpicPhotoUnknown() ? 0 : _userpicPhotoId;
	}

	int nameVersion = 1;

	// if this string is not empty we must not allow to open the
	// conversation and we must show this string instead
	virtual QString restrictionReason() const {
		return QString();
	}

	ClickHandlerPtr createOpenLink();
	const ClickHandlerPtr &openLink() {
		if (!_openLink) {
			_openLink = createOpenLink();
		}
		return _openLink;
	}

	ImagePtr currentUserpic() const;

protected:
	void updateNameDelayed(
		const QString &newName,
		const QString &newNameOrPhone,
		const QString &newUsername);
	void updateUserpic(PhotoId photoId, const MTPFileLocation &location);
	void clearUserpic();

private:
	void fillNames();
	std::unique_ptr<Ui::EmptyUserpic> createEmptyUserpic() const;
	void refreshEmptyUserpic() const;

	void setUserpicChecked(
		PhotoId photoId,
		const StorageImageLocation &location,
		ImagePtr userpic);

	static constexpr auto kUnknownPhotoId = PhotoId(0xFFFFFFFFFFFFFFFFULL);

	ImagePtr _userpic;
	PhotoId _userpicPhotoId = kUnknownPhotoId;
	mutable std::unique_ptr<Ui::EmptyUserpic> _userpicEmpty;
	StorageImageLocation _userpicLocation;

	Data::NotifySettings _notify;

	ClickHandlerPtr _openLink;
	NameWords _nameWords; // for filtering
	NameFirstChars _nameFirstChars;

	TimeMs _lastFullUpdate = 0;

};

class BotCommand {
public:
	BotCommand(
		const QString &command,
		const QString &description)
	: command(command)
	, _description(description) {
	}
	QString command;

	bool setDescription(const QString &description) {
		if (_description != description) {
			_description = description;
			_descriptionText = Text();
			return true;
		}
		return false;
	}

	const Text &descriptionText() const;

private:
	QString _description;
	mutable Text _descriptionText;

};

struct BotInfo {
	bool inited = false;
	bool readsAllHistory = false;
	bool cantJoinGroups = false;
	int version = 0;
	QString description, inlinePlaceholder;
	QList<BotCommand> commands;
	Text text = Text{ int(st::msgMinWidth) }; // description

	QString startToken, startGroupToken, shareGameShortName;
	PeerId inlineReturnPeerId = 0;
};

class UserData : public PeerData {
public:
	static constexpr auto kEssentialFlags = 0
		| MTPDuser::Flag::f_self
		| MTPDuser::Flag::f_contact
		| MTPDuser::Flag::f_mutual_contact
		| MTPDuser::Flag::f_deleted
		| MTPDuser::Flag::f_bot
		| MTPDuser::Flag::f_bot_chat_history
		| MTPDuser::Flag::f_bot_nochats
		| MTPDuser::Flag::f_verified
		| MTPDuser::Flag::f_restricted
		| MTPDuser::Flag::f_bot_inline_geo;
	using Flags = Data::Flags<
		MTPDuser::Flags,
		kEssentialFlags.value()>;

	static constexpr auto kEssentialFullFlags = 0
		| MTPDuserFull::Flag::f_blocked
		| MTPDuserFull::Flag::f_phone_calls_available
		| MTPDuserFull::Flag::f_phone_calls_private;
	using FullFlags = Data::Flags<
		MTPDuserFull::Flags,
		kEssentialFullFlags.value()>;

	UserData(const PeerId &id) : PeerData(id) {
	}
	void setPhoto(const MTPUserProfilePhoto &photo);

	void setName(
		const QString &newFirstName,
		const QString &newLastName,
		const QString &newPhoneName,
		const QString &newUsername);

	void setPhone(const QString &newPhone);
	void setBotInfoVersion(int version);
	void setBotInfo(const MTPBotInfo &info);

	void setNameOrPhone(const QString &newNameOrPhone);

	void madeAction(TimeId when); // pseudo-online

	uint64 accessHash() const {
		return _accessHash;
	}
	void setAccessHash(uint64 accessHash);

	void setFlags(MTPDuser::Flags which) {
		_flags.set(which);
	}
	void addFlags(MTPDuser::Flags which) {
		_flags.add(which);
	}
	void removeFlags(MTPDuser::Flags which) {
		_flags.remove(which);
	}
	auto flags() const {
		return _flags.current();
	}
	auto flagsValue() const {
		return _flags.value();
	}

	void setFullFlags(MTPDuserFull::Flags which) {
		_fullFlags.set(which);
	}
	void addFullFlags(MTPDuserFull::Flags which) {
		_fullFlags.add(which);
	}
	void removeFullFlags(MTPDuserFull::Flags which) {
		_fullFlags.remove(which);
	}
	auto fullFlags() const {
		return _fullFlags.current();
	}
	auto fullFlagsValue() const {
		return _fullFlags.value();
	}

	bool isVerified() const {
		return flags() & MTPDuser::Flag::f_verified;
	}
	bool isBotInlineGeo() const {
		return flags() & MTPDuser::Flag::f_bot_inline_geo;
	}
	bool isInaccessible() const {
		constexpr auto inaccessible = 0
			| MTPDuser::Flag::f_deleted;
//			| MTPDuser_ClientFlag::f_inaccessible;
		return flags() & inaccessible;
	}
	bool canWrite() const {
		// Duplicated in Data::CanWriteValue().
		return !isInaccessible();
	}
	bool isContact() const {
		return (contact > 0);
	}

	bool canShareThisContact() const;
	bool canAddContact() const {
		return canShareThisContact() && !isContact();
	}

	// In feedUsers() we check only that.
	// When actually trying to share contact we perform
	// a full check by canShareThisContact() call.
	bool canShareThisContactFast() const {
		return !_phone.isEmpty();
	}

	MTPInputUser inputUser;

	QString firstName;
	QString lastName;
	QString username;
	const QString &phone() const {
		return _phone;
	}
	QString nameOrPhone;
	Text phoneText;
	TimeId onlineTill = 0;
	int32 contact = -1; // -1 - not contact, cant add (self, empty, deleted, foreign), 0 - not contact, can add (request), 1 - contact

	enum class BlockStatus {
		Unknown,
		Blocked,
		NotBlocked,
	};
	BlockStatus blockStatus() const {
		return _blockStatus;
	}
	bool isBlocked() const {
		return (blockStatus() == BlockStatus::Blocked);
	}
	void setBlockStatus(BlockStatus blockStatus);

	enum class CallsStatus {
		Unknown,
		Enabled,
		Disabled,
		Private,
	};
	CallsStatus callsStatus() const {
		return _callsStatus;
	}
	bool hasCalls() const;
	void setCallsStatus(CallsStatus callsStatus);

	bool setAbout(const QString &newAbout);
	const QString &about() const {
		return _about;
	}

	std::unique_ptr<BotInfo> botInfo;

	QString restrictionReason() const override {
		return _restrictionReason;
	}
	void setRestrictionReason(const QString &reason);

	int commonChatsCount() const {
		return _commonChatsCount;
	}
	void setCommonChatsCount(int count);

private:
	Flags _flags;
	FullFlags _fullFlags;

	QString _restrictionReason;
	QString _about;
	QString _phone;
	BlockStatus _blockStatus = BlockStatus::Unknown;
	CallsStatus _callsStatus = CallsStatus::Unknown;
	int _commonChatsCount = 0;

	uint64 _accessHash = 0;
	static constexpr auto kInaccessibleAccessHashOld
		= 0xFFFFFFFFFFFFFFFFULL;

};

class ChatData : public PeerData {
public:
	static constexpr auto kEssentialFlags = 0
		| MTPDchat::Flag::f_creator
		| MTPDchat::Flag::f_kicked
		| MTPDchat::Flag::f_left
		| MTPDchat::Flag::f_admins_enabled
		| MTPDchat::Flag::f_admin
		| MTPDchat::Flag::f_deactivated
		| MTPDchat::Flag::f_migrated_to;
	using Flags = Data::Flags<
		MTPDchat::Flags,
		kEssentialFlags>;

	ChatData(const PeerId &id)
	: PeerData(id)
	, inputChat(MTP_int(bareId())) {
	}
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
	bool canEdit() const {
		return !isDeactivated()
			&& (amCreator()
				|| (adminsEnabled() ? amAdmin() : amIn()));
	}
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
	bool adminsEnabled() const {
		return flags() & MTPDchat::Flag::f_admins_enabled;
	}
	bool amCreator() const {
		return flags() & MTPDchat::Flag::f_creator;
	}
	bool amAdmin() const {
		return (flags() & MTPDchat::Flag::f_admin) && adminsEnabled();
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

enum PtsSkippedQueue {
	SkippedUpdate,
	SkippedUpdates,
};
class PtsWaiter {
public:
	PtsWaiter() = default;
	void init(int32 pts) {
		_good = _last = _count = pts;
		clearSkippedUpdates();
	}
	bool inited() const {
		return _good > 0;
	}
	void setRequesting(bool isRequesting) {
		_requesting = isRequesting;
		if (_requesting) {
			clearSkippedUpdates();
		}
	}
	bool requesting() const {
		return _requesting;
	}
	bool waitingForSkipped() const {
		return _waitingForSkipped;
	}
	bool waitingForShortPoll() const {
		return _waitingForShortPoll;
	}
	void setWaitingForSkipped(ChannelData *channel, int32 ms); // < 0 - not waiting
	void setWaitingForShortPoll(ChannelData *channel, int32 ms); // < 0 - not waiting
	int32 current() const{
		return _good;
	}
	bool updated(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdates &updates);
	bool updated(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdate &update);
	bool updated(
		ChannelData *channel,
		int32 pts,
		int32 count);
	bool updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdates &updates);
	bool updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdate &update);
	bool updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count);
	void applySkippedUpdates(ChannelData *channel);
	void clearSkippedUpdates();

private:
	bool check(ChannelData *channel, int32 pts, int32 count); // return false if need to save that update and apply later
	uint64 ptsKey(PtsSkippedQueue queue, int32 pts);
	void checkForWaiting(ChannelData *channel);
	QMap<uint64, PtsSkippedQueue> _queue;
	QMap<uint64, MTPUpdate> _updateQueue;
	QMap<uint64, MTPUpdates> _updatesQueue;
	int32 _good = 0;
	int32 _last = 0;
	int32 _count = 0;
	int32 _applySkippedLevel = 0;
	bool _requesting = false;
	bool _waitingForSkipped = false;
	bool _waitingForShortPoll = false;
	uint32 _skippedKey = 0;
};

struct MegagroupInfo {
	struct Admin {
		explicit Admin(MTPChannelAdminRights rights)
		: rights(rights) {
		}
		Admin(MTPChannelAdminRights rights, bool canEdit)
		: rights(rights)
		, canEdit(canEdit) {
		}
		MTPChannelAdminRights rights;
		bool canEdit = false;
	};
	struct Restricted {
		explicit Restricted(MTPChannelBannedRights rights)
		: rights(rights) {
		}
		MTPChannelBannedRights rights;
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

using ChannelAdminRight = MTPDchannelAdminRights::Flag;
using ChannelRestriction = MTPDchannelBannedRights::Flag;
using ChannelAdminRights = MTPDchannelAdminRights::Flags;
using ChannelRestrictions = MTPDchannelBannedRights::Flags;

class ChannelData : public PeerData {
public:
	static constexpr auto kEssentialFlags = 0
		| MTPDchannel::Flag::f_creator
		| MTPDchannel::Flag::f_left
		| MTPDchannel::Flag::f_broadcast
		| MTPDchannel::Flag::f_verified
		| MTPDchannel::Flag::f_megagroup
		| MTPDchannel::Flag::f_restricted
		| MTPDchannel::Flag::f_democracy
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

	ChannelData(const PeerId &id);

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

	uint64 access = 0;

	MTPinputChannel inputChannel;

	QString username;

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

	static MTPChannelBannedRights KickedRestrictedRights();
	static constexpr auto kRestrictUntilForever = TimeId(INT_MAX);
	static bool IsRestrictedForever(TimeId until) {
		return !until || (until == kRestrictUntilForever);
	}
	void applyEditAdmin(
		not_null<UserData*> user,
		const MTPChannelAdminRights &oldRights,
		const MTPChannelAdminRights &newRights);
	void applyEditBanned(
		not_null<UserData*> user,
		const MTPChannelBannedRights &oldRights,
		const MTPChannelBannedRights &newRights);

	bool isGroupAdmin(not_null<UserData*> user) const;

	int32 date = 0;
	int version = 0;
	std::unique_ptr<MegagroupInfo> mgInfo;
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

	using AdminRight = ChannelAdminRight;
	using Restriction = ChannelRestriction;
	using AdminRights = ChannelAdminRights;
	using Restrictions = ChannelRestrictions;
	using AdminRightFlags = Data::Flags<AdminRights>;
	using RestrictionFlags = Data::Flags<Restrictions>;
	auto adminRights() const {
		return _adminRights.current();
	}
	auto adminRightsValue() const {
		return _adminRights.value();
	}
	void setAdminRights(const MTPChannelAdminRights &rights);
	bool hasAdminRights() const {
		return (adminRights() != 0);
	}
	auto restrictions() const {
		return _restrictions.current();
	}
	auto restrictionsValue() const {
		return _restrictions.value();
	}
	bool restricted(Restriction right) const {
		return restrictions() & right;
	}
	TimeId restrictedUntil() const {
		return _restrictedUntill;
	}
	void setRestrictedRights(const MTPChannelBannedRights &rights);
	bool hasRestrictions() const {
		return (restrictions() != 0);
	}
	bool hasRestrictions(TimeId now) const {
		return hasRestrictions()
			&& (restrictedUntil() > now);
	}
	bool canBanMembers() const;
	bool canEditMessages() const;
	bool canDeleteMessages() const;
	bool anyoneCanAddMembers() const;
	bool hiddenPreHistory() const;
	bool canAddMembers() const;
	bool canAddAdmins() const;
	bool canPinMessages() const;
	bool canPublish() const;
	bool canWrite() const;
	bool canViewMembers() const;
	bool canViewAdmins() const;
	bool canViewBanned() const;
	bool canEditInformation() const;
	bool canEditInvites() const;
	bool canEditSignatures() const;
	bool canEditPreHistoryHidden() const;
	bool canEditUsername() const;
	bool canEditStickers() const;
	bool canDelete() const;
	bool canEditAdmin(not_null<UserData*> user) const;
	bool canRestrictUser(not_null<UserData*> user) const;

	void setInviteLink(const QString &newInviteLink);
	QString inviteLink() const {
		return _inviteLink;
	}
	bool canHaveInviteLink() const {
		return (adminRights() & AdminRight::f_invite_link)
			|| amCreator();
	}

	int32 inviter = 0; // > 0 - user who invited me to channel, < 0 - not in channel
	QDateTime inviteDate;

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

	QString restrictionReason() const override {
		return _restrictionReason;
	}
	void setRestrictionReason(const QString &reason);

	MsgId availableMinId() const {
		return _availableMinId;
	}
	void setAvailableMinId(MsgId availableMinId);

	MsgId pinnedMessageId() const {
		return _pinnedMessageId;
	}
	void setPinnedMessageId(MsgId messageId);
	void clearPinnedMessage() {
		setPinnedMessageId(0);
	}

private:
	void flagsUpdated(MTPDchannel::Flags diff);
	void fullFlagsUpdated(MTPDchannelFull::Flags diff);

	bool canEditLastAdmin(not_null<UserData*> user) const;

	Flags _flags = Flags(MTPDchannel_ClientFlag::f_forbidden | 0);
	FullFlags _fullFlags;

	PtsWaiter _ptsWaiter;

	int _membersCount = 1;
	int _adminsCount = 1;
	int _restrictedCount = 0;
	int _kickedCount = 0;
	MsgId _availableMinId = 0;
	MsgId _pinnedMessageId = 0;

	AdminRightFlags _adminRights;
	RestrictionFlags _restrictions;
	TimeId _restrictedUntill;

	QString _restrictionReason;
	QString _about;

	QString _inviteLink;

	rpl::lifetime _lifetime;

};

inline bool isUser(const PeerData *peer) {
	return peer ? peer->isUser() : false;
}
inline UserData *PeerData::asUser() {
	return isUser() ? static_cast<UserData*>(this) : nullptr;
}
inline UserData *asUser(PeerData *peer) {
	return peer ? peer->asUser() : nullptr;
}
inline const UserData *PeerData::asUser() const {
	return isUser() ? static_cast<const UserData*>(this) : nullptr;
}
inline const UserData *asUser(const PeerData *peer) {
	return peer ? peer->asUser() : nullptr;
}
inline bool isChat(const PeerData *peer) {
	return peer ? peer->isChat() : false;
}
inline ChatData *PeerData::asChat() {
	return isChat() ? static_cast<ChatData*>(this) : nullptr;
}
inline ChatData *asChat(PeerData *peer) {
	return peer ? peer->asChat() : nullptr;
}
inline const ChatData *PeerData::asChat() const {
	return isChat() ? static_cast<const ChatData*>(this) : nullptr;
}
inline const ChatData *asChat(const PeerData *peer) {
	return peer ? peer->asChat() : nullptr;
}
inline bool isChannel(const PeerData *peer) {
	return peer ? peer->isChannel() : false;
}
inline ChannelData *PeerData::asChannel() {
	return isChannel() ? static_cast<ChannelData*>(this) : nullptr;
}
inline ChannelData *asChannel(PeerData *peer) {
	return peer ? peer->asChannel() : nullptr;
}
inline const ChannelData *PeerData::asChannel() const {
	return isChannel()
		? static_cast<const ChannelData*>(this)
		: nullptr;
}
inline const ChannelData *asChannel(const PeerData *peer) {
	return peer ? peer->asChannel() : nullptr;
}
inline ChannelData *PeerData::asMegagroup() {
	return isMegagroup() ? static_cast<ChannelData*>(this) : nullptr;
}
inline ChannelData *asMegagroup(PeerData *peer) {
	return peer ? peer->asMegagroup() : nullptr;
}
inline const ChannelData *PeerData::asMegagroup() const {
	return isMegagroup()
		? static_cast<const ChannelData*>(this)
		: nullptr;
}
inline const ChannelData *asMegagroup(const PeerData *peer) {
	return peer ? peer->asMegagroup() : nullptr;
}
inline bool isMegagroup(const PeerData *peer) {
	return peer ? peer->isMegagroup() : false;
}
inline ChatData *PeerData::migrateFrom() const {
	return (isMegagroup() && asChannel()->amIn())
		? asChannel()->mgInfo->migrateFromPtr
		: nullptr;
}
inline ChannelData *PeerData::migrateTo() const {
	return (isChat()
		&& asChat()->migrateToPtr
		&& asChat()->migrateToPtr->amIn())
		? asChat()->migrateToPtr
		: nullptr;
}
inline const Text &PeerData::dialogName() const {
	return migrateTo()
		? migrateTo()->dialogName()
		: (isUser() && !asUser()->phoneText.isEmpty())
			? asUser()->phoneText
			: nameText;
}
inline const QString &PeerData::shortName() const {
	return isUser() ? asUser()->firstName : name;
}
inline QString PeerData::userName() const {
	return isUser()
		? asUser()->username
		: isChannel()
			? asChannel()->username
			: QString();
}
inline bool PeerData::isVerified() const {
	return isUser()
		? asUser()->isVerified()
		: isChannel()
			? asChannel()->isVerified()
			: false;
}
inline bool PeerData::isMegagroup() const {
	return isChannel() ? asChannel()->isMegagroup() : false;
}
inline bool PeerData::canWrite() const {
	return isChannel()
		? asChannel()->canWrite()
		: isChat()
			? asChat()->canWrite()
			: isUser()
				? asUser()->canWrite()
				: false;
}
