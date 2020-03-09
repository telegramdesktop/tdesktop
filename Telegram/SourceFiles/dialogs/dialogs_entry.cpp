/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_entry.h"

#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_chat_filters.h"
#include "mainwidget.h"
#include "main/main_session.h"
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

Entry::Entry(not_null<Data::Session*> owner, const Key &key)
: lastItemTextCache(st::dialogsTextWidthMin)
, _owner(owner)
, _key(key) {
}

Data::Session &Entry::owner() const {
	return *_owner;
}

Main::Session &Entry::session() const {
	return _owner->session();
}

void Entry::cachePinnedIndex(int index) {
	if (_pinnedIndex != index) {
		const auto wasPinned = isPinnedDialog();
		_pinnedIndex = index;
		if (session().supportMode()) {
			// Force reorder in support mode.
			_sortKeyInChatList = 0;
		}
		updateChatListSortPosition();
		updateChatListEntry();
		if (wasPinned != isPinnedDialog()) {
			changedChatListPinHook();
		}
	}
}

void Entry::cacheProxyPromoted(bool promoted) {
	if (_isProxyPromoted != promoted) {
		_isProxyPromoted = promoted;
		updateChatListSortPosition();
		updateChatListEntry();
		if (!_isProxyPromoted) {
			updateChatListExistence();
		}
	}
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
		: isPinnedDialog()
		? PinnedDialogPos(_pinnedIndex)
		: _sortKeyByDate;
	if (needUpdateInChatList()) {
		setChatListExistence(true);
	} else {
		_sortKeyInChatList = _sortKeyByDate = 0;
	}
}

void Entry::updateChatListExistence() {
	setChatListExistence(shouldBeInChatList());
}

void Entry::notifyUnreadStateChange(const UnreadState &wasState) {
	owner().unreadStateChanged(_key, wasState);
}

void Entry::setChatListExistence(bool exists) {
	if (const auto main = App::main()) {
		if (exists && _sortKeyInChatList) {
			main->refreshDialog(_key);
			updateChatListEntry();
		} else {
			main->removeDialog(_key);
		}
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

PositionChange Entry::adjustByPosInChatList(FilterId filterId) {
	const auto links = chatListLinks(filterId);
	Assert(links != nullptr);
	const auto from = links->main->pos();
	myChatsList(filterId)->adjustByDate(*links);
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

not_null<Row*> Entry::addToChatList(FilterId filterId) {
	if (const auto main = maybeMainChatListLink(filterId)) {
		return main;
	}
	const auto result = _chatListLinks.emplace(
		filterId,
		myChatsList(filterId)->addToEnd(_key)
	).first->second.main;
	if (!filterId) {
		owner().unreadEntryChanged(_key, true);
	}
	return result;
}

void Entry::removeFromChatList(FilterId filterId) {
	const auto i = _chatListLinks.find(filterId);
	if (i == end(_chatListLinks)) {
		return;
	}
	myChatsList(filterId)->del(_key);
	_chatListLinks.erase(i);
	if (!filterId) {
		owner().unreadEntryChanged(_key, false);
	}
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

void Entry::updateChatListEntry() const {
	if (const auto main = App::main()) {
		for (const auto &[filterId, links] : _chatListLinks) {
			main->repaintDialogRow(filterId, links.main);
		}
		if (session().supportMode()
			&& !session().settings().supportAllSearchResults()) {
			main->repaintDialogRow({ _key, FullMsgId() });
		}
	}
}

not_null<IndexedList*> Entry::myChatsList(FilterId filterId) const {
	return filterId
		? owner().chatsFilters().chatsList(filterId)->indexed()
		: owner().chatsList(folder())->indexed();
}

} // namespace Dialogs
