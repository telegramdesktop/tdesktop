/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

class History;

namespace Data {

class ChatFilter final {
public:
	enum class Flag : uchar {
		Users         = 0x01,
		PrivateGroups = 0x02,
		PublicGroups  = 0x04,
		Broadcasts    = 0x08,
		Bots          = 0x10,
		NoMuted       = 0x20,
		NoRead        = 0x40,
	};
	friend constexpr inline bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	ChatFilter() = default;
	ChatFilter(
		const QString &title,
		Flags flags,
		base::flat_set<not_null<History*>> always);

	[[nodiscard]] bool contains(not_null<History*> history) const;

private:
	QString _title;
	base::flat_set<not_null<History*>> _always;
	Flags _flags;

};

} // namespace Data
