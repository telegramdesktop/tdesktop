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
		Photo   = 0x01,
		Video   = 0x02,
		Sticker = 0x04,
		GIF     = 0x08,
		File    = 0x10,
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
		PersonalInfo  = 0x01,
		Userpics      = 0x02,
		Contacts      = 0x04,
		Sessions      = 0x08,
		PersonalChats = 0x10,
		PrivateGroups = 0x20,
		PublicGroups  = 0x40,
		MyChannels    = 0x80,
	};
	using Types = base::flags<Type>;
	friend inline constexpr auto is_flag_type(Type) { return true; };

	QString path;
	Output::Format format = Output::Format();

	Types types = DefaultTypes();
	MediaSettings defaultMedia;
	base::flat_map<Type, MediaSettings> customMedia;

	static inline Types DefaultTypes() {
		return Type::PersonalInfo
			| Type::Userpics
			| Type::Contacts
			| Type::Sessions
			| Type::PersonalChats;
	}

};

} // namespace Export
