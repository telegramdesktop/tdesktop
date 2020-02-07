/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_pinned_list.h"

namespace Dialogs {

class MainList final {
public:
	explicit MainList(rpl::producer<int> pinnedLimit);

	bool empty() const;
	bool loaded() const;
	void setLoaded(bool loaded = true);
	void clear();

	void unreadStateChanged(
		const UnreadState &wasState,
		const UnreadState &nowState);
	void unreadEntryChanged(
		const Dialogs::UnreadState &state,
		bool added);
	[[nodiscard]] UnreadState unreadState() const;

	not_null<IndexedList*> indexed(FilterId filterId = 0);
	not_null<const IndexedList*> indexed() const;
	not_null<PinnedList*> pinned();
	not_null<const PinnedList*> pinned() const;

private:
	IndexedList _all;
	base::flat_map<FilterId, IndexedList> _other;
	PinnedList _pinned;
	UnreadState _unreadState;

	bool _loaded = false;

	rpl::lifetime _lifetime;

};

} // namespace Dialogs
