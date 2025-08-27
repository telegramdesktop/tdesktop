/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/notify/data_peer_notify_settings.h"
#include "data/data_types.h"
#include "data/data_flags.h"
#include "data/data_cloud_file.h"
#include "data/data_peer_common.h"
#include "ui/userpic_view.h"

struct BotInfo;
class PeerData;
class UserData;
class ChatData;
class ChannelData;

enum class ChatRestriction;

namespace Ui {
class EmptyUserpic;
struct BotVerifyDetails;
} // namespace Ui

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Data {

class Forum;
class ForumTopic;
class Session;
class GroupCall;
class SavedMessages;
class SavedSublist;
struct ReactionId;
class WallPaper;

[[nodiscard]] uint8 DecideColorIndex(PeerId peerId);

// Must be used only for PeerColor-s.
[[nodiscard]] PeerId FakePeerIdForJustName(const QString &name);

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

	friend inline bool operator==(
		const UnavailableReason &,
		const UnavailableReason &) = default;

	[[nodiscard]] bool sensitive() const;
	[[nodiscard]] static UnavailableReason Sensitive();

	[[nodiscard]] static QString Compute(
		not_null<Main::Session*> session,
		const std::vector<UnavailableReason> &list);
	[[nodiscard]] static bool IgnoreSensitiveMark(
		not_null<Main::Session*> session);

	[[nodiscard]] static std::vector<UnavailableReason> Extract(
		const MTPvector<MTPRestrictionReason> *list);
};

bool ApplyBotMenuButton(
	not_null<BotInfo*> info,
	const MTPBotMenuButton *button);

enum class AllowedReactionsType : uchar {
	All,
	Default,
	Some,
};

struct AllowedReactions {
	std::vector<ReactionId> some;
	int maxCount = 0;
	AllowedReactionsType type = AllowedReactionsType::Some;
	bool paidEnabled = false;

	friend inline bool operator==(
		const AllowedReactions &,
		const AllowedReactions &) = default;
};

[[nodiscard]] AllowedReactions Parse(
	const MTPChatReactions &value,
	int maxCount,
	bool paidEnabled);
[[nodiscard]] PeerData *PeerFromInputMTP(
	not_null<Session*> owner,
	const MTPInputPeer &input);
[[nodiscard]] UserData *UserFromInputMTP(
	not_null<Session*> owner,
	const MTPInputUser &input);

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

enum class PeerBarSetting {
	ReportSpam = (1 << 0),
	AddContact = (1 << 1),
	BlockContact = (1 << 2),
	ShareContact = (1 << 3),
	NeedContactsException = (1 << 4),
	AutoArchived = (1 << 5),
	RequestChat = (1 << 6),
	RequestChatIsBroadcast = (1 << 7),
	HasBusinessBot = (1 << 8),
	BusinessBotPaused = (1 << 9),
	BusinessBotCanReply = (1 << 10),
	Unknown = (1 << 11),
};
inline constexpr bool is_flag_type(PeerBarSetting) { return true; };
using PeerBarSettings = base::flags<PeerBarSetting>;

struct PeerBarDetails {
	QString phoneCountryCode;
	int registrationDate = 0; // YYYYMM or 0, YYYY > 2012, MM > 0.
	TimeId nameChangeDate = 0;
	TimeId photoChangeDate = 0;
	QString requestChatTitle;
	TimeId requestChatDate;
	UserData *businessBot = nullptr;
	QString businessBotManageUrl;
	int paysPerMessage = 0;
};

struct PaintUserpicContext {
	QPoint position;
	int size = 0;
	Ui::PeerUserpicShape shape = Ui::PeerUserpicShape::Auto;
};

class PeerData {
protected:
	PeerData(not_null<Data::Session*> owner, PeerId id);
	PeerData(const PeerData &other) = delete;
	PeerData &operator=(const PeerData &other) = delete;

public:
	using BarSettings = Data::Flags<PeerBarSettings>;

	virtual ~PeerData();

	static constexpr auto kServiceNotificationsId = peerFromUser(777000);
	static constexpr auto kSavedHiddenAuthorId = peerFromUser(2666000);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] Main::Account &account() const;

	[[nodiscard]] uint8 colorIndex() const {
		return _colorIndex;
	}
	bool changeColorIndex(uint8 index);
	bool clearColorIndex();
	[[nodiscard]] DocumentId backgroundEmojiId() const;
	bool changeBackgroundEmojiId(DocumentId id);

	void setEmojiStatus(const MTPEmojiStatus &status);
	void setEmojiStatus(EmojiStatusId emojiStatusId, TimeId until = 0);
	[[nodiscard]] EmojiStatusId emojiStatusId() const;

	[[nodiscard]] bool isUser() const {
		return peerIsUser(id);
	}
	[[nodiscard]] bool isChat() const {
		return peerIsChat(id);
	}
	[[nodiscard]] bool isChannel() const {
		return peerIsChannel(id);
	}
	[[nodiscard]] bool isBot() const;
	[[nodiscard]] bool isSelf() const;
	[[nodiscard]] bool isVerified() const;
	[[nodiscard]] bool isPremium() const;
	[[nodiscard]] bool isScam() const;
	[[nodiscard]] bool isFake() const;
	[[nodiscard]] bool isMegagroup() const;
	[[nodiscard]] bool isBroadcast() const;
	[[nodiscard]] bool isForum() const;
	[[nodiscard]] bool isMonoforum() const;
	[[nodiscard]] bool isGigagroup() const;
	[[nodiscard]] bool isRepliesChat() const;
	[[nodiscard]] bool isVerifyCodes() const;
	[[nodiscard]] bool isFreezeAppealChat() const;
	[[nodiscard]] bool sharedMediaInfo() const;
	[[nodiscard]] bool savedSublistsInfo() const;
	[[nodiscard]] bool hasStoriesHidden() const;
	void setStoriesHidden(bool hidden);

	[[nodiscard]] Ui::BotVerifyDetails *botVerifyDetails() const;

	[[nodiscard]] bool isNotificationsUser() const {
		return (id == peerFromUser(333000))
			|| (id == kServiceNotificationsId);
	}
	[[nodiscard]] bool isServiceUser() const {
		return isUser() && !(id.value % 1000);
	}
	[[nodiscard]] bool isSavedHiddenAuthor() const {
		return (id == kSavedHiddenAuthorId);
	}

	[[nodiscard]] Data::Forum *forum() const;
	[[nodiscard]] Data::ForumTopic *forumTopicFor(MsgId rootId) const;

	[[nodiscard]] Data::SavedMessages *monoforum() const;
	[[nodiscard]] Data::SavedSublist *monoforumSublistFor(
		PeerId sublistPeerId) const;

	[[nodiscard]] Data::PeerNotifySettings &notify() {
		return _notify;
	}
	[[nodiscard]] const Data::PeerNotifySettings &notify() const {
		return _notify;
	}

	[[nodiscard]] bool allowsForwarding() const;
	[[nodiscard]] Data::RestrictionCheckResult amRestricted(
		ChatRestriction right) const;
	[[nodiscard]] bool amAnonymous() const;
	[[nodiscard]] bool canRevokeFullHistory() const;
	[[nodiscard]] bool slowmodeApplied() const;
	[[nodiscard]] rpl::producer<bool> slowmodeAppliedValue() const;
	[[nodiscard]] int slowmodeSecondsLeft() const;
	[[nodiscard]] bool canManageGroupCall() const;
	[[nodiscard]] bool amMonoforumAdmin() const;

	[[nodiscard]] int starsPerMessage() const;
	[[nodiscard]] int starsPerMessageChecked() const;
	[[nodiscard]] Data::StarsRating starsRating() const;

	[[nodiscard]] UserData *asBot();
	[[nodiscard]] const UserData *asBot() const;
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
	[[nodiscard]] ChannelData *asMonoforum();
	[[nodiscard]] const ChannelData *asMonoforum() const;

	[[nodiscard]] ChatData *migrateFrom() const;
	[[nodiscard]] ChannelData *migrateTo() const;
	[[nodiscard]] not_null<PeerData*> migrateToOrMe();
	[[nodiscard]] not_null<const PeerData*> migrateToOrMe() const;
	[[nodiscard]] not_null<PeerData*> userpicPaintingPeer();
	[[nodiscard]] not_null<const PeerData*> userpicPaintingPeer() const;
	[[nodiscard]] Ui::PeerUserpicShape userpicShape() const;

	// isMonoforum() ? monoforumLink() : nullptr
	[[nodiscard]] ChannelData *monoforumBroadcast() const;

	// isMonoforum() ? nullptr : monoforumLink()
	[[nodiscard]] ChannelData *broadcastMonoforum() const;

	void updateFull();
	void updateFullForced();
	void fullUpdated();
	[[nodiscard]] bool wasFullUpdated() const {
		return (_lastFullUpdate != 0);
	}

	[[nodiscard]] int nameVersion() const;
	[[nodiscard]] const QString &name() const;
	[[nodiscard]] const QString &shortName() const;
	[[nodiscard]] const QString &topBarNameText() const;

	[[nodiscard]] QString username() const;
	[[nodiscard]] QString editableUsername() const;
	[[nodiscard]] const std::vector<QString> &usernames() const;
	[[nodiscard]] bool isUsernameEditable(QString username) const;

	[[nodiscard]] const base::flat_set<QString> &nameWords() const {
		return _nameWords;
	}
	[[nodiscard]] const base::flat_set<QChar> &nameFirstLetters() const {
		return _nameFirstLetters;
	}

	void setUserpic(
		PhotoId photoId,
		const ImageLocation &location,
		bool hasVideo);
	void setUserpicPhoto(const MTPPhoto &data);

	void paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		PaintUserpicContext context) const;
	void paintUserpic(
			Painter &p,
			Ui::PeerUserpicView &view,
			int x,
			int y,
			int size,
			bool forceCircle = false) const {
		paintUserpic(p, view, {
			.position = { x, y },
			.size = size,
			.shape = (forceCircle
				? Ui::PeerUserpicShape::Circle
				: Ui::PeerUserpicShape::Auto),
		});
	}
	void paintUserpicLeft(
			Painter &p,
			Ui::PeerUserpicView &view,
			int x,
			int y,
			int w,
			int size,
			bool forceCircle = false) const {
		paintUserpic(
			p,
			view,
			rtl() ? (w - x - size) : x,
			y,
			size,
			forceCircle);
	}
	void loadUserpic();
	[[nodiscard]] bool hasUserpic() const;
	[[nodiscard]] Ui::PeerUserpicView activeUserpicView();
	[[nodiscard]] Ui::PeerUserpicView createUserpicView();
	[[nodiscard]] bool useEmptyUserpic(Ui::PeerUserpicView &view) const;
	[[nodiscard]] InMemoryKey userpicUniqueKey(Ui::PeerUserpicView &view) const;
	[[nodiscard]] static QImage GenerateUserpicImage(
		not_null<PeerData*> peer,
		Ui::PeerUserpicView &view,
		int size,
		std::optional<int> radius = {});
	[[nodiscard]] ImageLocation userpicLocation() const;

	static constexpr auto kUnknownPhotoId = PhotoId(0xFFFFFFFFFFFFFFFFULL);
	[[nodiscard]] bool userpicPhotoUnknown() const;
	[[nodiscard]] PhotoId userpicPhotoId() const;
	[[nodiscard]] bool userpicHasVideo() const;
	[[nodiscard]] Data::FileOrigin userpicOrigin() const;
	[[nodiscard]] Data::FileOrigin userpicPhotoOrigin() const;

	// If this string is not empty we must not allow to open the
	// conversation and we must show this string instead.
	[[nodiscard]] QString computeUnavailableReason() const;
	[[nodiscard]] bool hasSensitiveContent() const;
	void setUnavailableReasons(
		std::vector<Data::UnavailableReason> &&reason);

	[[nodiscard]] ClickHandlerPtr createOpenLink();
	[[nodiscard]] const ClickHandlerPtr &openLink() {
		if (!_openLink) {
			_openLink = createOpenLink();
		}
		return _openLink;
	}

	[[nodiscard]] QImage *userpicCloudImage(Ui::PeerUserpicView &view) const;

	[[nodiscard]] bool canPinMessages() const;
	[[nodiscard]] bool canEditMessagesIndefinitely() const;
	[[nodiscard]] bool canCreatePolls() const;
	[[nodiscard]] bool canCreateTodoLists() const;
	[[nodiscard]] bool canCreateTopics() const;
	[[nodiscard]] bool canManageTopics() const;
	[[nodiscard]] bool canPostStories() const;
	[[nodiscard]] bool canEditStories() const;
	[[nodiscard]] bool canDeleteStories() const;
	[[nodiscard]] bool canManageGifts() const;
	[[nodiscard]] bool canTransferGifts() const;
	[[nodiscard]] bool canExportChatHistory() const;
	[[nodiscard]] bool autoTranslation() const;

	// Returns true if about text was changed.
	bool setAbout(const QString &newAbout);
	[[nodiscard]] const QString &about() const {
		return _about;
	}

	void checkFolder(FolderId folderId);

	void setBarSettings(PeerBarSettings which) {
		_barSettings.set(which);
	}
	[[nodiscard]] auto barSettings() const {
		return (_barSettings.current() & PeerBarSetting::Unknown)
			? std::nullopt
			: std::make_optional(_barSettings.current());
	}
	[[nodiscard]] auto barSettingsValue() const {
		return (_barSettings.current() & PeerBarSetting::Unknown)
			? _barSettings.changes()
			: (_barSettings.value() | rpl::type_erased());
	}
	[[nodiscard]] int paysPerMessage() const;
	void clearPaysPerMessage();
	[[nodiscard]] QString requestChatTitle() const;
	[[nodiscard]] TimeId requestChatDate() const;
	[[nodiscard]] UserData *businessBot() const;
	[[nodiscard]] QString businessBotManageUrl() const;
	void clearBusinessBot();
	[[nodiscard]] QString phoneCountryCode() const;
	[[nodiscard]] int registrationMonth() const;
	[[nodiscard]] int registrationYear() const;
	[[nodiscard]] TimeId nameChangeDate() const;
	[[nodiscard]] TimeId photoChangeDate() const;

	enum class TranslationFlag : uchar {
		Unknown,
		Disabled,
		Enabled,
	};
	void setTranslationDisabled(bool disabled);
	[[nodiscard]] TranslationFlag translationFlag() const;
	void saveTranslationDisabled(bool disabled);

	void setBarSettings(const MTPPeerSettings &data);
	bool changeColorIndex(const tl::conditional<MTPint> &cloudColorIndex);
	bool changeBackgroundEmojiId(
		const tl::conditional<MTPlong> &cloudBackgroundEmoji);
	bool changeColor(const tl::conditional<MTPPeerColor> &cloudColor);

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
		Normal,
		Full,
	};
	[[nodiscard]] LoadedStatus loadedStatus() const {
		return _loadedStatus;
	}
	[[nodiscard]] bool isMinimalLoaded() const {
		return (loadedStatus() != LoadedStatus::Not);
	}
	[[nodiscard]] bool isLoaded() const {
		return (loadedStatus() == LoadedStatus::Normal) || isFullLoaded();
	}
	[[nodiscard]] bool isFullLoaded() const {
		return (loadedStatus() == LoadedStatus::Full);
	}
	void setLoadedStatus(LoadedStatus status);

	[[nodiscard]] TimeId messagesTTL() const;
	void setMessagesTTL(TimeId period);

	[[nodiscard]] Data::GroupCall *groupCall() const;
	[[nodiscard]] PeerId groupCallDefaultJoinAs() const;

	void setThemeEmoji(const QString &emoticon);
	[[nodiscard]] const QString &themeEmoji() const;

	void setWallPaper(
		std::optional<Data::WallPaper> paper,
		bool overriden = false);
	[[nodiscard]] bool wallPaperOverriden() const;
	[[nodiscard]] const Data::WallPaper *wallPaper() const;

	enum class StoriesState {
		Unknown,
		None,
		HasRead,
		HasUnread,
	};
	[[nodiscard]] bool hasActiveStories() const;
	[[nodiscard]] bool hasUnreadStories() const;
	void setStoriesState(StoriesState state);

	[[nodiscard]] int peerGiftsCount() const;

	const PeerId id;
	MTPinputPeer input = MTP_inputPeerEmpty();

protected:
	void updateNameDelayed(
		const QString &newName,
		const QString &newNameOrPhone,
		const QString &newUsername);
	void updateUserpic(PhotoId photoId, MTP::DcId dcId, bool hasVideo);
	void clearUserpic();
	void invalidateEmptyUserpic();
	void checkTrustedPayForMessage();

private:
	void fillNames();
	[[nodiscard]] not_null<Ui::EmptyUserpic*> ensureEmptyUserpic() const;
	[[nodiscard]] virtual auto unavailableReasons() const
		-> const std::vector<Data::UnavailableReason> &;

	void setUserpicChecked(
		PhotoId photoId,
		const ImageLocation &location,
		bool hasVideo);

	virtual void setUnavailableReasonsList(
		std::vector<Data::UnavailableReason> &&reasons);
	void setHasSensitiveContent(bool has);

	const not_null<Data::Session*> _owner;

	mutable Data::CloudImage _userpic;
	PhotoId _userpicPhotoId = kUnknownPhotoId;

	mutable std::unique_ptr<Ui::EmptyUserpic> _userpicEmpty;

	Data::PeerNotifySettings _notify;

	ClickHandlerPtr _openLink;
	base::flat_set<QString> _nameWords; // for filtering
	base::flat_set<QChar> _nameFirstLetters;

	EmojiStatusId _emojiStatusId;
	DocumentId _backgroundEmojiId = 0;
	crl::time _lastFullUpdate = 0;

	QString _name;
	uint32 _nameVersion : 29 = 1;
	uint32 _sensitiveContent : 1 = 0;
	uint32 _wallPaperOverriden : 1 = 0;
	uint32 _checkedTrustedPayForMessage : 1 = 0;

	TimeId _ttlPeriod = 0;

	BarSettings _barSettings = PeerBarSettings(PeerBarSetting::Unknown);
	std::unique_ptr<PeerBarDetails> _barDetails;

	BlockStatus _blockStatus = BlockStatus::Unknown;
	LoadedStatus _loadedStatus = LoadedStatus::Not;
	TranslationFlag _translationFlag = TranslationFlag::Unknown;
	uint8 _colorIndex : 6 = 0;
	uint8 _colorIndexCloud : 1 = 0;
	uint8 _userpicHasVideo : 1 = 0;

	QString _about;
	QString _themeEmoticon;
	std::unique_ptr<Data::WallPaper> _wallPaper;

};

namespace Data {

void SetTopPinnedMessageId(
	not_null<PeerData*> peer,
	MsgId messageId);
[[nodiscard]] FullMsgId ResolveTopPinnedId(
	not_null<PeerData*> peer,
	MsgId topicRootId,
	PeerId monoforumPeerId,
	PeerData *migrated = nullptr);
[[nodiscard]] FullMsgId ResolveMinPinnedId(
	not_null<PeerData*> peer,
	MsgId topicRootId,
	PeerId monoforumPeerId,
	PeerData *migrated = nullptr);

} // namespace Data
