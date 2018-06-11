/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "scheme.h"
#include "base/optional.h"

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <vector>

namespace Export {
namespace Data {

using Utf8String = QByteArray;

Utf8String ParseString(const MTPstring &data);

template <typename Type>
inline auto NumberToString(Type value)
-> std::enable_if_t<std::is_arithmetic_v<Type>, Utf8String> {
	const auto result = std::to_string(value);
	return QByteArray(result.data(), int(result.size()));
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
	QDateTime date;

	int width = 0;
	int height = 0;
	File image;
};

struct UserpicsSlice {
	std::vector<Photo> list;
};

UserpicsSlice ParseUserpicsSlice(const MTPVector<MTPPhoto> &data);

struct User {
	int id = 0;
	Utf8String firstName;
	Utf8String lastName;
	Utf8String phoneNumber;
	Utf8String username;
};

User ParseUser(const MTPUser &user);
std::map<int, User> ParseUsersList(const MTPVector<MTPUser> &data);

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
	QDateTime created;
	QDateTime lastActive;
	Utf8String ip;
	Utf8String country;
	Utf8String region;
};

struct SessionsList {
	std::vector<Session> list;
};

SessionsList ParseSessionsList(const MTPaccount_Authorizations &data);

struct ChatsInfo {
	int count = 0;
};

struct ChatInfo {
	enum class Type {
		Personal,
		Group,
		Channel,
	};
	Type type = Type::Personal;
	QString name;
};

struct Message {
	int id = 0;
};

struct MessagesSlice {
	std::vector<Message> list;
};

Utf8String FormatPhoneNumber(const Utf8String &phoneNumber);

Utf8String FormatDateTime(
	const QDateTime &date,
	QChar dateSeparator = QChar('.'),
	QChar timeSeparator = QChar(':'),
	QChar separator = QChar(' '));

inline Utf8String FormatDateTime(
		int32 date,
		QChar dateSeparator = QChar('.'),
		QChar timeSeparator = QChar(':'),
		QChar separator = QChar(' ')) {
	return FormatDateTime(
		QDateTime::fromTime_t(date),
		dateSeparator,
		timeSeparator,
		separator);
}

} // namespace Data
} // namespace Export
