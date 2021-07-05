/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"
#include "data/data_flags.h"
#include "data/data_notify_settings.h"
#include "data/data_cloud_file.h"

class PeerData;
class UserData;
class ChatData;
class ChannelData;

enum class ChatAdminRight {
	ChangeInfo = (1 << 0),
	PostMessages = (1 << 1),
	EditMessages = (1 << 2),
	DeleteMessages = (1 << 3),
	BanUsers = (1 << 4),
	InviteUsers = (1 << 5),
	PinMessages = (1 << 7),
	AddAdmins = (1 << 9),
	Anonymous = (1 << 10),
	ManageCall = (1 << 11),
	Other = (1 << 12),
};
inline constexpr bool is_flag_type(ChatAdminRight) { return true; }
using ChatAdminRights = base::flags<ChatAdminRight>;

enum class ChatRestriction {
	ViewMessages = (1 << 0),
	SendMessages = (1 << 1),
	SendMedia = (1 << 2),
	SendStickers = (1 << 3),
	SendGifs = (1 << 4),
	SendGames = (1 << 5),
	SendInline = (1 << 6),
	EmbedLinks = (1 << 7),
	SendPolls = (1 << 8),
	ChangeInfo = (1 << 10),
	InviteUsers = (1 << 15),
	PinMessages = (1 << 17),
};
inline constexpr bool is_flag_type(ChatRestriction) { return true; }
using ChatRestrictions = base::flags<ChatRestriction>;

namespace Data {

[[nodiscard]] ChatAdminRights ChatAdminRightsFlags(
	const MTPChatAdminRights &rights);
[[nodiscard]] ChatRestrictions ChatBannedRightsFlags(
	const MTPChatBannedRights &rights);
[[nodiscard]] TimeId ChatBannedRightsUntilDate(
	const MTPChatBannedRights &rights);

} // namespace Data

struct ChatAdminRightsInfo {
	ChatAdminRightsInfo() = default;
	explicit ChatAdminRightsInfo(ChatAdminRights flags) : flags(flags) {
	}
	explicit ChatAdminRightsInfo(const MTPChatAdminRights &rights)
	: flags(Data::ChatAdminRightsFlags(rights)) {
	}

	ChatAdminRights flags;
};

struct ChatRestrictionsInfo {
	ChatRestrictionsInfo() = default;
	ChatRestrictionsInfo(ChatRestrictions flags, TimeId until)
	: flags(flags)
	, until(until) {
	}
	explicit ChatRestrictionsInfo(const MTPChatBannedRights &rights)
	: flags(Data::ChatBannedRightsFlags(rights))
	, until(Data::ChatBannedRightsUntilDate(rights)) {
	}

	ChatRestrictions flags;
	TimeId until = 0;
};

struct BotCommand {
	QString command;
	QString description;
};

namespace Ui {
class EmptyUserpic;
} // namespace Ui

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Data {

class Session;
class GroupCall;
class CloudImageView;

int PeerColorIndex(PeerId peerId);
int PeerColorIndex(BareId bareId);
style::color PeerUserpicColor(PeerId peerId);
PeerId FakePeerIdForJustName(const QString &name);

class RestrictionCheckResult {
public:
	[[nodiscard]] static RestrictionCheckResult Allowed() {
		return { 0 };
	}
	[[nodiscard]] static RestrictionCheckResult WithEveryone() {
		return { 1 };
	}
	[[nodiscard]] static RestrictionCheckResult Explicit() {
		return { 2 };
	}

	explicit operator bool() const {
		return (_value != 0);
	}

	bool operator==(const RestrictionCheckResult &other) const {
		return (_value == other._value);
	}
	bool operator!=(const RestrictionCheckResult &other) const {
		return !(*this == other);
	}

	[[nodiscard]] bool isAllowed() const {
		return (*this == Allowed());
	}
	[[nodiscard]] bool isWithEveryone() const {
		return (*this == WithEveryone());
	}
	[[nodiscard]] bool isExplicit() const {
		return (*this == Explicit());
	}

private:
	RestrictionCheckResult(int value) : _value(value) {
	}

	int _value = 0;

};

struct UnavailableReason {
	QString reason;
	QString text;

	bool operator==(const UnavailableReason &other) const {
		return (reason == other.reason) && (text == other.text);
	}
	bool operator!=(const UnavailableReason &other) const {
		return !(*this == other);
	}
};

bool UpdateBotCommands(
	std::vector<BotCommand> &commands,
	const MTPVector<MTPBotCommand> &data);
bool UpdateBotCommands(
	base::flat_map<UserId, std::vector<BotCommand>> &commands,
	UserId botId,
	const MTPVector<MTPBotCommand> &data);
bool UpdateBotCommands(
	base::flat_map<UserId, std::vector<BotCommand>> &commands,
	const MTPVector<MTPBotInfo> &data);

} // namespace Data

class PeerClickHandler : public ClickHandler {
public:
	PeerClickHandler(not_null<PeerData*> peer);
	void onClick(ClickContext context) const override;

	not_null<PeerData*> peer() const {
		return _peer;
	}

private:
	not_null<PeerData*> _peer;

};

enum class PeerSetting {
	ReportSpam = (1 << 0),
	AddContact = (1 << 1),
	BlockContact = (1 << 2),
	ShareContact = (1 << 3),
	NeedContactsException = (1 << 4),
	AutoArchived = (1 << 5),
	Unknown = (1 << 6),
};
inline constexpr bool is_flag_type(PeerSetting) { return true; };
using PeerSettings = base::flags<PeerSetting>;

class PeerData {
protected:
	PeerData(not_null<Data::Session*> owner, PeerId id);
	PeerData(const PeerData &other) = delete;
	PeerData &operator=(const PeerData &other) = delete;

public:
	using Settings = Data::Flags<PeerSettings>;

	virtual ~PeerData();

	static constexpr auto kServiceNotificationsId = peerFromUser(777000);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] Main::Account &account() const;

	[[nodiscard]] bool isUser() const {
		return peerIsUser(id);
	}
	[[nodiscard]] bool isChat() const {
		return peerIsChat(id);
	}
	[[nodiscard]] bool isChannel() const {
		return peerIsChannel(id);
	}
	[[nodiscard]] bool isSelf() const {
		return (input.type() == mtpc_inputPeerSelf);
	}
	[[nodiscard]] bool isVerified() const;
	[[nodiscard]] bool isScam() const;
	[[nodiscard]] bool isFake() const;
	[[nodiscard]] bool isMegagroup() const;
	[[nodiscard]] bool isBroadcast() const;
	[[nodiscard]] bool isGigagroup() const;
	[[nodiscard]] bool isRepliesChat() const;
	[[nodiscard]] bool sharedMediaInfo() const {
		return isSelf() || isRepliesChat();
	}

	[[nodiscard]] bool isNotificationsUser() const {
		return (id == peerFromUser(333000))
			|| (id == kServiceNotificationsId);
	}
	[[nodiscard]] bool isServiceUser() const {
		return isUser() && !(id.value % 1000);
	}

	[[nodiscard]] std::optional<TimeId> notifyMuteUntil() const {
		return _notify.muteUntil();
	}
	bool notifyChange(const MTPPeerNotifySettings &settings) {
		return _notify.change(settings);
	}
	bool notifyChange(
			std::optional<int> muteForSeconds,
			std::optional<bool> silentPosts) {
		return _notify.change(muteForSeconds, silentPosts);
	}
	[[nodiscard]] bool notifySettingsUnknown() const {
		return _notify.settingsUnknown();
	}
	[[nodiscard]] std::optional<bool> notifySilentPosts() const {
		return _notify.silentPosts();
	}
	[[nodiscard]] MTPinputPeerNotifySettings notifySerialize() const {
		return _notify.serialize();
	}

	[[nodiscard]] bool canWrite() const;
	[[nodiscard]] Data::RestrictionCheckResult amRestricted(
		ChatRestriction right) const;
	[[nodiscard]] bool amAnonymous() const;
	[[nodiscard]] bool canRevokeFullHistory() const;
	[[nodiscard]] bool slowmodeApplied() const;
	[[nodiscard]] rpl::producer<bool> slowmodeAppliedValue() const;
	[[nodiscard]] int slowmodeSecondsLeft() const;
	[[nodiscard]] bool canSendPolls() const;
	[[nodiscard]] bool canManageGroupCall() const;

	[[nodiscard]] UserData *asUser();
	[[nodiscard]] const UserData *asUser() const;
	[[nodiscard]] ChatData *asChat();
	[[nodiscard]] const ChatData *asChat() const;
	[[nodiscard]] ChannelData *asChannel();
	[[nodiscard]] const ChannelData *asChannel() const;
	[[nodiscard]] ChannelData *asMegagroup();
	[[nodiscard]] const ChannelData *asMegagroup() const;
	[[nodiscard]] ChannelData *asBroadcast();
	[[nodiscard]] const ChannelData *asBroadcast() const;
	[[nodiscard]] ChatData *asChatNotMigrated();
	[[nodiscard]] const ChatData *asChatNotMigrated() const;
	[[nodiscard]] ChannelData *asChannelOrMigrated();
	[[nodiscard]] const ChannelData *asChannelOrMigrated() const;

	[[nodiscard]] ChatData *migrateFrom() const;
	[[nodiscard]] ChannelData *migrateTo() const;
	[[nodiscard]] not_null<PeerData*> migrateToOrMe();
	[[nodiscard]] not_null<const PeerData*> migrateToOrMe() const;

	void updateFull();
	void updateFullForced();
	void fullUpdated();
	[[nodiscard]] bool wasFullUpdated() const {
		return (_lastFullUpdate != 0);
	}

	[[nodiscard]] const Ui::Text::String &nameText() const;
	[[nodiscard]] const QString &shortName() const;
	[[nodiscard]] const Ui::Text::String &topBarNameText() const;
	[[nodiscard]] QString userName() const;

	[[nodiscard]] const base::flat_set<QString> &nameWords() const {
		return _nameWords;
	}
	[[nodiscard]] const base::flat_set<QChar> &nameFirstLetters() const {
		return _nameFirstLetters;
	}

	void setUserpic(PhotoId photoId, const ImageLocation &location);
	void setUserpicPhoto(const MTPPhoto &data);
	void paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const;
	void paintUserpicLeft(
			Painter &p,
			std::shared_ptr<Data::CloudImageView> &view,
			int x,
			int y,
			int w,
			int size) const {
		paintUserpic(p, view, rtl() ? (w - x - size) : x, y, size);
	}
	void paintUserpicRounded(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const;
	void paintUserpicSquare(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const;
	void loadUserpic();
	[[nodiscard]] bool hasUserpic() const;
	[[nodiscard]] std::shared_ptr<Data::CloudImageView> activeUserpicView();
	[[nodiscard]] std::shared_ptr<Data::CloudImageView> createUserpicView();
	[[nodiscard]] bool useEmptyUserpic(
		std::shared_ptr<Data::CloudImageView> &view) const;
	[[nodiscard]] InMemoryKey userpicUniqueKey(
		std::shared_ptr<Data::CloudImageView> &view) const;
	void saveUserpic(
		std::shared_ptr<Data::CloudImageView> &view,
		const QString &path,
		int size) const;
	void saveUserpicRounded(
		std::shared_ptr<Data::CloudImageView> &view,
		const QString &path,
		int size) const;
	[[nodiscard]] QPixmap genUserpic(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) const;
	[[nodiscard]] QPixmap genUserpicRounded(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) const;
	[[nodiscard]] ImageLocation userpicLocation() const {
		return _userpic.location();
	}

	static constexpr auto kUnknownPhotoId = PhotoId(0xFFFFFFFFFFFFFFFFULL);
	[[nodiscard]] bool userpicPhotoUnknown() const {
		return (_userpicPhotoId == kUnknownPhotoId);
	}
	[[nodiscard]] PhotoId userpicPhotoId() const {
		return userpicPhotoUnknown() ? 0 : _userpicPhotoId;
	}
	[[nodiscard]] Data::FileOrigin userpicOrigin() const;
	[[nodiscard]] Data::FileOrigin userpicPhotoOrigin() const;

	// If this string is not empty we must not allow to open the
	// conversation and we must show this string instead.
	[[nodiscard]] QString computeUnavailableReason() const;

	[[nodiscard]] ClickHandlerPtr createOpenLink();
	[[nodiscard]] const ClickHandlerPtr &openLink() {
		if (!_openLink) {
			_openLink = createOpenLink();
		}
		return _openLink;
	}

	[[nodiscard]] Image *currentUserpic(
		std::shared_ptr<Data::CloudImageView> &view) const;

	[[nodiscard]] bool canPinMessages() const;
	[[nodiscard]] bool canEditMessagesIndefinitely() const;

	[[nodiscard]] bool hasPinnedMessages() const;
	void setHasPinnedMessages(bool has);

	[[nodiscard]] bool canExportChatHistory() const;

	// Returns true if about text was changed.
	bool setAbout(const QString &newAbout);
	const QString &about() const {
		return _about;
	}

	void checkFolder(FolderId folderId);

	void setSettings(PeerSettings which) {
		_settings.set(which);
	}
	auto settings() const {
		return (_settings.current() & PeerSetting::Unknown)
			? std::nullopt
			: std::make_optional(_settings.current());
	}
	auto settingsValue() const {
		return (_settings.current() & PeerSetting::Unknown)
			? _settings.changes()
			: (_settings.value() | rpl::type_erased());
	}

	void setSettings(const MTPPeerSettings &data);

	enum class BlockStatus : char {
		Unknown,
		Blocked,
		NotBlocked,
	};
	[[nodiscard]] BlockStatus blockStatus() const {
		return _blockStatus;
	}
	[[nodiscard]] bool isBlocked() const {
		return (blockStatus() == BlockStatus::Blocked);
	}
	void setIsBlocked(bool is);

	enum class LoadedStatus : char {
		Not,
		Minimal,
		Full,
	};
	[[nodiscard]] LoadedStatus loadedStatus() const {
		return _loadedStatus;
	}
	[[nodiscard]] bool isMinimalLoaded() const {
		return (loadedStatus() != LoadedStatus::Not);
	}
	[[nodiscard]] bool isFullLoaded() const {
		return (loadedStatus() == LoadedStatus::Full);
	}
	void setLoadedStatus(LoadedStatus status);

	[[nodiscard]] TimeId messagesTTL() const;
	void setMessagesTTL(TimeId period);

	[[nodiscard]] Data::GroupCall *groupCall() const;
	[[nodiscard]] PeerId groupCallDefaultJoinAs() const;

	const PeerId id;
	QString name;
	MTPinputPeer input = MTP_inputPeerEmpty();

	int nameVersion = 1;

protected:
	void updateNameDelayed(
		const QString &newName,
		const QString &newNameOrPhone,
		const QString &newUsername);
	void updateUserpic(PhotoId photoId, MTP::DcId dcId);
	void clearUserpic();

private:
	void fillNames();
	[[nodiscard]] not_null<Ui::EmptyUserpic*> ensureEmptyUserpic() const;
	[[nodiscard]] virtual auto unavailableReasons() const
		-> const std::vector<Data::UnavailableReason> &;

	void setUserpicChecked(PhotoId photoId, const ImageLocation &location);

	const not_null<Data::Session*> _owner;

	mutable Data::CloudImage _userpic;
	PhotoId _userpicPhotoId = kUnknownPhotoId;
	mutable std::unique_ptr<Ui::EmptyUserpic> _userpicEmpty;
	Ui::Text::String _nameText;

	Data::NotifySettings _notify;

	ClickHandlerPtr _openLink;
	base::flat_set<QString> _nameWords; // for filtering
	base::flat_set<QChar> _nameFirstLetters;

	crl::time _lastFullUpdate = 0;

	TimeId _ttlPeriod = 0;
	bool _hasPinnedMessages = false;

	Settings _settings = PeerSettings(PeerSetting::Unknown);
	BlockStatus _blockStatus = BlockStatus::Unknown;
	LoadedStatus _loadedStatus = LoadedStatus::Not;

	QString _about;

};

namespace Data {

std::vector<ChatRestrictions> ListOfRestrictions();

std::optional<QString> RestrictionError(
	not_null<PeerData*> peer,
	ChatRestriction restriction);

void SetTopPinnedMessageId(not_null<PeerData*> peer, MsgId messageId);
[[nodiscard]] FullMsgId ResolveTopPinnedId(
	not_null<PeerData*> peer,
	PeerData *migrated);
[[nodiscard]] FullMsgId ResolveMinPinnedId(
	not_null<PeerData*> peer,
	PeerData *migrated);
[[nodiscard]] std::optional<int> ResolvePinnedCount(
	not_null<PeerData*> peer,
	PeerData *migrated);

} // namespace Data
