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

struct MediaSettings {
	enum class Type {
		Photo,
		Video,
		Sticker,
		GIF,
		File,
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
		PersonalInfo,
		Avatars,
		Contacts,
		Sessions,
		PersonalChats,
		PrivateGroups,
		PublicGroups,
		MyChannels,
	};
	using Types = base::flags<Type>;
	friend inline constexpr auto is_flag_type(Type) { return true; };

	QString path;

	Types types = DefaultTypes();
	MediaSettings defaultMedia;
	base::flat_map<Type, MediaSettings> customMedia;

	static inline Types DefaultTypes() {
		return Type::PersonalInfo
			| Type::Avatars
			| Type::Contacts
			| Type::Sessions
			| Type::PersonalChats;
	}

};

} // namespace Export
