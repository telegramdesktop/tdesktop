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

uint64 DialogPosFromDate(const QDateTime &date) {
	if (date.isNull()) {
		return 0;
	}
	return (uint64(date.toTime_t()) << 32) | (++DialogsPosToTopShift);
}

uint64 PinnedDialogPos(int pinnedIndex) {
	return 0xFFFFFFFF00000000ULL + pinnedIndex;
}

} // namespace

bool MessageIsLess(not_null<HistoryItem*> a, not_null<HistoryItem*> b) {
	if (a->date < b->date) {
		return true;
	} else if (b->date < a->date) {
		return false;
	}
	const auto apeer = a->history()->peer->bareId();
	const auto bpeer = b->history()->peer->bareId();
	if (apeer < bpeer) {
		return true;
	} else if (bpeer < apeer) {
		return false;
	}
	return a->id < b->id;
}

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

void Entry::updateChatListSortPosition() {
	_sortKeyInChatList = isPinnedDialog()
		? PinnedDialogPos(_pinnedIndex)
		: DialogPosFromDate(adjustChatListDate());
	if (auto m = App::main()) {
		if (needUpdateInChatList()) {
			if (_sortKeyInChatList) {
				m->createDialog(_key);
				updateChatListEntry();
			} else {
				removeDialog();
			}
		}
	}
}

void Entry::removeDialog() {
	if (const auto main = App::main()) {
		main->removeDialog(_key);
	}
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

void Entry::setChatsListDate(const QDateTime &date) {
	if (!_lastMessageDate.isNull() && _lastMessageDate >= date) {
		if (!needUpdateInChatList() || !inChatList(Dialogs::Mode::All)) {
			return;
		}
	}
	_lastMessageDate = date;
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
