/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_entry.h"

#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_chat_filters.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "history/history_item.h"
#include "history/history.h"
#include "app.h"
#include "styles/style_dialogs.h" // st::dialogsTextWidthMin

namespace Dialogs {
namespace {

auto DialogsPosToTopShift = 0;

uint64 DialogPosFromDate(TimeId date) {
	if (!date) {
		return 0;
	}
	return (uint64(date) << 32) | (++DialogsPosToTopShift);
}

uint64 FixedOnTopDialogPos(int index) {
	return 0xFFFFFFFFFFFF000FULL - index;
}

uint64 PinnedDialogPos(int pinnedIndex) {
	return 0xFFFFFFFF000000FFULL - pinnedIndex;
}

} // namespace

Entry::Entry(not_null<Data::Session*> owner, Type type)
: lastItemTextCache(st::dialogsTextWidthMin)
, _owner(owner)
, _isFolder(type == Type::Folder) {
}

Data::Session &Entry::owner() const {
	return *_owner;
}

Main::Session &Entry::session() const {
	return _owner->session();
}

History *Entry::asHistory() {
	return _isFolder ? nullptr : static_cast<History*>(this);
}

Data::Folder *Entry::asFolder() {
	return _isFolder ? static_cast<Data::Folder*>(this) : nullptr;
}

void Entry::pinnedIndexChanged(int was, int now) {
	if (session().supportMode()) {
		// Force reorder in support mode.
		_sortKeyInChatList = 0;
	}
	updateChatListSortPosition();
	updateChatListEntry();
	if ((was != 0) != (now != 0)) {
		changedChatListPinHook();
	}
}

void Entry::cachePinnedIndex(FilterId filterId, int index) {
	const auto i = _pinnedIndex.find(filterId);
	const auto was = (i != end(_pinnedIndex)) ? i->second : 0;
	if (index == was) {
		return;
	}
	if (!index) {
		_pinnedIndex.erase(i);
		pinnedIndexChanged(was, index);
	} else {
		if (!was) {
			_pinnedIndex.emplace(filterId, index);
		} else {
			i->second = index;
		}
		pinnedIndexChanged(was, index);
	}
}

void Entry::cacheTopPromoted(bool promoted) {
	if (_isTopPromoted == promoted) {
		return;
	}
	_isTopPromoted = promoted;
	updateChatListSortPosition();
	updateChatListEntry();
	if (!_isTopPromoted) {
		updateChatListExistence();
	}
}

bool Entry::isTopPromoted() const {
	return _isTopPromoted;
}

bool Entry::needUpdateInChatList() const {
	return inChatList() || shouldBeInChatList();
}

void Entry::updateChatListSortPosition() {
	if (session().supportMode()
		&& _sortKeyInChatList != 0
		&& session().settings().supportFixChatsOrder()) {
		updateChatListEntry();
		return;
	}
	_sortKeyByDate = DialogPosFromDate(adjustedChatListTimeId());
	const auto fixedIndex = fixedOnTopIndex();
	_sortKeyInChatList = fixedIndex
		? FixedOnTopDialogPos(fixedIndex)
		: computeSortPosition(0);
	if (needUpdateInChatList()) {
		setChatListExistence(true);
	} else {
		_sortKeyInChatList = _sortKeyByDate = 0;
	}
}

int Entry::lookupPinnedIndex(FilterId filterId) const {
	if (filterId) {
		const auto i = _pinnedIndex.find(filterId);
		return (i != end(_pinnedIndex)) ? i->second : 0;
	} else if (!_pinnedIndex.empty()) {
		return _pinnedIndex.front().first
			? 0
			: _pinnedIndex.front().second;
	}
	return 0;
}

uint64 Entry::computeSortPosition(FilterId filterId) const {
	const auto index = lookupPinnedIndex(filterId);
	return index ? PinnedDialogPos(index) : _sortKeyByDate;
}

void Entry::updateChatListExistence() {
	setChatListExistence(shouldBeInChatList());
}

void Entry::notifyUnreadStateChange(const UnreadState &wasState) {
	Expects(folderKnown());
	Expects(inChatList());

	const auto nowState = chatListUnreadState();
	owner().chatsList(folder())->unreadStateChanged(wasState, nowState);
	auto &filters = owner().chatsFilters();
	for (const auto &[filterId, links] : _chatListLinks) {
		filters.chatsList(filterId)->unreadStateChanged(wasState, nowState);
	}
}

void Entry::setChatListExistence(bool exists) {
	if (exists && _sortKeyInChatList) {
		owner().refreshChatListEntry(this);
		updateChatListEntry();
	} else {
		owner().removeChatListEntry(this);
	}
}

TimeId Entry::adjustedChatListTimeId() const {
	return chatListTimeId();
}

void Entry::changedChatListPinHook() {
}

RowsByLetter *Entry::chatListLinks(FilterId filterId) {
	const auto i = _chatListLinks.find(filterId);
	return (i != end(_chatListLinks)) ? &i->second : nullptr;
}

const RowsByLetter *Entry::chatListLinks(FilterId filterId) const {
	const auto i = _chatListLinks.find(filterId);
	return (i != end(_chatListLinks)) ? &i->second : nullptr;
}

not_null<Row*> Entry::mainChatListLink(FilterId filterId) const {
	const auto links = chatListLinks(filterId);
	Assert(links != nullptr);
	return links->main;
}

Row *Entry::maybeMainChatListLink(FilterId filterId) const {
	const auto links = chatListLinks(filterId);
	return links ? links->main.get() : nullptr;
}

PositionChange Entry::adjustByPosInChatList(
		FilterId filterId,
		not_null<MainList*> list) {
	const auto links = chatListLinks(filterId);
	Assert(links != nullptr);
	const auto from = links->main->pos();
	list->indexed()->adjustByDate(*links);
	const auto to = links->main->pos();
	return { from, to };
}

void Entry::setChatListTimeId(TimeId date) {
	_timeId = date;
	updateChatListSortPosition();
	if (const auto folder = this->folder()) {
		folder->updateChatListSortPosition();
	}
}

int Entry::posInChatList(FilterId filterId) const {
	return mainChatListLink(filterId)->pos();
}

not_null<Row*> Entry::addToChatList(
		FilterId filterId,
		not_null<MainList*> list) {
	if (const auto main = maybeMainChatListLink(filterId)) {
		return main;
	}
	return _chatListLinks.emplace(
		filterId,
		list->addEntry(this)
	).first->second.main;
}

void Entry::removeFromChatList(
		FilterId filterId,
		not_null<MainList*> list) {
	if (isPinnedDialog(filterId)) {
		owner().setChatPinned(this, filterId, false);
	}

	const auto i = _chatListLinks.find(filterId);
	if (i == end(_chatListLinks)) {
		return;
	}
	_chatListLinks.erase(i);
	list->removeEntry(this);
}

void Entry::removeChatListEntryByLetter(FilterId filterId, QChar letter) {
	const auto i = _chatListLinks.find(filterId);
	if (i != end(_chatListLinks)) {
		i->second.letters.remove(letter);
	}
}

void Entry::addChatListEntryByLetter(
		FilterId filterId,
		QChar letter,
		not_null<Row*> row) {
	const auto i = _chatListLinks.find(filterId);
	if (i != end(_chatListLinks)) {
		i->second.letters.emplace(letter, row);
	}
}

void Entry::updateChatListEntry() {
	session().changes().entryUpdated(this, Data::EntryUpdate::Flag::Repaint);
}

} // namespace Dialogs
