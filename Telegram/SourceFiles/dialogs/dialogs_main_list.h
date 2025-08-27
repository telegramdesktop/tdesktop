/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_common.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_pinned_list.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Thread;
} // namespace Data

namespace Dialogs {

class MainList final {
public:
	MainList(
		not_null<Main::Session*> session,
		FilterId filterId,
		rpl::producer<int> pinnedLimit);

	bool empty() const;
	bool loaded() const;
	void setLoaded(bool loaded = true);
	void setAllAreMuted(bool allAreMuted = true);
	void clear();

	RowsByLetter addEntry(Key key);
	void removeEntry(Key key);

	void unreadStateChanged(
		const UnreadState &wasState,
		const UnreadState &nowState);
	void unreadEntryChanged(const UnreadState &state, bool added);
	void updateCloudUnread(const MTPDdialogFolder &data);
	[[nodiscard]] bool cloudUnreadKnown() const;
	[[nodiscard]] UnreadState unreadState() const;
	[[nodiscard]] rpl::producer<UnreadState> unreadStateChanges() const;

	[[nodiscard]] not_null<IndexedList*> indexed();
	[[nodiscard]] not_null<const IndexedList*> indexed() const;
	[[nodiscard]] not_null<PinnedList*> pinned();
	[[nodiscard]] not_null<const PinnedList*> pinned() const;

	void setCloudListSize(int size);
	[[nodiscard]] const rpl::variable<int> &fullSize() const;

private:
	void finalizeCloudUnread();
	void recomputeFullListSize();

	inline auto unreadStateChangeNotifier(bool notify);

	FilterId _filterId = 0;
	IndexedList _all;
	PinnedList _pinned;
	UnreadState _unreadState;
	UnreadState _cloudUnreadState;
	rpl::event_stream<UnreadState> _unreadStateChanges;
	rpl::variable<int> _fullListSize = 0;
	int _cloudListSize = 0;

	bool _loaded = false;
	bool _allAreMuted = false;

	rpl::lifetime _lifetime;

};

auto MainList::unreadStateChangeNotifier(bool notify) {
	const auto wasState = notify ? unreadState() : UnreadState();
	return gsl::finally([=] {
		if (notify) {
			_unreadStateChanges.fire_copy(wasState);
		}
	});
}

} // namespace Dialogs
