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
	enum class Type {
		Photo        = 0x01,
		Video        = 0x02,
		VoiceMessage = 0x04,
		VideoMessage = 0x08,
		Sticker      = 0x10,
		GIF          = 0x20,
		File         = 0x40,
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
	enum class Type {
		PersonalInfo        = 0x001,
		Userpics            = 0x002,
		Contacts            = 0x004,
		Sessions            = 0x008,
		PersonalChats       = 0x010,
		BotChats            = 0x020,
		PrivateGroups       = 0x040,
		PublicGroups        = 0x080,
		PrivateChannels     = 0x100,
		PublicChannels      = 0x200,

		GroupsMask          = PrivateGroups | PublicGroups,
		ChannelsMask        = PrivateChannels | PublicChannels,
		GroupsChannelsMask  = GroupsMask | ChannelsMask,
		NonChannelChatsMask = PersonalChats | BotChats | PrivateGroups,
		AnyChatsMask        = PersonalChats | BotChats | GroupsChannelsMask,
	};
	using Types = base::flags<Type>;
	friend inline constexpr auto is_flag_type(Type) { return true; };

	QString path;
	QString internalLinksDomain;
	Output::Format format = Output::Format();

	Types types = DefaultTypes();
	Types fullChats = DefaultFullChats();
	MediaSettings media;

	static inline Types DefaultTypes() {
		return Type::PersonalInfo
			| Type::Userpics
			| Type::Contacts
			| Type::Sessions
			| Type::PersonalChats
			| Type::PrivateGroups;
	}

	static inline Types DefaultFullChats() {
		return Type::PersonalChats
			| Type::BotChats;
	}

};

} // namespace Export
