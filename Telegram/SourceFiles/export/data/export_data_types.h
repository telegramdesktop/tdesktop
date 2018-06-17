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

#include <QtCore/QString>
#include <QtCore/QByteArray>

#include <vector>

namespace Export {
namespace Data {

using TimeId = int32;
using Utf8String = QByteArray;
using PeerId = uint64;

PeerId UserPeerId(int32 userId);
PeerId ChatPeerId(int32 chatId);
int32 BarePeerId(PeerId peerId);

Utf8String ParseString(const MTPstring &data);

Utf8String FillLeft(const Utf8String &data, int length, char filler);

template <typename Type>
inline auto NumberToString(Type value, int length = 0, char filler = '0')
-> std::enable_if_t<std::is_arithmetic_v<Type>, Utf8String> {
	const auto result = std::to_string(value);
	return FillLeft(
		Utf8String(result.data(), int(result.size())),
		length,
		filler);
}

struct UserpicsInfo {
	int count = 0;
};

struct FileLocation {
	int dcId = 0;
	MTPInputFileLocation data;
};

struct File {
	enum class SkipReason {
		None,
		Unavailable,
		FileType,
		FileSize,
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

struct Photo {
	uint64 id = 0;
	TimeId date = 0;

	Image image;
};

struct Document {
	uint64 id = 0;
	TimeId date = 0;

	File file;

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

struct UserpicsSlice {
	std::vector<Photo> list;
};

UserpicsSlice ParseUserpicsSlice(const MTPVector<MTPPhoto> &data);

struct ContactInfo {
	int32 userId = 0;
	Utf8String firstName;
	Utf8String lastName;
	Utf8String phoneNumber;
	TimeId date = 0;

	Utf8String name() const;
};

ContactInfo ParseContactInfo(const MTPUser &data);
ContactInfo ParseContactInfo(const MTPDmessageMediaContact &data);

struct User {
	ContactInfo info;
	Utf8String username;
	bool isBot = false;

	MTPInputUser input = MTP_inputUserEmpty();

	Utf8String name() const;
};

User ParseUser(const MTPUser &data);
std::map<int32, User> ParseUsersList(const MTPVector<MTPUser> &data);

struct Chat {
	int32 id = 0;
	Utf8String title;
	Utf8String username;
	bool broadcast = false;

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

	base::variant<User, Chat> data;

};

std::map<PeerId, Peer> ParsePeersLists(
	const MTPVector<MTPUser> &users,
	const MTPVector<MTPChat> &chats);

struct PersonalInfo {
	User user;
	Utf8String bio;
};

PersonalInfo ParsePersonalInfo(const MTPUserFull &data);

struct ContactsList {
	std::vector<ContactInfo> list;
};

ContactsList ParseContactsList(const MTPcontacts_Contacts &data);
ContactsList ParseContactsList(const MTPVector<MTPSavedContact> &data);
std::vector<int> SortedContactsIndices(const ContactsList &data);

struct Session {
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

struct SessionsList {
	std::vector<Session> list;
};

SessionsList ParseSessionsList(const MTPaccount_Authorizations &data);

struct UnsupportedMedia {
};

struct Media {
	base::optional_variant<
		Photo,
		Document,
		ContactInfo,
		GeoPoint,
		Venue,
		Game,
		Invoice,
		UnsupportedMedia> content;
	TimeId ttl = 0;

	File &file();
	const File &file() const;
};

Media ParseMedia(
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

struct ServiceAction {
	base::optional_variant<
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
		ActionSecureValuesSent> content;
};

ServiceAction ParseServiceAction(
	const MTPMessageAction &data,
	const QString &mediaFolder,
	TimeId date);

struct Message {
	int32 id = 0;
	TimeId date = 0;
	TimeId edited = 0;
	int32 fromId = 0;
	PeerId forwardedFromId = 0;
	Utf8String signature;
	int32 viaBotId = 0;
	int32 replyToMsgId = 0;
	Utf8String text;
	Media media;
	ServiceAction action;

	File &file();
	const File &file() const;
};

Message ParseMessage(const MTPMessage &data, const QString &mediaFolder);
std::map<int32, Message> ParseMessagesList(
	const MTPVector<MTPMessage> &data,
	const QString &mediaFolder);

struct DialogInfo {
	enum class Type {
		Unknown,
		Personal,
		Bot,
		PrivateGroup,
		PublicGroup,
		PrivateChannel,
		PublicChannel,
	};
	Type type = Type::Unknown;
	Utf8String name;

	MTPInputPeer input = MTP_inputPeerEmpty();
	int32 topMessageId = 0;
	TimeId topMessageDate = 0;

	QString relativePath;

};

struct DialogsInfo {
	std::vector<DialogInfo> list;
};

DialogsInfo ParseDialogsInfo(const MTPmessages_Dialogs &data);

struct MessagesSlice {
	std::vector<Message> list;
	std::map<PeerId, Peer> peers;
};

MessagesSlice ParseMessagesSlice(
	const MTPVector<MTPMessage> &data,
	const MTPVector<MTPUser> &users,
	const MTPVector<MTPChat> &chats,
	const QString &mediaFolder);

Utf8String FormatPhoneNumber(const Utf8String &phoneNumber);

Utf8String FormatDateTime(
	TimeId date,
	QChar dateSeparator = QChar('.'),
	QChar timeSeparator = QChar(':'),
	QChar separator = QChar(' '));

Utf8String FormatMoneyAmount(uint64 amount, const Utf8String &currency);

} // namespace Data
} // namespace Export
