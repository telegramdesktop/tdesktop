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
	UnreadState unreadState() const;

	not_null<IndexedList*> indexed(Mode list = Mode::All);
	not_null<const IndexedList*> indexed(Mode list = Mode::All) const;
	not_null<PinnedList*> pinned();
	not_null<const PinnedList*> pinned() const;

private:
	IndexedList _all;
	IndexedList _important;
	PinnedList _pinned;
	UnreadState _unreadState;

	bool _loaded = false;

	rpl::lifetime _lifetime;

};

} // namespace Dialogs
