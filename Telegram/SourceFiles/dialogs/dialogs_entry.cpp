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
#include "mainwidget.h"
#include "auth_session.h"
#include "history/history_item.h"
#include "history/history.h"
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

AuthSession &Entry::session() const {
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
	const auto fixedIndex = fixedOnTopIndex();
	_sortKeyInChatList = fixedIndex
		? FixedOnTopDialogPos(fixedIndex)
		: isPinnedDialog()
		? PinnedDialogPos(_pinnedIndex)
		: DialogPosFromDate(adjustedChatListTimeId());
	if (needUpdateInChatList()) {
		setChatListExistence(true);
	} else {
		_sortKeyInChatList = 0;
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

RowsByLetter &Entry::chatListLinks(Mode list) {
	return _chatListLinks[static_cast<int>(list)];
}

const RowsByLetter &Entry::chatListLinks(Mode list) const {
	return _chatListLinks[static_cast<int>(list)];
}

Row *Entry::mainChatListLink(Mode list) const {
	auto it = chatListLinks(list).find(0);
	Assert(it != chatListLinks(list).cend());
	return it->second;
}

PositionChange Entry::adjustByPosInChatList(Mode list) {
	const auto lnk = mainChatListLink(list);
	const auto from = lnk->pos();
	myChatsList(list)->adjustByDate(chatListLinks(list));
	const auto to = lnk->pos();
	return { from, to };
}

void Entry::setChatListTimeId(TimeId date) {
	_timeId = date;
	updateChatListSortPosition();
	if (const auto folder = this->folder()) {
		folder->updateChatListSortPosition();
	}
}

int Entry::posInChatList(Dialogs::Mode list) const {
	return mainChatListLink(list)->pos();
}

not_null<Row*> Entry::addToChatList(Mode list) {
	if (!inChatList(list)) {
		chatListLinks(list) = myChatsList(list)->addToEnd(_key);
		if (list == Mode::All) {
			owner().unreadEntryChanged(_key, true);
		}
	}
	return mainChatListLink(list);
}

void Entry::removeFromChatList(Dialogs::Mode list) {
	if (inChatList(list)) {
		myChatsList(list)->del(_key);
		chatListLinks(list).clear();
		if (list == Mode::All) {
			owner().unreadEntryChanged(_key, false);
		}
	}
}

void Entry::removeChatListEntryByLetter(Mode list, QChar letter) {
	Expects(letter != 0);

	if (inChatList(list)) {
		chatListLinks(list).remove(letter);
	}
}

void Entry::addChatListEntryByLetter(
		Mode list,
		QChar letter,
		not_null<Row*> row) {
	Expects(letter != 0);

	if (inChatList(list)) {
		chatListLinks(list).emplace(letter, row);
	}
}

void Entry::updateChatListEntry() const {
	if (const auto main = App::main()) {
		if (inChatList()) {
			main->repaintDialogRow(
				Mode::All,
				mainChatListLink(Mode::All));
			if (inChatList(Mode::Important)) {
				main->repaintDialogRow(
					Mode::Important,
					mainChatListLink(Mode::Important));
			}
		}
		if (session().supportMode()
			&& !session().settings().supportAllSearchResults()) {
			main->repaintDialogRow({ _key, FullMsgId() });
		}
	}
}

not_null<IndexedList*> Entry::myChatsList(Mode list) const {
	return owner().chatsList(folder())->indexed(list);
}

} // namespace Dialogs
