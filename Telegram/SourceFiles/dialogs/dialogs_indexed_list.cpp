/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "dialogs/dialogs_indexed_list.h"

namespace Dialogs {

IndexedList::IndexedList(SortMode sortMode)
: _sortMode(sortMode)
, _list(sortMode) {
}

RowsByLetter IndexedList::addToEnd(History *history) {
	RowsByLetter result;
	if (!_list.contains(history->peer->id)) {
		result.insert(0, _list.addToEnd(history));
		for_const (auto ch, history->peer->chars) {
			auto j = _index.find(ch);
			if (j == _index.cend()) {
				j = _index.insert(ch, new List(_sortMode));
			}
			result.insert(ch, j.value()->addToEnd(history));
		}
	}
	return result;
}

Row *IndexedList::addByName(History *history) {
	if (auto row = _list.getRow(history->peer->id)) {
		return row;
	}

	Row *result = _list.addByName(history);
	for_const (auto ch, history->peer->chars) {
		auto j = _index.find(ch);
		if (j == _index.cend()) {
			j = _index.insert(ch, new List(_sortMode));
		}
		j.value()->addByName(history);
	}
	return result;
}

void IndexedList::adjustByPos(const RowsByLetter &links) {
	for (auto i = links.cbegin(), e = links.cend(); i != e; ++i) {
		if (i.key() == QChar(0)) {
			_list.adjustByPos(i.value());
		} else {
			if (auto list = _index.value(i.key())) {
				list->adjustByPos(i.value());
			}
		}
	}
}

void IndexedList::moveToTop(PeerData *peer) {
	if (_list.moveToTop(peer->id)) {
		for_const (auto ch, peer->chars) {
			if (auto list = _index.value(ch)) {
				list->moveToTop(peer->id);
			}
		}
	}
}

void IndexedList::movePinned(Row *row, int deltaSign) {
	auto swapPinnedIndexWith = find(row);
	Assert(swapPinnedIndexWith != cend());
	if (deltaSign > 0) {
		++swapPinnedIndexWith;
	} else {
		Assert(swapPinnedIndexWith != cbegin());
		--swapPinnedIndexWith;
	}
	auto history1 = row->history();
	auto history2 = (*swapPinnedIndexWith)->history();
	Assert(history1->isPinnedDialog());
	Assert(history2->isPinnedDialog());
	auto index1 = history1->getPinnedIndex();
	auto index2 = history2->getPinnedIndex();
	history1->setPinnedIndex(index2);
	history2->setPinnedIndex(index1);
}

void IndexedList::peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	Assert(_sortMode != SortMode::Date);
	if (_sortMode == SortMode::Name) {
		adjustByName(peer, oldNames, oldChars);
	} else {
		adjustNames(Dialogs::Mode::All, peer, oldNames, oldChars);
	}
}

void IndexedList::peerNameChanged(Mode list, PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	Assert(_sortMode == SortMode::Date);
	adjustNames(list, peer, oldNames, oldChars);
}

void IndexedList::adjustByName(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	Row *mainRow = _list.adjustByName(peer);
	if (!mainRow) return;

	History *history = mainRow->history();

	PeerData::NameFirstChars toRemove = oldChars, toAdd;
	for_const (auto ch, peer->chars) {
		auto j = toRemove.find(ch);
		if (j == toRemove.cend()) {
			toAdd.insert(ch);
		} else {
			toRemove.erase(j);
			if (auto list = _index.value(ch)) {
				list->adjustByName(peer);
			}
		}
	}
	for_const (auto ch, toRemove) {
		if (auto list = _index.value(ch)) {
			list->del(peer->id, mainRow);
		}
	}
	if (!toAdd.isEmpty()) {
		for_const (auto ch, toAdd) {
			auto j = _index.find(ch);
			if (j == _index.cend()) {
				j = _index.insert(ch, new List(_sortMode));
			}
			j.value()->addByName(history);
		}
	}
}

void IndexedList::adjustNames(Mode list, PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	auto mainRow = _list.getRow(peer->id);
	if (!mainRow) return;

	History *history = mainRow->history();

	PeerData::NameFirstChars toRemove = oldChars, toAdd;
	for_const (auto ch, peer->chars) {
		auto j = toRemove.find(ch);
		if (j == toRemove.cend()) {
			toAdd.insert(ch);
		} else {
			toRemove.erase(j);
		}
	}
	for_const (auto ch, toRemove) {
		if (_sortMode == SortMode::Date) {
			history->removeChatListEntryByLetter(list, ch);
		}
		if (auto list = _index.value(ch)) {
			list->del(peer->id, mainRow);
		}
	}
	for_const (auto ch, toAdd) {
		auto j = _index.find(ch);
		if (j == _index.cend()) {
			j = _index.insert(ch, new List(_sortMode));
		}
		Row *row = j.value()->addToEnd(history);
		if (_sortMode == SortMode::Date) {
			history->addChatListEntryByLetter(list, ch, row);
		}
	}
}

void IndexedList::del(const PeerData *peer, Row *replacedBy) {
	if (_list.del(peer->id, replacedBy)) {
		for_const (auto ch, peer->chars) {
			if (auto list = _index.value(ch)) {
				list->del(peer->id, replacedBy);
			}
		}
	}
}

void IndexedList::clear() {
	for_const (auto &list, _index) {
		delete list;
	}
}

IndexedList::~IndexedList() {
	clear();
}

} // namespace Dialogs
