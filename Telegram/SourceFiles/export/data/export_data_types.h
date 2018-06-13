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
	FileLocation location;
	int size = 0;
	QByteArray content;

	QString suggestedPath;

	QString relativePath;
};

struct Photo {
	uint64 id = 0;
	TimeId date = 0;

	int width = 0;
	int height = 0;
	File image;
};

struct UserpicsSlice {
	std::vector<Photo> list;
};

UserpicsSlice ParseUserpicsSlice(const MTPVector<MTPPhoto> &data);

struct User {
	int32 id = 0;
	Utf8String firstName;
	Utf8String lastName;
	Utf8String phoneNumber;
	Utf8String username;

	MTPInputUser input;
};

User ParseUser(const MTPUser &data);
std::map<int32, User> ParseUsersList(const MTPVector<MTPUser> &data);

struct Chat {
	int32 id = 0;
	Utf8String title;
	Utf8String username;
	bool broadcast = false;

	MTPInputPeer input;
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
	std::vector<User> list;
};

ContactsList ParseContactsList(const MTPcontacts_Contacts &data);
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

struct Message {
	int32 id = 0;
	TimeId date = 0;

	Utf8String text;
	File mediaFile;

};

Message ParseMessage(const MTPMessage &data);
std::map<int32, Message> ParseMessagesList(
	const MTPVector<MTPMessage> &data);

struct DialogInfo {
	enum class Type {
		Unknown,
		Personal,
		PrivateGroup,
		PublicGroup,
		Channel,
	};
	Type type = Type::Unknown;
	Utf8String name;

	MTPInputPeer input;
	int32 topMessageId = 0;
	TimeId topMessageDate = 0;
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
	const MTPVector<MTPChat> &chats);

Utf8String FormatPhoneNumber(const Utf8String &phoneNumber);

Utf8String FormatDateTime(
	TimeId date,
	QChar dateSeparator = QChar('.'),
	QChar timeSeparator = QChar(':'),
	QChar separator = QChar(' '));

} // namespace Data
} // namespace Export
