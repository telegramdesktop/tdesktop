/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

class UserData;

namespace Data {

enum class BusinessChatType {
	NewChats = (1 << 0),
	ExistingChats = (1 << 1),
	Contacts = (1 << 2),
	NonContacts = (1 << 3),
};
inline constexpr bool is_flag_type(BusinessChatType) { return true; }

using BusinessChatTypes = base::flags<BusinessChatType>;

struct BusinessChats {
	BusinessChatTypes types;
	std::vector<not_null<UserData*>> list;

	friend inline bool operator==(
		const BusinessChats &a,
		const BusinessChats &b) = default;
};

struct BusinessRecipients {
	BusinessChats included;
	BusinessChats excluded;
	bool onlyIncluded = false;

	friend inline bool operator==(
		const BusinessRecipients &a,
		const BusinessRecipients &b) = default;
};

} // namespace Data
