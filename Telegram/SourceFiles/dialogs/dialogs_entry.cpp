/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_entry.h"

#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_indexed_list.h"
#include "mainwidget.h"
#include "styles/style_dialogs.h"
#include "history/history_item.h"
#include "history/history.h"

namespace Dialogs {
namespace {

auto DialogsPosToTopShift = 0;

uint64 DialogPosFromDate(TimeId date) {
	if (!date) {
		return 0;
	}
	return (uint64(date) << 32) | (++DialogsPosToTopShift);
}

uint64 ProxyPromotedDialogPos() {
	return 0xFFFFFFFFFFFF0001ULL;
}

uint64 PinnedDialogPos(int pinnedIndex) {
	return 0xFFFFFFFF00000000ULL + pinnedIndex;
}

} // namespace

Entry::Entry(const Key &key)
: lastItemTextCache(st::dialogsTextWidthMin)
, _key(key) {
}

void Entry::cachePinnedIndex(int index) {
	if (_pinnedIndex != index) {
		const auto wasPinned = isPinnedDialog();
		_pinnedIndex = index;
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
	return inChatList(Dialogs::Mode::All) || shouldBeInChatList();
}

void Entry::updateChatListSortPosition() {
	_sortKeyInChatList = useProxyPromotion()
		? ProxyPromotedDialogPos()
		: isPinnedDialog()
		? PinnedDialogPos(_pinnedIndex)
		: DialogPosFromDate(adjustChatListTimeId());
	if (needUpdateInChatList()) {
		setChatListExistence(true);
	}
}

void Entry::updateChatListExistence() {
	setChatListExistence(shouldBeInChatList());
}

void Entry::setChatListExistence(bool exists) {
	if (const auto main = App::main()) {
		if (exists && _sortKeyInChatList) {
			main->createDialog(_key);
			updateChatListEntry();
		} else {
			main->removeDialog(_key);
		}
	}
}

TimeId Entry::adjustChatListTimeId() const {
	return chatsListTimeId();
}

void Entry::changedInChatListHook(Dialogs::Mode list, bool added) {
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

PositionChange Entry::adjustByPosInChatList(
		Mode list,
		not_null<IndexedList*> indexed) {
	const auto lnk = mainChatListLink(list);
	const auto movedFrom = lnk->pos();
	indexed->adjustByPos(chatListLinks(list));
	const auto movedTo = lnk->pos();
	return { movedFrom, movedTo };
}

void Entry::setChatsListTimeId(TimeId date) {
	if (_lastMessageTimeId && _lastMessageTimeId >= date) {
		if (!inChatList(Dialogs::Mode::All)) {
			return;
		}
	}
	_lastMessageTimeId = date;
	updateChatListSortPosition();
}

int Entry::posInChatList(Dialogs::Mode list) const {
	return mainChatListLink(list)->pos();
}

not_null<Row*> Entry::addToChatList(
		Mode list,
		not_null<IndexedList*> indexed) {
	if (!inChatList(list)) {
		chatListLinks(list) = indexed->addToEnd(_key);
		changedInChatListHook(list, true);
	}
	return mainChatListLink(list);
}

void Entry::removeFromChatList(
		Dialogs::Mode list,
		not_null<Dialogs::IndexedList*> indexed) {
	if (inChatList(list)) {
		indexed->del(_key);
		chatListLinks(list).clear();
		changedInChatListHook(list, false);
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
		if (inChatList(Mode::All)) {
			main->repaintDialogRow(
				Mode::All,
				mainChatListLink(Mode::All));
			if (inChatList(Mode::Important)) {
				main->repaintDialogRow(
					Mode::Important,
					mainChatListLink(Mode::Important));
			}
		}
	}
}

} // namespace Dialogs
