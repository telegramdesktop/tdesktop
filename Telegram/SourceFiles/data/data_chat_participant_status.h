/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

enum class UserRestriction {
	SendVoiceMessages,
	SendVideoMessages,
};

enum class ChatAdminRight {
	ChangeInfo = (1 << 0),
	PostMessages = (1 << 1),
	EditMessages = (1 << 2),
	DeleteMessages = (1 << 3),
	BanUsers = (1 << 4),
	InviteUsers = (1 << 5),
	PinMessages = (1 << 7),
	AddAdmins = (1 << 9),
	Anonymous = (1 << 10),
	ManageCall = (1 << 11),
	Other = (1 << 12),
};
inline constexpr bool is_flag_type(ChatAdminRight) { return true; }
using ChatAdminRights = base::flags<ChatAdminRight>;

enum class ChatRestriction {
	ViewMessages = (1 << 0),
	SendMessages = (1 << 1),
	SendMedia = (1 << 2),
	SendStickers = (1 << 3),
	SendGifs = (1 << 4),
	SendGames = (1 << 5),
	SendInline = (1 << 6),
	EmbedLinks = (1 << 7),
	SendPolls = (1 << 8),
	ChangeInfo = (1 << 10),
	InviteUsers = (1 << 15),
	PinMessages = (1 << 17),
};
inline constexpr bool is_flag_type(ChatRestriction) { return true; }
using ChatRestrictions = base::flags<ChatRestriction>;

struct ChatAdminRightsInfo {
	ChatAdminRightsInfo() = default;
	explicit ChatAdminRightsInfo(ChatAdminRights flags) : flags(flags) {
	}
	explicit ChatAdminRightsInfo(const MTPChatAdminRights &rights);

	ChatAdminRights flags;
};

struct ChatRestrictionsInfo {
	ChatRestrictionsInfo() = default;
	ChatRestrictionsInfo(ChatRestrictions flags, TimeId until)
	: flags(flags)
	, until(until) {
	}
	explicit ChatRestrictionsInfo(const MTPChatBannedRights &rights);

	ChatRestrictions flags;
	TimeId until = 0;
};

namespace Data {

std::vector<ChatRestrictions> ListOfRestrictions();

} // namespace Data
