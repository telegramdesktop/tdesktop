/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "scheme.h"

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

struct PersonalInfo {
	Utf8String firstName;
	Utf8String lastName;
	Utf8String phoneNumber;
	Utf8String username;
	Utf8String bio;
};

PersonalInfo ParsePersonalInfo(const MTPUserFull &data);

struct UserpicsInfo {
	int count = 0;
};

struct File {
	QString relativePath;
};

struct Userpic {
	uint64 id = 0;
	QDateTime date;
	File image;
};

struct UserpicsSlice {
	std::vector<Userpic> list;
};

UserpicsSlice ParseUserpicsSlice(const MTPVector<MTPPhoto> &data);

struct Contact {
	Utf8String firstName;
	Utf8String lastName;
	Utf8String phoneNumber;
};

struct ContactsList {
	std::vector<Contact> list;
};

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

} // namespace Data
} // namespace Export
