/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "scheme.h"
#include "base/optional.h"
#include "base/variant.h"
#include "core/credits_amount.h"
#include "data/data_peer_id.h"

#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QByteArray>

#include <vector>

namespace Export {
struct Settings;
namespace Data {

using Utf8String = QByteArray;

uint8 PeerColorIndex(BareId bareId);
BareId PeerToBareId(PeerId peerId);
uint8 PeerColorIndex(PeerId peerId);
uint8 ApplicationColorIndex(int applicationId);
int DomainApplicationId(const Utf8String &data);

Utf8String ParseString(const MTPstring &data);

Utf8String FillLeft(const Utf8String &data, int length, char filler);

template <typename Type>
inline auto NumberToString(Type value, int length = 0, char filler = '0')
-> std::enable_if_t<std::is_arithmetic_v<Type>, Utf8String> {
	const auto result = std::to_string(value);
	return FillLeft(
		Utf8String(result.data(), int(result.size())),
		length,
		filler).replace(',', '.');
}

struct TextPart {
	enum class Type {
		Text,
		Unknown,
		Mention,
		Hashtag,
		BotCommand,
		Url,
		Email,
		Bold,
		Italic,
		Code,
		Pre,
		TextUrl,
		MentionName,
		Phone,
		Cashtag,
		Underline,
		Strike,
		Blockquote,
		BankCard,
		Spoiler,
		CustomEmoji,
	};
	Type type = Type::Text;
	Utf8String text;
	Utf8String additional;

	[[nodiscard]] static Utf8String UnavailableEmoji() {
		return "(unavailable)";
	}
};

struct UserpicsInfo {
	int count = 0;
};

struct StoriesInfo {
	int count = 0;
};

struct FileLocation {
	int dcId = 0;
	MTPInputFileLocation data;

	explicit operator bool() const {
		return dcId != 0;
	}
};

bool RefreshFileReference(FileLocation &to, const FileLocation &from);

struct File {
	enum class SkipReason {
		None,
		Unavailable,
		FileType,
		FileSize,
		DateLimits,
	};
	FileLocation location;
	int64 size = 0;
	QByteArray content;

	QString suggestedPath;

	QString relativePath;
	SkipReason skipReason = SkipReason::None;
};

struct Image {
	int width = 0;
	int height = 0;
	File file;
};

std::pair<QString, QSize> WriteImageThumb(
	const QString &basePath,
	const QString &largePath,
	Fn<QSize(QSize)> convertSize,
	std::optional<QByteArray> format = std::nullopt,
	std::optional<int> quality = std::nullopt,
	const QString &postfix = "_thumb");

QString WriteImageThumb(
	const QString &basePath,
	const QString &largePath,
	int width,
	int height,
	const QString &postfix = "_thumb");

struct ContactInfo {
	UserId userId = 0;
	Utf8String firstName;
	Utf8String lastName;
	Utf8String phoneNumber;
	TimeId date = 0;
	uint8 colorIndex = 0;

	Utf8String name() const;
};

ContactInfo ParseContactInfo(const MTPUser &data);
uint8 ContactColorIndex(const ContactInfo &data);

struct Photo {
	uint64 id = 0;
	TimeId date = 0;
	bool spoilered = false;

	Image image;
};

struct Document {
	uint64 id = 0;
	TimeId date = 0;

	File file;
	Image thumb;

	Utf8String name;
	Utf8String mime;
	int width = 0;
	int height = 0;

	Utf8String stickerEmoji;
	Utf8String songPerformer;
	Utf8String songTitle;
	int duration = 0;

	bool isSticker = false;
	bool isAnimated = false;
	bool isVideoMessage = false;
	bool isVoiceMessage = false;
	bool isVideoFile = false;
	bool isAudioFile = false;
	bool spoilered = false;
};

struct SharedContact {
	ContactInfo info;
	File vcard;
};

struct GeoPoint {
	float64 latitude = 0.;
	float64 longitude = 0.;
	bool valid = false;
};

struct Venue {
	GeoPoint point;
	Utf8String title;
	Utf8String address;
};

struct Game {
	uint64 id = 0;
	Utf8String shortName;
	Utf8String title;
	Utf8String description;

	UserId botId = 0;
};

struct Invoice {
	Utf8String title;
	Utf8String description;
	Utf8String currency;
	uint64 amount = 0;
	int32 receiptMsgId = 0;
};

struct Media;
struct PaidMedia {
	PaidMedia() = default;
	PaidMedia(PaidMedia &&) = default;
	PaidMedia &operator=(PaidMedia &&) = default;
	PaidMedia(const PaidMedia &) = delete;
	PaidMedia &operator=(const PaidMedia &) = delete;

	uint64 stars = 0;
	std::vector<std::unique_ptr<Media>> extended;
};

struct Poll {
	struct Answer {
		std::vector<TextPart> text;
		QByteArray option;
		int votes = 0;
		bool my = false;
	};

	uint64 id = 0;
	std::vector<TextPart> question;
	std::vector<Answer> answers;
	int totalVotes = 0;
	bool closed = false;
};

struct TodoListItem {
	std::vector<TextPart> text;
	int id = 0;
};

struct TodoList {
	bool othersCanAppend = false;
	bool othersCanComplete = false;
	std::vector<TextPart> title;
	std::vector<TodoListItem> items;
};

struct GiveawayStart {
	std::vector<QString> countries;
	std::vector<ChannelId> channels;
	QString additionalPrize;
	TimeId untilDate = 0;
	uint64 credits = 0;
	int quantity = 0;
	int months = 0;
	bool all = false;
};

struct GiveawayResults {
	ChannelId channel = 0;
	std::vector<PeerId> winners;
	QString additionalPrize;
	TimeId untilDate = 0;
	int32 launchId = 0;
	int additionalPeersCount = 0;
	int winnersCount = 0;
	int unclaimedCount = 0;
	int months = 0;
	uint64 credits = 0;
	bool refunded = false;
	bool all = false;
};

struct UserpicsSlice {
	std::vector<Photo> list;
};

UserpicsSlice ParseUserpicsSlice(
	const MTPVector<MTPPhoto> &data,
	int baseIndex);

struct User {
	PeerId id() const;

	BareId bareId = 0;
	ContactInfo info;
	Utf8String username;
	uint8 colorIndex = 0;
	bool isBot = false;
	bool isSelf = false;
	bool isReplies = false;
	bool isVerifyCodes = false;

	MTPInputUser input = MTP_inputUserEmpty();

	Utf8String name() const;
};

User ParseUser(const MTPUser &data);
std::map<UserId, User> ParseUsersList(const MTPVector<MTPUser> &data);

struct Chat {
	PeerId id() const;

	BareId bareId = 0;
	ChannelId migratedToChannelId = 0;
	Utf8String title;
	Utf8String username;
	uint8 colorIndex = 0;
	bool isMonoforum = false;
	bool isBroadcast = false;
	bool isSupergroup = false;
	bool isMonoforumAdmin = false;
	bool hasMonoforumAdminRights = false;
	bool isMonoforumOfPublicBroadcast = false;
	BareId monoforumLinkId = 0;

	MTPInputPeer input = MTP_inputPeerEmpty();
	MTPInputPeer monoforumBroadcastInput = MTP_inputPeerEmpty();
};

Chat ParseChat(const MTPChat &data);

struct Peer {
	PeerId id() const;
	Utf8String name() const;
	MTPInputPeer input() const;
	uint8 colorIndex() const;

	const User *user() const;
	const Chat *chat() const;

	std::variant<User, Chat> data;

};

std::map<PeerId, Peer> ParsePeersLists(
	const MTPVector<MTPUser> &users,
	const MTPVector<MTPChat> &chats);

struct PersonalInfo {
	User user;
	Utf8String bio;
};

PersonalInfo ParsePersonalInfo(const MTPDusers_userFull &data);

struct TopPeer {
	Peer peer;
	float64 rating = 0.;
};

struct ContactsList {
	std::vector<ContactInfo> list;
	std::vector<TopPeer> correspondents;
	std::vector<TopPeer> inlineBots;
	std::vector<TopPeer> phoneCalls;
};

ContactsList ParseContactsList(const MTPcontacts_Contacts &data);
ContactsList ParseContactsList(const MTPVector<MTPSavedContact> &data);
std::vector<int> SortedContactsIndices(const ContactsList &data);
bool AppendTopPeers(ContactsList &to, const MTPcontacts_TopPeers &data);

struct Session {
	int applicationId = 0;
	Utf8String platform;
	Utf8String deviceModel;
	Utf8String systemVersion;
	Utf8String applicationName;
	Utf8String applicationVersion;
	TimeId created = 0;
	TimeId lastActive = 0;
	Utf8String ip;
	Utf8String country;
	Utf8String region;
};

struct WebSession {
	Utf8String botUsername;
	Utf8String domain;
	Utf8String browser;
	Utf8String platform;
	TimeId created = 0;
	TimeId lastActive = 0;
	Utf8String ip;
	Utf8String region;
};

struct SessionsList {
	std::vector<Session> list;
	std::vector<WebSession> webList;
};

SessionsList ParseSessionsList(const MTPaccount_Authorizations &data);
SessionsList ParseWebSessionsList(const MTPaccount_WebAuthorizations &data);

struct UnsupportedMedia {
};

struct Media {
	std::variant<
		v::null_t,
		Photo,
		Document,
		SharedContact,
		GeoPoint,
		Venue,
		Game,
		Invoice,
		Poll,
		TodoList,
		GiveawayStart,
		GiveawayResults,
		PaidMedia,
		UnsupportedMedia> content;
	TimeId ttl = 0;

	File &file();
	const File &file() const;
	Image &thumb();
	const Image &thumb() const;
};

struct ParseMediaContext {
	PeerId selfPeerId = 0;
	int photos = 0;
	int audios = 0;
	int videos = 0;
	int files = 0;
	int contacts = 0;
	UserId botId = 0;
};

Document ParseDocument(
	ParseMediaContext &context,
	const MTPDocument &data,
	const QString &suggestedFolder,
	TimeId date);

Media ParseMedia(
	ParseMediaContext &context,
	const MTPMessageMedia &data,
	const QString &folder,
	TimeId date);

struct ActionChatCreate {
	Utf8String title;
	std::vector<UserId> userIds;
};

struct ActionChatEditTitle {
	Utf8String title;
};

struct ActionChatEditPhoto {
	Photo photo;
};

struct ActionChatDeletePhoto {
};

struct ActionChatAddUser {
	std::vector<UserId> userIds;
};

struct ActionChatDeleteUser {
	UserId userId = 0;
};

struct ActionChatJoinedByLink {
	UserId inviterId = 0;
};

struct ActionChannelCreate {
	Utf8String title;
};

struct ActionChatMigrateTo {
	ChannelId channelId = 0;
};

struct ActionChannelMigrateFrom {
	Utf8String title;
	ChatId chatId = 0;
};

struct ActionPinMessage {
};

struct ActionHistoryClear {
};

struct ActionGameScore {
	uint64 gameId = 0;
	int score = 0;
};

struct ActionPaymentSent {
	Utf8String currency;
	uint64 amount = 0;
	bool recurringInit = false;
	bool recurringUsed = false;
};

struct ActionPhoneCall {
	enum class State {
		Unknown,
		Missed,
		Disconnect,
		Hangup,
		Busy,
		MigrateConferenceCall,
		Invitation,
		Active,
	};

	uint64 conferenceId = 0;
	State state = State::Unknown;
	int duration = 0;
};

struct ActionScreenshotTaken {
};

struct ActionCustomAction {
	Utf8String message;
};

struct ActionBotAllowed {
	uint64 appId = 0;
	Utf8String app;
	Utf8String domain;
	bool attachMenu = false;
	bool fromRequest = false;
};

struct ActionSecureValuesSent {
	enum class Type {
		PersonalDetails,
		Passport,
		DriverLicense,
		IdentityCard,
		InternalPassport,
		Address,
		UtilityBill,
		BankStatement,
		RentalAgreement,
		PassportRegistration,
		TemporaryRegistration,
		Phone,
		Email,
	};
	std::vector<Type> types;
};

struct ActionContactSignUp {
};

struct ActionPhoneNumberRequest {
};

struct ActionGeoProximityReached {
	PeerId fromId = 0;
	PeerId toId = 0;
	int distance = 0;
	bool fromSelf = false;
	bool toSelf = false;
};

struct ActionGroupCall {
	int duration = 0;
};

struct ActionInviteToGroupCall {
	std::vector<UserId> userIds;
};

struct ActionSetMessagesTTL {
	TimeId period = 0;
};

struct ActionGroupCallScheduled {
	TimeId date = 0;
};

struct ActionSetChatTheme {
	QString emoji;
};

struct ActionChatJoinedByRequest {
};

struct ActionWebViewDataSent {
	Utf8String text;
};

struct ActionGiftPremium {
	Utf8String cost;
	int months = 0;
};

struct ActionTopicCreate {
	Utf8String title;
};

struct ActionTopicEdit {
	Utf8String title;
	std::optional<uint64> iconEmojiId = 0;
};

struct ActionSuggestProfilePhoto {
	Photo photo;
};

struct ActionSetChatWallPaper {
	bool same = false;
	bool both = false;
	// #TODO wallpapers
};

struct ActionGiftCode {
	QByteArray code;
	PeerId boostPeerId = 0;
	int months = 0;
	bool viaGiveaway = false;
	bool unclaimed = false;
};

struct ActionRequestedPeer {
	std::vector<PeerId> peers;
	int buttonId = 0;
};

struct ActionGiveawayLaunch {
};

struct ActionGiveawayResults {
	int winners = 0;
	int unclaimed = 0;
	bool credits = false;
};

struct ActionBoostApply {
	int boosts = 0;
};

struct ActionPaymentRefunded {
	PeerId peerId = 0;
	Utf8String currency;
	uint64 amount = 0;
	Utf8String transactionId;
};

struct ActionGiftCredits {
	Utf8String cost;
	CreditsAmount amount;
};

struct ActionPrizeStars {
	PeerId peerId = 0;
	uint64 amount = 0;
	Utf8String transactionId;
	int32 giveawayMsgId = 0;
	bool isUnclaimed = false;
};

struct ActionStarGift {
	uint64 giftId = 0;
	int64 stars = 0;
	std::vector<TextPart> text;
	bool anonymous = false;
	bool limited = false;
};

struct ActionPaidMessagesRefunded {
	int messages = 0;
	int64 stars = 0;
};

struct ActionPaidMessagesPrice {
	int stars = 0;
	bool broadcastAllowed = false;
};

struct ActionTodoCompletions {
	std::vector<int> completed;
	std::vector<int> incompleted;
};

struct ActionTodoAppendTasks {
	std::vector<TodoListItem> items;
};

struct ActionSuggestedPostApproval {
	Utf8String rejectComment;
	TimeId scheduleDate = 0;
	CreditsAmount price;
	bool rejected = false;
	bool balanceTooLow = false;
};

struct ActionSuggestedPostSuccess {
	CreditsAmount price;
};

struct ActionSuggestedPostRefund {
	bool payerInitiated = false;
};

struct ServiceAction {
	std::variant<
		v::null_t,
		ActionChatCreate,
		ActionChatEditTitle,
		ActionChatEditPhoto,
		ActionChatDeletePhoto,
		ActionChatAddUser,
		ActionChatDeleteUser,
		ActionChatJoinedByLink,
		ActionChannelCreate,
		ActionChatMigrateTo,
		ActionChannelMigrateFrom,
		ActionPinMessage,
		ActionHistoryClear,
		ActionGameScore,
		ActionPaymentSent,
		ActionPhoneCall,
		ActionScreenshotTaken,
		ActionCustomAction,
		ActionBotAllowed,
		ActionSecureValuesSent,
		ActionContactSignUp,
		ActionPhoneNumberRequest,
		ActionGeoProximityReached,
		ActionGroupCall,
		ActionInviteToGroupCall,
		ActionSetMessagesTTL,
		ActionGroupCallScheduled,
		ActionSetChatTheme,
		ActionChatJoinedByRequest,
		ActionWebViewDataSent,
		ActionGiftPremium,
		ActionTopicCreate,
		ActionTopicEdit,
		ActionSuggestProfilePhoto,
		ActionRequestedPeer,
		ActionSetChatWallPaper,
		ActionGiftCode,
		ActionGiveawayLaunch,
		ActionGiveawayResults,
		ActionBoostApply,
		ActionPaymentRefunded,
		ActionGiftCredits,
		ActionPrizeStars,
		ActionStarGift,
		ActionPaidMessagesRefunded,
		ActionPaidMessagesPrice,
		ActionTodoCompletions,
		ActionTodoAppendTasks,
		ActionSuggestedPostApproval,
		ActionSuggestedPostSuccess,
		ActionSuggestedPostRefund> content;
};

ServiceAction ParseServiceAction(
	ParseMediaContext &context,
	const MTPMessageAction &data,
	const QString &mediaFolder);

struct Reaction {
	enum class Type {
		Empty,
		Emoji,
		CustomEmoji,
		Paid,
	};

	[[nodiscard]] static Utf8String TypeToString(const Reaction &);

	[[nodiscard]] static Utf8String Id(const Reaction &);

	struct Recent {
		PeerId peerId = 0;
		TimeId date = 0;
	};

	Type type;
	QString emoji;
	Utf8String documentId;
	uint32 count = 0;
	std::vector<Recent> recent;
};

struct MessageId {
	ChannelId channelId;
	int32 msgId = 0;
};

inline bool operator==(MessageId a, MessageId b) {
	return (a.channelId == b.channelId) && (a.msgId == b.msgId);
}
inline bool operator!=(MessageId a, MessageId b) {
	return !(a == b);
}
inline bool operator<(MessageId a, MessageId b) {
	return (a.channelId < b.channelId)
		|| (a.channelId == b.channelId && a.msgId < b.msgId);
}
inline bool operator>(MessageId a, MessageId b) {
	return (b < a);
}
inline bool operator<=(MessageId a, MessageId b) {
	return !(b < a);
}
inline bool operator>=(MessageId a, MessageId b) {
	return !(a < b);
}

struct HistoryMessageMarkupButton {
	enum class Type {
		Default,
		Url,
		Callback,
		CallbackWithPassword,
		RequestPhone,
		RequestLocation,
		RequestPoll,
		RequestPeer,
		SwitchInline,
		SwitchInlineSame,
		Game,
		Buy,
		Auth,
		UserProfile,
		WebView,
		SimpleWebView,
		CopyText,
	};

	static QByteArray TypeToString(const HistoryMessageMarkupButton &);

	Type type;
	QString text;
	QByteArray data;
	QString forwardText;
	int64 buttonId = 0;
};

struct Message {
	int32 id = 0;
	TimeId date = 0;
	TimeId edited = 0;
	PeerId fromId = 0;
	PeerId peerId = 0;
	PeerId selfId = 0;
	PeerId forwardedFromId = 0;
	Utf8String forwardedFromName;
	TimeId forwardedDate = 0;
	bool forwarded = false;
	bool showForwardedAsOriginal = false;
	PeerId savedFromChatId = 0;
	Utf8String signature;
	UserId viaBotId = 0;
	int32 replyToMsgId = 0;
	PeerId replyToPeerId = 0;
	std::vector<TextPart> text;
	std::vector<Reaction> reactions;
	Media media;
	ServiceAction action;
	bool out = false;
	std::vector<std::vector<HistoryMessageMarkupButton>> inlineButtonRows;

	File &file();
	const File &file() const;
	Image &thumb();
	const Image &thumb() const;
};

struct FileOrigin {
	int split = 0;
	MTPInputPeer peer;
	int32 messageId = 0;
	int32 storyId = 0;
	uint64 customEmojiId = 0;
};

struct Story {
	int32 id = 0;
	TimeId date = 0;
	TimeId expires = 0;
	Media media;
	bool pinned = false;
	std::vector<TextPart> caption;

	File &file();
	const File &file() const;
	Image &thumb();
	const Image &thumb() const;
};

struct StoriesSlice {
	std::vector<Story> list;
	int32 lastId = 0;
	int skipped = 0;
};

StoriesSlice ParseStoriesSlice(
	const MTPVector<MTPStoryItem> &data,
	int baseIndex);

Message ParseMessage(
	ParseMediaContext &context,
	const MTPMessage &data,
	const QString &mediaFolder);
std::map<MessageId, Message> ParseMessagesList(
	PeerId selfId,
	const MTPVector<MTPMessage> &data,
	const QString &mediaFolder);

struct DialogInfo {
	enum class Type {
		Unknown,
		Self,
		Replies,
		VerifyCodes,
		Personal,
		Bot,
		PrivateGroup,
		PrivateSupergroup,
		PublicSupergroup,
		PrivateChannel,
		PublicChannel,
	};
	Type type = Type::Unknown;
	Utf8String name;
	Utf8String lastName;

	MTPInputPeer input = MTP_inputPeerEmpty();
	int32 topMessageId = 0;
	TimeId topMessageDate = 0;
	PeerId peerId = 0;
	uint8 colorIndex = 0;

	MTPInputPeer migratedFromInput = MTP_inputPeerEmpty();
	ChannelId migratedToChannelId = 0;

	MTPInputPeer monoforumBroadcastInput = MTP_inputPeerEmpty();

	// User messages splits which contained that dialog.
	std::vector<int> splits;

	// Filled after the whole dialogs list is accumulated.
	bool onlyMyMessages = false;
	bool isLeftChannel = false;
	bool isMonoforum = false;
	QString relativePath;

	// Filled when requesting dialog messages.
	std::vector<int> messagesCountPerSplit;
};

struct DialogsInfo {
	DialogInfo *item(int index);
	const DialogInfo *item(int index) const;

	std::vector<DialogInfo> chats;
	std::vector<DialogInfo> left;
};

DialogInfo::Type DialogTypeFromChat(const Chat &chat);

DialogsInfo ParseDialogsInfo(const MTPmessages_Dialogs &data);
DialogsInfo ParseDialogsInfo(
	const MTPInputPeer &singlePeer,
	const MTPVector<MTPUser> &data);
DialogsInfo ParseDialogsInfo(
	const MTPInputPeer &singlePeer,
	const MTPmessages_Chats &data);
DialogsInfo ParseLeftChannelsInfo(const MTPmessages_Chats &data);
bool AddMigrateFromSlice(
	DialogInfo &to,
	const DialogInfo &from,
	int splitIndex,
	int splitsCount);
void FinalizeDialogsInfo(DialogsInfo &info, const Settings &settings);

struct MessagesSlice {
	std::vector<Message> list;
	std::map<PeerId, Peer> peers;
};

MessagesSlice ParseMessagesSlice(
	ParseMediaContext &context,
	const MTPVector<MTPMessage> &data,
	const MTPVector<MTPUser> &users,
	const MTPVector<MTPChat> &chats,
	const QString &mediaFolder);
MessagesSlice AdjustMigrateMessageIds(MessagesSlice slice);

bool SingleMessageBefore(
	const MTPmessages_Messages &data,
	TimeId date);
bool SingleMessageAfter(
	const MTPmessages_Messages &data,
	TimeId date);
bool SkipMessageByDate(const Message &message, const Settings &settings);

Utf8String FormatPhoneNumber(const Utf8String &phoneNumber);
Utf8String FormatDateTime(
	TimeId date,
	bool hasTimeZone = false,
	QChar dateSeparator = QChar('.'),
	QChar timeSeparator = QChar(':'),
	QChar separator = QChar(' '));
Utf8String FormatMoneyAmount(int64 amount, const Utf8String &currency);
Utf8String FormatFileSize(int64 size);
Utf8String FormatDuration(int64 seconds);

} // namespace Data
} // namespace Export
