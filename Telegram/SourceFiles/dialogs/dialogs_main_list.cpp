/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_main_list.h"

#include "observer_peer.h"
#include "history/history.h"

namespace Dialogs {
namespace {

UnreadState ApplyMarkToCounters(const UnreadState &state) {
	auto result = UnreadState();
	const auto count = state.messagesCount.value_or(0);
	result.messagesCount = (count > 0)
		? count
		: state.mark
		? 1
		: 0;
	result.messagesCountMuted = (state.messagesCountMuted > 0)
		? state.messagesCountMuted
		: state.markMuted
		? 1
		: 0;
	result.chatsCount = (state.chatsCount > 0)
		? state.chatsCount
		: state.mark
		? 1
		: 0;
	result.chatsCountMuted = (state.chatsCountMuted > 0)
		? state.chatsCountMuted
		: state.markMuted
		? 1
		: 0;
	return result;
}

} // namespace

MainList::MainList(rpl::producer<int> pinnedLimit)
: _all(SortMode::Date)
, _important(SortMode::Date)
, _pinned(1) {
	_unreadState.messagesCount = 0;

	std::move(
		pinnedLimit
	) | rpl::start_with_next([=](int limit) {
		_pinned.setLimit(limit);
	}, _lifetime);

	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::NameChanged
	) | rpl::start_with_next([=](const Notify::PeerUpdate &update) {
		const auto peer = update.peer;
		const auto &oldLetters = update.oldNameFirstLetters;
		_all.peerNameChanged(Mode::All, peer, oldLetters);
		_important.peerNameChanged(Mode::Important, peer, oldLetters);
	}, _lifetime);
}

bool MainList::empty() const {
	return _all.empty();
}

bool MainList::loaded() const {
	return _loaded;
}

void MainList::setLoaded(bool loaded) {
	_loaded = loaded;
}

void MainList::clear() {
	_all.clear();
	_important.clear();
	_unreadState = UnreadState();
}

void MainList::unreadStateChanged(
		const UnreadState &wasState,
		const UnreadState &nowState) {
	const auto wasWithMark = ApplyMarkToCounters(wasState);
	const auto nowWithMark = ApplyMarkToCounters(nowState);
	*_unreadState.messagesCount += *nowWithMark.messagesCount
		- *wasWithMark.messagesCount;
	_unreadState.messagesCountMuted += nowWithMark.messagesCountMuted
		- wasWithMark.messagesCountMuted;
	_unreadState.chatsCount += nowWithMark.chatsCount
		- wasWithMark.chatsCount;
	_unreadState.chatsCountMuted += nowWithMark.chatsCountMuted
		- wasWithMark.chatsCountMuted;
}

void MainList::unreadEntryChanged(
		const Dialogs::UnreadState &state,
		bool added) {
	const auto withMark = ApplyMarkToCounters(state);
	const auto delta = (added ? 1 : -1);
	*_unreadState.messagesCount += delta * *withMark.messagesCount;
	_unreadState.messagesCountMuted += delta * withMark.messagesCountMuted;
	_unreadState.chatsCount += delta * withMark.chatsCount;
	_unreadState.chatsCountMuted += delta * withMark.chatsCountMuted;
}

UnreadState MainList::unreadState() const {
	return _unreadState;
}

not_null<IndexedList*> MainList::indexed(Mode list) {
	return (list == Mode::All) ? &_all : &_important;
}

not_null<const IndexedList*> MainList::indexed(Mode list) const {
	return (list == Mode::All) ? &_all : &_important;
}

not_null<PinnedList*> MainList::pinned() {
	return &_pinned;
}

not_null<const PinnedList*> MainList::pinned() const {
	return &_pinned;
}

} // namespace Dialogs
