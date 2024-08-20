/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace AdminLog {

struct FilterValue final {
	enum class Flag : uint32 {
		Join = (1U << 0),
		Leave = (1U << 1),
		Invite = (1U << 2),
		Ban = (1U << 3),
		Unban = (1U << 4),
		Kick = (1U << 5),
		Unkick = (1U << 6),
		Promote = (1U << 7),
		Demote = (1U << 8),
		Info = (1U << 9),
		Settings = (1U << 10),
		Pinned = (1U << 11),
		Edit = (1U << 12),
		Delete = (1U << 13),
		GroupCall = (1U << 14),
		Invites = (1U << 15),
		Topics = (1U << 16),

		MAX_FIELD = (1U << 16),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr bool is_flag_type(Flag) { return true; };

	// Empty "flags" means all events.
	Flags flags = 0;
	std::vector<not_null<UserData*>> admins;
	bool allUsers = true;

};

inline bool operator==(const FilterValue &a, const FilterValue &b) {
	return (a.flags == b.flags)
		&& (a.admins == b.admins)
		&& (a.allUsers == b.allUsers);
}

inline bool operator!=(const FilterValue &a, const FilterValue &b) {
	return !(a == b);
}

} // namespace AdminLog
