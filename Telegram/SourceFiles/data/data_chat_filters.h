/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

class History;

namespace Dialogs {
class MainList;
} // namespace Dialogs

namespace Data {

class Session;

class ChatFilter final {
public:
	enum class Flag : uchar {
		Contacts    = 0x01,
		NonContacts = 0x02,
		Groups      = 0x04,
		Channels    = 0x08,
		Bots        = 0x10,
		NoMuted     = 0x20,
		NoRead      = 0x40,
		NoArchived  = 0x80,
	};
	friend constexpr inline bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	static constexpr int kPinnedLimit = 100;

	ChatFilter() = default;
	ChatFilter(
		FilterId id,
		const QString &title,
		Flags flags,
		base::flat_set<not_null<History*>> always,
		std::vector<not_null<History*>> pinned,
		base::flat_set<not_null<History*>> never);

	[[nodiscard]] static ChatFilter FromTL(
		const MTPDialogFilter &data,
		not_null<Session*> owner);
	[[nodiscard]] MTPDialogFilter tl() const;

	[[nodiscard]] FilterId id() const;
	[[nodiscard]] QString title() const;
	[[nodiscard]] Flags flags() const;
	[[nodiscard]] const base::flat_set<not_null<History*>> &always() const;
	[[nodiscard]] const std::vector<not_null<History*>> &pinned() const;
	[[nodiscard]] const base::flat_set<not_null<History*>> &never() const;

	[[nodiscard]] bool contains(not_null<History*> history) const;

private:
	FilterId _id = 0;
	QString _title;
	base::flat_set<not_null<History*>> _always;
	std::vector<not_null<History*>> _pinned;
	base::flat_set<not_null<History*>> _never;
	Flags _flags;

};

inline bool operator==(const ChatFilter &a, const ChatFilter &b) {
	return (a.title() == b.title())
		&& (a.flags() == b.flags())
		&& (a.always() == b.always())
		&& (a.never() == b.never());
}

inline bool operator!=(const ChatFilter &a, const ChatFilter &b) {
	return !(a == b);
}

class ChatFilters final {
public:
	explicit ChatFilters(not_null<Session*> owner);
	~ChatFilters();

	void load();
	void apply(const MTPUpdate &update);
	void set(ChatFilter filter);
	void remove(FilterId id);
	[[nodiscard]] const std::vector<ChatFilter> &list() const;
	[[nodiscard]] rpl::producer<> changed() const;

	void refreshHistory(not_null<History*> history);
	[[nodiscard]] auto refreshHistoryRequests() const
		-> rpl::producer<not_null<History*>>;

	[[nodiscard]] not_null<Dialogs::MainList*> chatsList(FilterId filterId);

private:
	void load(bool force);
	bool applyOrder(const QVector<MTPint> &order);
	bool applyChange(ChatFilter &filter, ChatFilter &&updated);
	void applyInsert(ChatFilter filter, int position);
	void applyRemove(int position);

	const not_null<Session*> _owner;

	std::vector<ChatFilter> _list;
	base::flat_map<FilterId, std::unique_ptr<Dialogs::MainList>> _chatsLists;
	rpl::event_stream<> _listChanged;
	rpl::event_stream<not_null<History*>> _refreshHistoryRequests;
	mtpRequestId _loadRequestId = 0;

};

} // namespace Data
