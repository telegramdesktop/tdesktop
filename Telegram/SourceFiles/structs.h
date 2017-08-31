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

using MediaKey = QPair<uint64, uint64>;

inline uint64 mediaMix32To64(int32 a, int32 b) {
	return (uint64(*reinterpret_cast<uint32*>(&a)) << 32) | uint64(*reinterpret_cast<uint32*>(&b));
}

enum LocationType {
	UnknownFileLocation = 0,
	// 1, 2, etc are used as "version" value in mediaKey() method.

	DocumentFileLocation = 0x4e45abe9, // mtpc_inputDocumentFileLocation
	AudioFileLocation = 0x74dc404d, // mtpc_inputAudioFileLocation
	VideoFileLocation = 0x3d0364ec, // mtpc_inputVideoFileLocation
};

// Old method, should not be used anymore.
//inline MediaKey mediaKey(LocationType type, int32 dc, const uint64 &id) {
//	return MediaKey(mediaMix32To64(type, dc), id);
//}
// New method when version was introduced, type is not relevant anymore (all files are Documents).
inline MediaKey mediaKey(LocationType type, int32 dc, const uint64 &id, int32 version) {
	return (version > 0) ? MediaKey(mediaMix32To64(version, dc), id) : MediaKey(mediaMix32To64(type, dc), id);
}

inline StorageKey mediaKey(const MTPDfileLocation &location) {
	return storageKey(location.vdc_id.v, location.vvolume_id.v, location.vlocal_id.v);
}

typedef int32 UserId;
typedef int32 ChatId;
typedef int32 ChannelId;
static const ChannelId NoChannel = 0;

typedef int32 MsgId;
struct FullMsgId {
	FullMsgId() = default;
	FullMsgId(ChannelId channel, MsgId msg) : channel(channel), msg(msg) {
	}
	ChannelId channel = NoChannel;
	MsgId msg = 0;
};

typedef uint64 PeerId;
static const uint64 PeerIdMask         = 0xFFFFFFFFULL;
static const uint64 PeerIdTypeMask     = 0x300000000ULL;
static const uint64 PeerIdUserShift    = 0x000000000ULL;
static const uint64 PeerIdChatShift    = 0x100000000ULL;
static const uint64 PeerIdChannelShift = 0x200000000ULL;
inline bool peerIsUser(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdUserShift;
}
inline bool peerIsChat(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdChatShift;
}
inline bool peerIsChannel(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdChannelShift;
}
inline PeerId peerFromUser(UserId user_id) {
	return PeerIdUserShift | uint64(uint32(user_id));
}
inline PeerId peerFromChat(ChatId chat_id) {
	return PeerIdChatShift | uint64(uint32(chat_id));
}
inline PeerId peerFromChannel(ChannelId channel_id) {
	return PeerIdChannelShift | uint64(uint32(channel_id));
}
inline PeerId peerFromUser(const MTPint &user_id) {
	return peerFromUser(user_id.v);
}
inline PeerId peerFromChat(const MTPint &chat_id) {
	return peerFromChat(chat_id.v);
}
inline PeerId peerFromChannel(const MTPint &channel_id) {
	return peerFromChannel(channel_id.v);
}
inline int32 peerToBareInt(const PeerId &id) {
	return int32(uint32(id & PeerIdMask));
}
inline UserId peerToUser(const PeerId &id) {
	return peerIsUser(id) ? peerToBareInt(id) : 0;
}
inline ChatId peerToChat(const PeerId &id) {
	return peerIsChat(id) ? peerToBareInt(id) : 0;
}
inline ChannelId peerToChannel(const PeerId &id) {
	return peerIsChannel(id) ? peerToBareInt(id) : NoChannel;
}
inline MTPint peerToBareMTPInt(const PeerId &id) {
	return MTP_int(peerToBareInt(id));
}
inline PeerId peerFromMTP(const MTPPeer &peer) {
	switch (peer.type()) {
	case mtpc_peerUser: return peerFromUser(peer.c_peerUser().vuser_id);
	case mtpc_peerChat: return peerFromChat(peer.c_peerChat().vchat_id);
	case mtpc_peerChannel: return peerFromChannel(peer.c_peerChannel().vchannel_id);
	}
	return 0;
}
inline MTPpeer peerToMTP(const PeerId &id) {
	if (peerIsUser(id)) {
		return MTP_peerUser(peerToBareMTPInt(id));
	} else if (peerIsChat(id)) {
		return MTP_peerChat(peerToBareMTPInt(id));
	} else if (peerIsChannel(id)) {
		return MTP_peerChannel(peerToBareMTPInt(id));
	}
	return MTP_peerUser(MTP_int(0));
}
inline PeerId peerFromMessage(const MTPmessage &msg) {
	auto compute = [](auto &message) {
		auto from_id = message.has_from_id() ? peerFromUser(message.vfrom_id) : 0;
		auto to_id = peerFromMTP(message.vto_id);
		auto out = message.is_out();
		return (out || !peerIsUser(to_id)) ? to_id : from_id;
	};
	switch (msg.type()) {
	case mtpc_message: return compute(msg.c_message());
	case mtpc_messageService: return compute(msg.c_messageService());
	}
	return 0;
}
inline MTPDmessage::Flags flagsFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_message: return msg.c_message().vflags.v;
	case mtpc_messageService: return mtpCastFlags(msg.c_messageService().vflags.v);
	}
	return 0;
}
inline MsgId idFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_messageEmpty: return msg.c_messageEmpty().vid.v;
	case mtpc_message: return msg.c_message().vid.v;
	case mtpc_messageService: return msg.c_messageService().vid.v;
	}
	Unexpected("Type in idFromMessage()");
}
inline TimeId dateFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_message: return msg.c_message().vdate.v;
	case mtpc_messageService: return msg.c_messageService().vdate.v;
	}
	return 0;
}

using PhotoId = uint64;
using VideoId = uint64;
using AudioId = uint64;
using DocumentId = uint64;
using WebPageId = uint64;
using GameId = uint64;
static const WebPageId CancelledWebPageId = 0xFFFFFFFFFFFFFFFFULL;

inline bool operator==(const FullMsgId &a, const FullMsgId &b) {
	return (a.channel == b.channel) && (a.msg == b.msg);
}
inline bool operator!=(const FullMsgId &a, const FullMsgId &b) {
	return !(a == b);
}
inline bool operator<(const FullMsgId &a, const FullMsgId &b) {
	if (a.msg < b.msg) return true;
	if (a.msg > b.msg) return false;
	return a.channel < b.channel;
}

constexpr const MsgId StartClientMsgId = -0x7FFFFFFF;
constexpr const MsgId EndClientMsgId = -0x40000000;
inline constexpr bool isClientMsgId(MsgId id) {
	return id >= StartClientMsgId && id < EndClientMsgId;
}
constexpr const MsgId ShowAtTheEndMsgId = -0x40000000;
constexpr const MsgId SwitchAtTopMsgId = -0x3FFFFFFF;
constexpr const MsgId ShowAtProfileMsgId = -0x3FFFFFFE;
constexpr const MsgId ShowAndStartBotMsgId = -0x3FFFFFD;
constexpr const MsgId ShowAtGameShareMsgId = -0x3FFFFFC;
constexpr const MsgId ServerMaxMsgId = 0x3FFFFFFF;
constexpr const MsgId ShowAtUnreadMsgId = 0;

struct NotifySettings {
	NotifySettings() : flags(MTPDpeerNotifySettings::Flag::f_show_previews), sound(qsl("default")) {
	}
	MTPDpeerNotifySettings::Flags flags;
	TimeId mute = 0;
	QString sound;
	bool previews() const {
		return flags & MTPDpeerNotifySettings::Flag::f_show_previews;
	}
	bool silent() const {
		return flags & MTPDpeerNotifySettings::Flag::f_silent;
	}
};
typedef NotifySettings *NotifySettingsPtr;

static const NotifySettingsPtr UnknownNotifySettings = NotifySettingsPtr(0);
static const NotifySettingsPtr EmptyNotifySettings = NotifySettingsPtr(1);
extern NotifySettings globalNotifyAll, globalNotifyUsers, globalNotifyChats;
extern NotifySettingsPtr globalNotifyAllPtr, globalNotifyUsersPtr, globalNotifyChatsPtr;

inline bool isNotifyMuted(NotifySettingsPtr settings, TimeId *changeIn = nullptr) {
	if (settings != UnknownNotifySettings && settings != EmptyNotifySettings) {
		auto t = unixtime();
		if (settings->mute > t) {
			if (changeIn) *changeIn = settings->mute - t + 1;
			return true;
		}
	}
	if (changeIn) *changeIn = 0;
	return false;
}

static constexpr int kUserColorsCount = 8;
static constexpr int kChatColorsCount = 4;
static constexpr int kChannelColorsCount = 4;

class EmptyUserpic {
public:
	EmptyUserpic();
	EmptyUserpic(int index, const QString &name);

	void set(int index, const QString &name);
	void clear();

	explicit operator bool() const {
		return (_impl != nullptr);
	}

	void paint(Painter &p, int x, int y, int outerWidth, int size) const;
	void paintRounded(Painter &p, int x, int y, int outerWidth, int size) const;
	void paintSquare(Painter &p, int x, int y, int outerWidth, int size) const;
	QPixmap generate(int size);
	StorageKey uniqueKey() const;

	~EmptyUserpic();

private:
	class Impl;
	std::unique_ptr<Impl> _impl;
	friend class Impl;

};

static const PhotoId UnknownPeerPhotoId = 0xFFFFFFFFFFFFFFFFULL;

inline const QString &emptyUsername() {
	static QString empty;
	return empty;
}

class PeerData;

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

class UserData;
class ChatData;
class ChannelData;

class PeerData {
protected:
	PeerData(const PeerId &id);
	PeerData(const PeerData &other) = delete;
	PeerData &operator=(const PeerData &other) = delete;

public:
	virtual ~PeerData() {
		if (notify != UnknownNotifySettings && notify != EmptyNotifySettings) {
			delete base::take(notify);
		}
	}

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
	bool isMuted() const {
		return (notify != EmptyNotifySettings) && (notify != UnknownNotifySettings) && (notify->mute >= unixtime());
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
	const QString &userName() const;

	const PeerId id;
	int32 bareId() const {
		return int32(uint32(id & 0xFFFFFFFFULL));
	}

	QString name;
	Text nameText;
	using Names = OrderedSet<QString>;
	Names names; // for filtering
	using NameFirstChars = OrderedSet<QChar>;
	NameFirstChars chars;

	enum LoadedStatus {
		NotLoaded = 0x00,
		MinimalLoaded = 0x01,
		FullLoaded = 0x02,
	};
	LoadedStatus loadedStatus = NotLoaded;
	MTPinputPeer input;

	int colorIndex() const {
		return _colorIndex;
	}
	void setUserpic(ImagePtr userpic);
	void paintUserpic(Painter &p, int x, int y, int size) const;
	void paintUserpicLeft(Painter &p, int x, int y, int w, int size) const {
		paintUserpic(p, rtl() ? (w - x - size) : x, y, size);
	}
	void paintUserpicRounded(Painter &p, int x, int y, int size) const;
	void paintUserpicSquare(Painter &p, int x, int y, int size) const;
	void loadUserpic(bool loadFirst = false, bool prior = true) {
		_userpic->load(loadFirst, prior);
	}
	bool userpicLoaded() const {
		return _userpic->loaded();
	}
	StorageKey userpicUniqueKey() const;
	void saveUserpic(const QString &path, int size) const;
	void saveUserpicRounded(const QString &path, int size) const;
	QPixmap genUserpic(int size) const;
	QPixmap genUserpicRounded(int size) const;

	PhotoId photoId = UnknownPeerPhotoId;
	StorageImageLocation photoLoc;

	int nameVersion = 1;

	NotifySettingsPtr notify = UnknownNotifySettings;

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
	void updateNameDelayed(const QString &newName, const QString &newNameOrPhone, const QString &newUsername);

	ImagePtr _userpic;
	mutable EmptyUserpic _userpicEmpty;

private:
	void fillNames();

	ClickHandlerPtr _openLink;

	int _colorIndex = 0;
	TimeMs _lastFullUpdate = 0;

};

class BotCommand {
public:
	BotCommand(const QString &command, const QString &description) : command(command), _description(description) {
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

class PhotoData;
class UserData : public PeerData {
public:
	UserData(const PeerId &id) : PeerData(id) {
	}
	void setPhoto(const MTPUserProfilePhoto &photo);

	void setName(const QString &newFirstName, const QString &newLastName
		, const QString &newPhoneName, const QString &newUsername);

	void setPhone(const QString &newPhone);
	void setBotInfoVersion(int version);
	void setBotInfo(const MTPBotInfo &info);

	void setNameOrPhone(const QString &newNameOrPhone);

	void madeAction(TimeId when); // pseudo-online

	uint64 access = 0;

	MTPDuser::Flags flags = 0;
	bool isVerified() const {
		return flags & MTPDuser::Flag::f_verified;
	}
	bool isBotInlineGeo() const {
		return flags & MTPDuser::Flag::f_bot_inline_geo;
	}
	bool isInaccessible() const {
		return (access == NoAccess);
	}
	void setIsInaccessible() {
		access = NoAccess;
	}
	bool canWrite() const {
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

	typedef QList<PhotoData*> Photos;
	Photos photos;
	int photosCount = -1; // -1 not loaded, 0 all loaded

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
	QString _restrictionReason;
	QString _about;
	QString _phone;
	BlockStatus _blockStatus = BlockStatus::Unknown;
	CallsStatus _callsStatus = CallsStatus::Unknown;
	int _commonChatsCount = 0;

	static constexpr const uint64 NoAccess = 0xFFFFFFFFFFFFFFFFULL;

};

class ChatData : public PeerData {
public:
	ChatData(const PeerId &id) : PeerData(id), inputChat(MTP_int(bareId())) {
	}
	void setPhoto(const MTPChatPhoto &photo, const PhotoId &phId = UnknownPeerPhotoId);

	void setName(const QString &newName);

	void invalidateParticipants();
	bool noParticipantInfo() const {
		return (count > 0 || amIn()) && participants.isEmpty();
	}

	MTPint inputChat;

	ChannelData *migrateToPtr = nullptr;

	int count = 0;
	TimeId date = 0;
	int version = 0;
	UserId creator = 0;

	MTPDchat::Flags flags = 0;
	bool isForbidden() const {
		return _isForbidden;
	}
	void setIsForbidden(bool forbidden) {
		_isForbidden = forbidden;
	}
	bool amIn() const {
		return !isForbidden() && !haveLeft() && !wasKicked();
	}
	bool canEdit() const {
		return !isDeactivated() && (amCreator() || (adminsEnabled() ? amAdmin() : amIn()));
	}
	bool canWrite() const {
		return !isDeactivated() && amIn();
	}
	bool haveLeft() const {
		return flags & MTPDchat::Flag::f_left;
	}
	bool wasKicked() const {
		return flags & MTPDchat::Flag::f_kicked;
	}
	bool adminsEnabled() const {
		return flags & MTPDchat::Flag::f_admins_enabled;
	}
	bool amCreator() const {
		return flags & MTPDchat::Flag::f_creator;
	}
	bool amAdmin() const {
		return (flags & MTPDchat::Flag::f_admin) && adminsEnabled();
	}
	bool isDeactivated() const {
		return flags & MTPDchat::Flag::f_deactivated;
	}
	bool isMigrated() const {
		return flags & MTPDchat::Flag::f_migrated_to;
	}
	QMap<not_null<UserData*>, int> participants;
	OrderedSet<not_null<UserData*>> invitedByMe;
	OrderedSet<not_null<UserData*>> admins;
	QList<not_null<UserData*>> lastAuthors;
	OrderedSet<not_null<PeerData*>> markupSenders;
	int botStatus = 0; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other
//	ImagePtr photoFull;

	void setInviteLink(const QString &newInviteLink);
	QString inviteLink() const {
		return _inviteLink;
	}

private:
	bool _isForbidden = false;
	QString _inviteLink;

};

enum PtsSkippedQueue {
	SkippedUpdate,
	SkippedUpdates,
};
class PtsWaiter {
public:

	PtsWaiter()
		: _good(0)
		, _last(0)
		, _count(0)
		, _applySkippedLevel(0)
		, _requesting(false)
		, _waitingForSkipped(false)
		, _waitingForShortPoll(false) {
	}
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
	bool updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates);
	bool updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update);
	bool updated(ChannelData *channel, int32 pts, int32 count);
	bool updateAndApply(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates);
	bool updateAndApply(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update);
	bool updateAndApply(ChannelData *channel, int32 pts, int32 count);
	void applySkippedUpdates(ChannelData *channel);
	void clearSkippedUpdates();

private:
	bool check(ChannelData *channel, int32 pts, int32 count); // return false if need to save that update and apply later
	uint64 ptsKey(PtsSkippedQueue queue, int32 pts);
	void checkForWaiting(ChannelData *channel);
	QMap<uint64, PtsSkippedQueue> _queue;
	QMap<uint64, MTPUpdate> _updateQueue;
	QMap<uint64, MTPUpdates> _updatesQueue;
	int32 _good, _last, _count;
	int32 _applySkippedLevel;
	bool _requesting, _waitingForSkipped, _waitingForShortPoll;
	uint32 _skippedKey = 0;
};

struct MegagroupInfo {
	struct Admin {
		explicit Admin(MTPChannelAdminRights rights) : rights(rights) {
		}
		Admin(MTPChannelAdminRights rights, bool canEdit) : rights(rights), canEdit(canEdit) {
		}
		MTPChannelAdminRights rights;
		bool canEdit = false;
	};
	struct Restricted {
		explicit Restricted(MTPChannelBannedRights rights) : rights(rights) {
		}
		MTPChannelBannedRights rights;
	};
	QList<not_null<UserData*>> lastParticipants;
	QMap<not_null<UserData*>, Admin> lastAdmins;
	QMap<not_null<UserData*>, Restricted> lastRestricted;
	OrderedSet<not_null<PeerData*>> markupSenders;
	OrderedSet<not_null<UserData*>> bots;

	UserData *creator = nullptr; // nullptr means unknown
	int botStatus = 0; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other
	MsgId pinnedMsgId = 0;
	bool joinedMessageFound = false;
	MTPInputStickerSet stickerSet = MTP_inputStickerSetEmpty();

	enum LastParticipantsStatus {
		LastParticipantsUpToDate       = 0x00,
		LastParticipantsAdminsOutdated = 0x01,
		LastParticipantsCountOutdated  = 0x02,
	};
	mutable int lastParticipantsStatus = LastParticipantsUpToDate;
	int lastParticipantsCount = 0;

	ChatData *migrateFromPtr = nullptr;

};

class ChannelData : public PeerData {
public:
	ChannelData(const PeerId &id) : PeerData(id), inputChannel(MTP_inputChannel(MTP_int(bareId()), MTP_long(0))) {
	}
	void setPhoto(const MTPChatPhoto &photo, const PhotoId &phId = UnknownPeerPhotoId);

	void setName(const QString &name, const QString &username);

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
		return flags & MTPDchannel::Flag::f_left;
	}
	bool amIn() const {
		return !isForbidden() && !haveLeft();
	}
	bool addsSignature() const {
		return flags & MTPDchannel::Flag::f_signatures;
	}
	bool isForbidden() const {
		return _isForbidden;
	}
	void setIsForbidden(bool forbidden) {
		_isForbidden = forbidden;
	}
	bool isVerified() const {
		return flags & MTPDchannel::Flag::f_verified;
	}

	static MTPChannelBannedRights KickedRestrictedRights();
	static constexpr auto kRestrictUntilForever = TimeId(INT_MAX);
	static bool IsRestrictedForever(TimeId until) {
		return !until || (until == kRestrictUntilForever);
	}
	void applyEditAdmin(not_null<UserData*> user, const MTPChannelAdminRights &oldRights, const MTPChannelAdminRights &newRights);
	void applyEditBanned(not_null<UserData*> user, const MTPChannelBannedRights &oldRights, const MTPChannelBannedRights &newRights);

	int32 date = 0;
	int version = 0;
	MTPDchannel::Flags flags = 0;
	MTPDchannelFull::Flags flagsFull = 0;
	std::unique_ptr<MegagroupInfo> mgInfo;
	bool lastParticipantsCountOutdated() const {
		if (!mgInfo || !(mgInfo->lastParticipantsStatus & MegagroupInfo::LastParticipantsCountOutdated)) {
			return false;
		}
		if (mgInfo->lastParticipantsCount == membersCount()) {
			mgInfo->lastParticipantsStatus &= ~MegagroupInfo::LastParticipantsCountOutdated;
			return false;
		}
		return true;
	}
	void flagsUpdated();
	bool isMegagroup() const {
		return flags & MTPDchannel::Flag::f_megagroup;
	}
	bool isBroadcast() const {
		return flags & MTPDchannel::Flag::f_broadcast;
	}
	bool isPublic() const {
		return flags & MTPDchannel::Flag::f_username;
	}
	bool amCreator() const {
		return flags & MTPDchannel::Flag::f_creator;
	}
	const MTPChannelAdminRights &adminRightsBoxed() const {
		return _adminRights;
	}
	const MTPDchannelAdminRights &adminRights() const {
		return _adminRights.c_channelAdminRights();
	}
	void setAdminRights(const MTPChannelAdminRights &rights);
	bool hasAdminRights() const {
		return (adminRights().vflags.v != 0);
	}
	const MTPChannelBannedRights &restrictedRightsBoxed() const {
		return _restrictedRights;
	}
	const MTPDchannelBannedRights &restrictedRights() const {
		return _restrictedRights.c_channelBannedRights();
	}
	void setRestrictedRights(const MTPChannelBannedRights &rights);
	bool hasRestrictedRights() const {
		return (restrictedRights().vflags.v != 0);
	}
	bool hasRestrictedRights(int32 now) const {
		return hasRestrictedRights() && (restrictedRights().vuntil_date.v > now);
	}
	bool canBanMembers() const {
		return adminRights().is_ban_users() || amCreator();
	}
	bool canEditMessages() const {
		return adminRights().is_edit_messages() || amCreator();
	}
	bool canDeleteMessages() const {
		return adminRights().is_delete_messages() || amCreator();
	}
	bool anyoneCanAddMembers() const {
		return (flags & MTPDchannel::Flag::f_democracy);
	}
	bool canAddMembers() const {
		return adminRights().is_invite_users() || amCreator() || (anyoneCanAddMembers() && amIn() && !hasRestrictedRights());
	}
	bool canAddAdmins() const {
		return adminRights().is_add_admins() || amCreator();
	}
	bool canPinMessages() const {
		return adminRights().is_pin_messages() || amCreator();
	}
	bool canPublish() const {
		return adminRights().is_post_messages() || amCreator();
	}
	bool canWrite() const {
		return amIn() && (canPublish() || (!isBroadcast() && !restrictedRights().is_send_messages()));
	}
	bool canViewMembers() const {
		return flagsFull & MTPDchannelFull::Flag::f_can_view_participants;
	}
	bool canViewAdmins() const {
		return (isMegagroup() || hasAdminRights() || amCreator());
	}
	bool canViewBanned() const {
		return (hasAdminRights() || amCreator());
	}
	bool canEditInformation() const {
		return adminRights().is_change_info() || amCreator();
	}
	bool canEditUsername() const {
		return amCreator() && (flagsFull & MTPDchannelFull::Flag::f_can_set_username);
	}
	bool canEditStickers() const {
		return (flagsFull & MTPDchannelFull::Flag::f_can_set_stickers);
	}
	bool canDelete() const {
		constexpr auto kDeleteChannelMembersLimit = 1000;
		return amCreator() && (membersCount() <= kDeleteChannelMembersLimit);
	}
	bool canEditAdmin(not_null<UserData*> user) const;
	bool canRestrictUser(not_null<UserData*> user) const;

	void setInviteLink(const QString &newInviteLink);
	QString inviteLink() const {
		return _inviteLink;
	}
	bool canHaveInviteLink() const {
		return adminRights().is_invite_link() || amCreator();
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
	bool ptsUpdateAndApply(int32 pts, int32 count, const MTPUpdate &update) {
		return _ptsWaiter.updateAndApply(this, pts, count, update);
	}
	bool ptsUpdateAndApply(int32 pts, int32 count, const MTPUpdates &updates) {
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

private:
	bool canNotEditLastAdmin(not_null<UserData*> user) const;

	PtsWaiter _ptsWaiter;

	bool _isForbidden = true;
	int _membersCount = 1;
	int _adminsCount = 1;
	int _restrictedCount = 0;
	int _kickedCount = 0;

	MTPChannelAdminRights _adminRights = MTP_channelAdminRights(MTP_flags(0));
	MTPChannelBannedRights _restrictedRights = MTP_channelBannedRights(MTP_flags(0), MTP_int(0));

	QString _restrictionReason;
	QString _about;

	QString _inviteLink;

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
	return isChannel() ? static_cast<const ChannelData*>(this) : nullptr;
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
	return isMegagroup() ? static_cast<const ChannelData*>(this) : nullptr;
}
inline const ChannelData *asMegagroup(const PeerData *peer) {
	return peer ? peer->asMegagroup() : nullptr;
}
inline bool isMegagroup(const PeerData *peer) {
	return peer ? peer->isMegagroup() : false;
}
inline ChatData *PeerData::migrateFrom() const {
	return (isMegagroup() && asChannel()->amIn()) ? asChannel()->mgInfo->migrateFromPtr : nullptr;
}
inline ChannelData *PeerData::migrateTo() const {
	return (isChat() && asChat()->migrateToPtr && asChat()->migrateToPtr->amIn()) ? asChat()->migrateToPtr : nullptr;
}
inline const Text &PeerData::dialogName() const {
	return migrateTo() ? migrateTo()->dialogName() : ((isUser() && !asUser()->phoneText.isEmpty()) ? asUser()->phoneText : nameText);
}
inline const QString &PeerData::shortName() const {
	return isUser() ? asUser()->firstName : name;
}
inline const QString &PeerData::userName() const {
	return isUser() ? asUser()->username : (isChannel() ? asChannel()->username : emptyUsername());
}
inline bool PeerData::isVerified() const {
	return isUser() ? asUser()->isVerified() : (isChannel() ? asChannel()->isVerified() : false);
}
inline bool PeerData::isMegagroup() const {
	return isChannel() ? asChannel()->isMegagroup() : false;
}
inline bool PeerData::canWrite() const {
	return isChannel() ? asChannel()->canWrite() : (isChat() ? asChat()->canWrite() : (isUser() ? asUser()->canWrite() : false));
}

enum ActionOnLoad {
	ActionOnLoadNone,
	ActionOnLoadOpen,
	ActionOnLoadOpenWith,
	ActionOnLoadPlayInline
};

typedef QMap<char, QPixmap> PreparedPhotoThumbs;
class PhotoData {
public:
	PhotoData(const PhotoId &id, const uint64 &access = 0, int32 date = 0, const ImagePtr &thumb = ImagePtr(), const ImagePtr &medium = ImagePtr(), const ImagePtr &full = ImagePtr());

	void automaticLoad(const HistoryItem *item);
	void automaticLoadSettingsChanged();

	void download();
	bool loaded() const;
	bool loading() const;
	bool displayLoading() const;
	void cancel();
	float64 progress() const;
	int32 loadOffset() const;
	bool uploading() const;

	void forget();
	ImagePtr makeReplyPreview();

	PhotoId id;
	uint64 access;
	int32 date;
	ImagePtr thumb, replyPreview;
	ImagePtr medium;
	ImagePtr full;

	PeerData *peer = nullptr; // for chat and channel photos connection
	// geo, caption

	struct UploadingData {
		UploadingData(int size) : size(size) {
		}
		int offset = 0;
		int size = 0;
	};
	std::unique_ptr<UploadingData> uploadingData;

private:
	void notifyLayoutChanged() const;

};

class PhotoClickHandler : public LeftButtonClickHandler {
public:
	PhotoClickHandler(not_null<PhotoData*> photo, PeerData *peer = nullptr) : _photo(photo), _peer(peer) {
	}
	not_null<PhotoData*> photo() const {
		return _photo;
	}
	PeerData *peer() const {
		return _peer;
	}

private:
	not_null<PhotoData*> _photo;
	PeerData *_peer;

};

class PhotoOpenClickHandler : public PhotoClickHandler {
public:
	using PhotoClickHandler::PhotoClickHandler;
protected:
	void onClickImpl() const override;
};

class PhotoSaveClickHandler : public PhotoClickHandler {
public:
	using PhotoClickHandler::PhotoClickHandler;
protected:
	void onClickImpl() const override;
};

class PhotoCancelClickHandler : public PhotoClickHandler {
public:
	using PhotoClickHandler::PhotoClickHandler;
protected:
	void onClickImpl() const override;
};

enum FileStatus {
	FileDownloadFailed = -2,
	FileUploadFailed = -1,
	FileUploading = 0,
	FileReady = 1,
};

// Don't change the values. This type is used for serialization.
enum DocumentType {
	FileDocument       = 0,
	VideoDocument      = 1,
	SongDocument       = 2,
	StickerDocument    = 3,
	AnimatedDocument   = 4,
	VoiceDocument      = 5,
	RoundVideoDocument = 6,
};

struct DocumentAdditionalData {
	virtual ~DocumentAdditionalData() = default;

};

struct StickerData : public DocumentAdditionalData {
	ImagePtr img;
	QString alt;

	MTPInputStickerSet set = MTP_inputStickerSetEmpty();
	bool setInstalled() const;

	StorageImageLocation loc; // doc thumb location

};

struct SongData : public DocumentAdditionalData {
	int32 duration = 0;
	QString title, performer;

};

typedef QVector<char> VoiceWaveform; // [0] == -1 -- counting, [0] == -2 -- could not count
struct VoiceData : public DocumentAdditionalData {
	~VoiceData();

	int duration = 0;
	VoiceWaveform waveform;
	char wavemax = 0;
};

bool fileIsImage(const QString &name, const QString &mime);

namespace Serialize {
class Document;
} // namespace Serialize;

class DocumentData {
public:
	static DocumentData *create(DocumentId id);
	static DocumentData *create(DocumentId id, int32 dc, uint64 accessHash, int32 version, const QVector<MTPDocumentAttribute> &attributes);
	static DocumentData *create(DocumentId id, const QString &url, const QVector<MTPDocumentAttribute> &attributes);

	void setattributes(const QVector<MTPDocumentAttribute> &attributes);

	void automaticLoad(const HistoryItem *item); // auto load sticker or video
	void automaticLoadSettingsChanged();

	enum FilePathResolveType {
		FilePathResolveCached,
		FilePathResolveChecked,
		FilePathResolveSaveFromData,
		FilePathResolveSaveFromDataSilent,
	};
	bool loaded(FilePathResolveType type = FilePathResolveCached) const;
	bool loading() const;
	QString loadingFilePath() const;
	bool displayLoading() const;
	void save(const QString &toFile, ActionOnLoad action = ActionOnLoadNone, const FullMsgId &actionMsgId = FullMsgId(), LoadFromCloudSetting fromCloud = LoadFromCloudOrLocal, bool autoLoading = false);
	void cancel();
	float64 progress() const;
	int32 loadOffset() const;
	bool uploading() const;

	QByteArray data() const;
	const FileLocation &location(bool check = false) const;
	void setLocation(const FileLocation &loc);

	QString filepath(FilePathResolveType type = FilePathResolveCached, bool forceSavingAs = false) const;

	bool saveToCache() const;

	void performActionOnLoad();

	void forget();
	ImagePtr makeReplyPreview();

	StickerData *sticker() {
		return (type == StickerDocument) ? static_cast<StickerData*>(_additional.get()) : nullptr;
	}
	void checkSticker() {
		StickerData *s = sticker();
		if (!s) return;

		automaticLoad(nullptr);
		if (s->img->isNull() && loaded()) {
			if (_data.isEmpty()) {
				const FileLocation &loc(location(true));
				if (loc.accessEnable()) {
					s->img = ImagePtr(loc.name());
					loc.accessDisable();
				}
			} else {
				s->img = ImagePtr(_data);
			}
		}
	}
	SongData *song() {
		return (type == SongDocument) ? static_cast<SongData*>(_additional.get()) : nullptr;
	}
	const SongData *song() const {
		return const_cast<DocumentData*>(this)->song();
	}
	VoiceData *voice() {
		return (type == VoiceDocument) ? static_cast<VoiceData*>(_additional.get()) : nullptr;
	}
	const VoiceData *voice() const {
		return const_cast<DocumentData*>(this)->voice();
	}
	bool isRoundVideo() const {
		return (type == RoundVideoDocument);
	}
	bool isAnimation() const {
		return (type == AnimatedDocument) || isRoundVideo() || !mime.compare(qstr("image/gif"), Qt::CaseInsensitive);
	}
	bool isGifv() const {
		return (type == AnimatedDocument) && !mime.compare(qstr("video/mp4"), Qt::CaseInsensitive);
	}
	bool isTheme() const {
		return name.endsWith(qstr(".tdesktop-theme"), Qt::CaseInsensitive) || name.endsWith(qstr(".tdesktop-palette"), Qt::CaseInsensitive);
	}
	bool tryPlaySong() const {
		return (song() != nullptr) || mime.startsWith(qstr("audio/"), Qt::CaseInsensitive);
	}
	bool isMusic() const {
		if (auto s = song()) {
			return (s->duration > 0);
		}
		return false;
	}
	bool isVideo() const {
		return (type == VideoDocument);
	}
	int32 duration() const {
		return (isAnimation() || isVideo()) ? _duration : -1;
	}
	bool isImage() const {
		return !isAnimation() && !isVideo() && (_duration > 0);
	}
	void recountIsImage();
	void setData(const QByteArray &data) {
		_data = data;
	}

	bool setRemoteVersion(int32 version); // Returns true if version has changed.
	void setRemoteLocation(int32 dc, uint64 access);
	void setContentUrl(const QString &url);
	bool hasRemoteLocation() const {
		return (_dc != 0 && _access != 0);
	}
	bool isValid() const {
		return hasRemoteLocation() || !_url.isEmpty();
	}
	MTPInputDocument mtpInput() const {
		if (_access) {
			return MTP_inputDocument(MTP_long(id), MTP_long(_access));
		}
		return MTP_inputDocumentEmpty();
	}

	// When we have some client-side generated document
	// (for example for displaying an external inline bot result)
	// and it has downloaded data, we can collect that data from it
	// to (this) received from the server "same" document.
	void collectLocalData(DocumentData *local);

	~DocumentData();

	DocumentId id = 0;
	DocumentType type = FileDocument;
	QSize dimensions;
	int32 date = 0;
	QString name;
	QString mime;
	ImagePtr thumb, replyPreview;
	int32 size = 0;

	FileStatus status = FileReady;
	int32 uploadOffset = 0;

	int32 md5[8];

	MediaKey mediaKey() const {
		return ::mediaKey(locationType(), _dc, id, _version);
	}

	static QString composeNameString(const QString &filename, const QString &songTitle, const QString &songPerformer);
	QString composeNameString() const {
		if (auto songData = song()) {
			return composeNameString(name, songData->title, songData->performer);
		}
		return composeNameString(name, QString(), QString());
	}

private:
	DocumentData(DocumentId id, int32 dc, uint64 accessHash, int32 version, const QString &url, const QVector<MTPDocumentAttribute> &attributes);

	friend class Serialize::Document;

	LocationType locationType() const {
		return voice() ? AudioFileLocation : (isVideo() ? VideoFileLocation : DocumentFileLocation);
	}

	// Two types of location: from MTProto by dc+access+version or from web by url
	int32 _dc = 0;
	uint64 _access = 0;
	int32 _version = 0;
	QString _url;

	FileLocation _location;
	QByteArray _data;
	std::unique_ptr<DocumentAdditionalData> _additional;
	int32 _duration = -1;

	ActionOnLoad _actionOnLoad = ActionOnLoadNone;
	FullMsgId _actionOnLoadMsgId;
	mutable FileLoader *_loader = nullptr;

	void notifyLayoutChanged() const;

	void destroyLoaderDelayed(mtpFileLoader *newValue = nullptr) const;

};

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit);
QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform);

class AudioMsgId {
public:
	enum class Type {
		Unknown,
		Voice,
		Song,
		Video,
	};

	AudioMsgId() = default;
	AudioMsgId(DocumentData *audio, const FullMsgId &msgId, uint32 playId = 0) : _audio(audio), _contextId(msgId), _playId(playId) {
		setTypeFromAudio();
	}

	Type type() const {
		return _type;
	}
	DocumentData *audio() const {
		return _audio;
	}
	FullMsgId contextId() const {
		return _contextId;
	}
	uint32 playId() const {
		return _playId;
	}

	explicit operator bool() const {
		return _audio != nullptr;
	}

private:
	void setTypeFromAudio() {
		if (_audio->voice() || _audio->isRoundVideo()) {
			_type = Type::Voice;
		} else if (_audio->isVideo()) {
			_type = Type::Video;
		} else if (_audio->tryPlaySong()) {
			_type = Type::Song;
		} else {
			_type = Type::Unknown;
		}
	}

	DocumentData *_audio = nullptr;
	Type _type = Type::Unknown;
	FullMsgId _contextId;
	uint32 _playId = 0;

};

inline bool operator<(const AudioMsgId &a, const AudioMsgId &b) {
	if (quintptr(a.audio()) < quintptr(b.audio())) {
		return true;
	} else if (quintptr(b.audio()) < quintptr(a.audio())) {
		return false;
	} else if (a.contextId() < b.contextId()) {
		return true;
	} else if (b.contextId() < a.contextId()) {
		return false;
	}
	return (a.playId() < b.playId());
}
inline bool operator==(const AudioMsgId &a, const AudioMsgId &b) {
	return a.audio() == b.audio() && a.contextId() == b.contextId() && a.playId() == b.playId();
}
inline bool operator!=(const AudioMsgId &a, const AudioMsgId &b) {
	return !(a == b);
}

class DocumentClickHandler : public LeftButtonClickHandler {
public:
	DocumentClickHandler(DocumentData *document) : _document(document) {
	}
	DocumentData *document() const {
		return _document;
	}

private:
	DocumentData *_document;

};

class DocumentSaveClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
	static void doSave(DocumentData *document, bool forceSavingAs = false);
protected:
	void onClickImpl() const override;
};

class DocumentOpenClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
	static void doOpen(DocumentData *document, HistoryItem *context, ActionOnLoad action = ActionOnLoadOpen);
protected:
	void onClickImpl() const override;
};

class GifOpenClickHandler : public DocumentOpenClickHandler {
public:
	using DocumentOpenClickHandler::DocumentOpenClickHandler;
protected:
	void onClickImpl() const override;
};

class VoiceSeekClickHandler : public DocumentOpenClickHandler {
public:
	using DocumentOpenClickHandler::DocumentOpenClickHandler;
protected:
	void onClickImpl() const override {
	}
};

class DocumentCancelClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
protected:
	void onClickImpl() const override;
};

enum WebPageType {
	WebPagePhoto,
	WebPageVideo,
	WebPageProfile,
	WebPageArticle
};
inline WebPageType toWebPageType(const QString &type) {
	if (type == qstr("photo")) return WebPagePhoto;
	if (type == qstr("video")) return WebPageVideo;
	if (type == qstr("profile")) return WebPageProfile;
	return WebPageArticle;
}

struct WebPageData {
	WebPageData(const WebPageId &id, WebPageType type = WebPageArticle, const QString &url = QString(), const QString &displayUrl = QString(), const QString &siteName = QString(), const QString &title = QString(), const TextWithEntities &description = TextWithEntities(), DocumentData *doc = nullptr, PhotoData *photo = nullptr, int32 duration = 0, const QString &author = QString(), int32 pendingTill = -1);

	void forget() {
		if (document) document->forget();
		if (photo) photo->forget();
	}

	WebPageId id;
	WebPageType type;
	QString url, displayUrl, siteName, title;
	TextWithEntities description;
	int32 duration;
	QString author;
	PhotoData *photo;
	DocumentData *document;
	int32 pendingTill;

};

struct GameData {
	GameData(const GameId &id, const uint64 &accessHash = 0, const QString &shortName = QString(), const QString &title = QString(), const QString &description = QString(), PhotoData *photo = nullptr, DocumentData *doc = nullptr);

	void forget() {
		if (document) document->forget();
		if (photo) photo->forget();
	}

	GameId id;
	uint64 accessHash;
	QString shortName, title, description;
	PhotoData *photo;
	DocumentData *document;

};

QString saveFileName(const QString &title, const QString &filter, const QString &prefix, QString name, bool savingAs, const QDir &dir = QDir());
MsgId clientMsgId();

struct MessageCursor {
	MessageCursor() = default;
	MessageCursor(int position, int anchor, int scroll) : position(position), anchor(anchor), scroll(scroll) {
	}
	MessageCursor(const QTextEdit *edit) {
		fillFrom(edit);
	}
	void fillFrom(const QTextEdit *edit) {
		QTextCursor c = edit->textCursor();
		position = c.position();
		anchor = c.anchor();
		QScrollBar *s = edit->verticalScrollBar();
		scroll = (s && (s->value() != s->maximum())) ? s->value() : QFIXED_MAX;
	}
	void applyTo(QTextEdit *edit) {
		auto cursor = edit->textCursor();
		cursor.setPosition(anchor, QTextCursor::MoveAnchor);
		cursor.setPosition(position, QTextCursor::KeepAnchor);
		edit->setTextCursor(cursor);
		if (auto scrollbar = edit->verticalScrollBar()) {
			scrollbar->setValue(scroll);
		}
	}
	int position = 0;
	int anchor = 0;
	int scroll = QFIXED_MAX;

};

inline bool operator==(const MessageCursor &a, const MessageCursor &b) {
	return (a.position == b.position) && (a.anchor == b.anchor) && (a.scroll == b.scroll);
}

struct SendAction {
	enum class Type {
		Typing,
		RecordVideo,
		UploadVideo,
		RecordVoice,
		UploadVoice,
		RecordRound,
		UploadRound,
		UploadPhoto,
		UploadFile,
		ChooseLocation,
		ChooseContact,
		PlayGame,
	};
	SendAction(Type type, TimeMs until, int progress = 0) : type(type), until(until), progress(progress) {
	}
	Type type;
	TimeMs until;
	int progress;
};
