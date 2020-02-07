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

class Session;

class ChatFilter final {
public:
	enum class Flag : uchar {
		Users         = 0x01,
		SecretChats   = 0x02,
		PrivateGroups = 0x04,
		PublicGroups  = 0x08,
		Broadcasts    = 0x10,
		Bots          = 0x20,
		NoMuted       = 0x40,
		NoRead        = 0x80,
	};
	friend constexpr inline bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	ChatFilter() = default;
	ChatFilter(
		const QString &title,
		Flags flags,
		base::flat_set<not_null<History*>> always);

	[[nodiscard]] QString title() const;

	[[nodiscard]] bool contains(not_null<History*> history) const;

private:
	QString _title;
	base::flat_set<not_null<History*>> _always;
	Flags _flags;

};

class ChatFilters final {
public:
	explicit ChatFilters(not_null<Session*> owner);

	[[nodiscard]] const base::flat_map<FilterId, ChatFilter> &list() const;

	void refreshHistory(not_null<History*> history);
	[[nodiscard]] auto refreshHistoryRequests() const
		-> rpl::producer<not_null<History*>>;

private:
	const not_null<Session*> _owner;

	base::flat_map<FilterId, ChatFilter> _list;
	rpl::event_stream<not_null<History*>> _refreshHistoryRequests;

};

} // namespace Data
