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

#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QByteArray>

#include <vector>

namespace Export {
struct Settings;
namespace Data {

using Utf8String = QByteArray;
using PeerId = uint64;

PeerId UserPeerId(int32 userId);
PeerId ChatPeerId(int32 chatId);
int32 BarePeerId(PeerId peerId);
bool IsChatPeerId(PeerId peerId);
bool IsUserPeerId(PeerId peerId);
int PeerColorIndex(int32 bareId);
int ApplicationColorIndex(int applicationId);
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

struct UserpicsInfo {
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
	int size = 0;
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
	int32 userId = 0;
	Utf8String firstName;
	Utf8String lastName;
	Utf8String phoneNumber;
	TimeId date = 0;

	Utf8String name() const;
};

ContactInfo ParseContactInfo(const MTPUser &data);
int ContactColorIndex(const ContactInfo &data);

struct Photo {
	uint64 id = 0;
	TimeId date = 0;

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

	int32 botId = 0;
};

struct Invoice {
	Utf8String title;
	Utf8String description;
	Utf8String currency;
	uint64 amount = 0;
	int32 receiptMsgId = 0;
};

struct Poll {
	struct Answer {
		Utf8String text;
		QByteArray option;
		int votes = 0;
		bool my = false;
	};

	uint64 id = 0;
	Utf8String question;
	std::vector<Answer> answers;
	int totalVotes = 0;
	bool closed = false;
};

struct UserpicsSlice {
	std::vector<Photo> list;
};

UserpicsSlice ParseUserpicsSlice(
	const MTPVector<MTPPhoto> &data,
	int baseIndex);

struct User {
	ContactInfo info;
	Utf8String username;
	int32 id;
	bool isBot = false;
	bool isSelf = false;
	bool isReplies = false;

	MTPInputUser input = MTP_inputUserEmpty();

	Utf8String name() const;
};

User ParseUser(const MTPUser &data);
std::map<int32, User> ParseUsersList(const MTPVector<MTPUser> &data);

struct Chat {
	int32 id = 0;
	int32 migratedToChannelId = 0;
	Utf8String title;
	Utf8String username;
	bool isBroadcast = false;
	bool isSupergroup = false;

	MTPInputPeer input = MTP_inputPeerEmpty();
};

Chat ParseChat(const MTPChat &data);
std::map<int32, Chat> ParseChatsList(const MTPVector<MTPChat> &data);

struct Peer {
	PeerId id() const;
	Utf8String name() const;
	MTPInputPeer input() const;

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

PersonalInfo ParsePersonalInfo(const MTPUserFull &data);

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
	int32 botId = 0;
};

Media ParseMedia(
	ParseMediaContext &context,
	const MTPMessageMedia &data,
	const QString &folder,
	TimeId date);

struct ActionChatCreate {
	Utf8String title;
	std::vector<int32> userIds;
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
	std::vector<int32> userIds;
};

struct ActionChatDeleteUser {
	int32 userId = 0;
};

struct ActionChatJoinedByLink {
	int32 inviterId = 0;
};

struct ActionChannelCreate {
	Utf8String title;
};

struct ActionChatMigrateTo {
	int32 channelId = 0;
};

struct ActionChannelMigrateFrom {
	Utf8String title;
	int32 chatId = 0;
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
};

struct ActionPhoneCall {
	enum class DiscardReason {
		Unknown,
		Missed,
		Disconnect,
		Hangup,
		Busy,
	};
	DiscardReason discardReason = DiscardReason::Unknown;
	int duration = 0;
};

struct ActionScreenshotTaken {
};

struct ActionCustomAction {
	Utf8String message;
};

struct ActionBotAllowed {
	Utf8String domain;
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
		ActionGeoProximityReached> content;
};

ServiceAction ParseServiceAction(
	ParseMediaContext &context,
	const MTPMessageAction &data,
	const QString &mediaFolder);

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
	};
	Type type = Type::Text;
	Utf8String text;
	Utf8String additional;
};

struct Message {
	int32 id = 0;
	int32 chatId = 0;
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
	int32 viaBotId = 0;
	int32 replyToMsgId = 0;
	PeerId replyToPeerId = 0;
	std::vector<TextPart> text;
	Media media;
	ServiceAction action;
	bool out = false;

	File &file();
	const File &file() const;
	Image &thumb();
	const Image &thumb() const;
};

struct FileOrigin {
	int split = 0;
	MTPInputPeer peer;
	int32 messageId = 0;
};

Message ParseMessage(
	ParseMediaContext &context,
	const MTPMessage &data,
	const QString &mediaFolder);
std::map<uint64, Message> ParseMessagesList(
	PeerId selfId,
	const MTPVector<MTPMessage> &data,
	const QString &mediaFolder);

struct DialogInfo {
	enum class Type {
		Unknown,
		Self,
		Replies,
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

	MTPInputPeer migratedFromInput = MTP_inputPeerEmpty();
	int32 migratedToChannelId = 0;

	// User messages splits which contained that dialog.
	std::vector<int> splits;

	// Filled after the whole dialogs list is accumulated.
	bool onlyMyMessages = false;
	bool isLeftChannel = false;
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
	QChar dateSeparator = QChar('.'),
	QChar timeSeparator = QChar(':'),
	QChar separator = QChar(' '));
Utf8String FormatMoneyAmount(uint64 amount, const Utf8String &currency);
Utf8String FormatFileSize(int64 size);
Utf8String FormatDuration(int64 seconds);

} // namespace Data
} // namespace Export
