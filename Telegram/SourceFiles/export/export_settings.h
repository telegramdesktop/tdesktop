/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/flat_map.h"

namespace Export {
namespace Output {
enum class Format;
} // namespace Output

struct MediaSettings {
	bool validate() const;

	enum class Type {
		Photo        = 0x01,
		Video        = 0x02,
		VoiceMessage = 0x04,
		VideoMessage = 0x08,
		Sticker      = 0x10,
		GIF          = 0x20,
		File         = 0x40,

		MediaMask    = Photo | Video | VoiceMessage | VideoMessage,
		AllMask      = MediaMask | Sticker | GIF | File,
	};
	using Types = base::flags<Type>;
	friend inline constexpr auto is_flag_type(Type) { return true; };

	Types types = DefaultTypes();
	int sizeLimit = 8 * 1024 * 1024;

	static inline Types DefaultTypes() {
		return Type::Photo;
	}

};

struct Settings {
	bool validate() const;

	enum class Type {
		PersonalInfo        = 0x001,
		Userpics            = 0x002,
		Contacts            = 0x004,
		Sessions            = 0x008,
		OtherData           = 0x010,
		PersonalChats       = 0x020,
		BotChats            = 0x040,
		PrivateGroups       = 0x080,
		PublicGroups        = 0x100,
		PrivateChannels     = 0x200,
		PublicChannels      = 0x400,

		GroupsMask          = PrivateGroups | PublicGroups,
		ChannelsMask        = PrivateChannels | PublicChannels,
		GroupsChannelsMask  = GroupsMask | ChannelsMask,
		NonChannelChatsMask = PersonalChats | BotChats | PrivateGroups,
		AnyChatsMask        = PersonalChats | BotChats | GroupsChannelsMask,
		NonChatsMask        = PersonalInfo | Userpics | Contacts | Sessions,
		AllMask             = NonChatsMask | OtherData | AnyChatsMask,
	};
	using Types = base::flags<Type>;
	friend inline constexpr auto is_flag_type(Type) { return true; };

	QString path;
	Output::Format format = Output::Format();

	Types types = DefaultTypes();
	Types fullChats = DefaultFullChats();
	MediaSettings media;

	MTPInputPeer singlePeer = MTP_inputPeerEmpty();

	TimeId availableAt = 0;

	bool onlySinglePeer() const {
		return singlePeer.type() != mtpc_inputPeerEmpty;
	}

	static inline Types DefaultTypes() {
		return Type::PersonalInfo
			| Type::Userpics
			| Type::Contacts
			| Type::PersonalChats
			| Type::PrivateGroups;
	}

	static inline Types DefaultFullChats() {
		return Type::PersonalChats
			| Type::BotChats;
	}

};

struct Environment {
	QString internalLinksDomain;
	QByteArray aboutTelegram;
	QByteArray aboutContacts;
	QByteArray aboutFrequent;
	QByteArray aboutSessions;
	QByteArray aboutWebSessions;
	QByteArray aboutChats;
	QByteArray aboutLeftChats;
};

} // namespace Export
