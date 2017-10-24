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
, _list(sortMode)
, _empty(sortMode) {
}

RowsByLetter IndexedList::addToEnd(History *history) {
	RowsByLetter result;
	if (!_list.contains(history->peer->id)) {
		result.emplace(0, _list.addToEnd(history));
		for (auto ch : history->peer->nameFirstChars()) {
			auto j = _index.find(ch);
			if (j == _index.cend()) {
				j = _index.emplace(
					ch,
					std::make_unique<List>(_sortMode)).first;
			}
			result.emplace(ch, j->second->addToEnd(history));
		}
	}
	return result;
}

Row *IndexedList::addByName(History *history) {
	if (auto row = _list.getRow(history->peer->id)) {
		return row;
	}

	Row *result = _list.addByName(history);
	for (auto ch : history->peer->nameFirstChars()) {
		auto j = _index.find(ch);
		if (j == _index.cend()) {
			j = _index.emplace(
				ch,
				std::make_unique<List>(_sortMode)).first;
		}
		j->second->addByName(history);
	}
	return result;
}

void IndexedList::adjustByPos(const RowsByLetter &links) {
	for (auto [ch, row] : links) {
		if (ch == QChar(0)) {
			_list.adjustByPos(row);
		} else {
			if (auto it = _index.find(ch); it != _index.cend()) {
				it->second->adjustByPos(row);
			}
		}
	}
}

void IndexedList::moveToTop(not_null<PeerData*> peer) {
	if (_list.moveToTop(peer->id)) {
		for (auto ch : peer->nameFirstChars()) {
			if (auto it = _index.find(ch); it != _index.cend()) {
				it->second->moveToTop(peer->id);
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

void IndexedList::peerNameChanged(not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars) {
	Assert(_sortMode != SortMode::Date);
	if (_sortMode == SortMode::Name) {
		adjustByName(peer, oldChars);
	} else {
		adjustNames(Dialogs::Mode::All, peer, oldChars);
	}
}

void IndexedList::peerNameChanged(Mode list, not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars) {
	Assert(_sortMode == SortMode::Date);
	adjustNames(list, peer, oldChars);
}

void IndexedList::adjustByName(not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars) {
	Row *mainRow = _list.adjustByName(peer);
	if (!mainRow) return;

	History *history = mainRow->history();

	PeerData::NameFirstChars toRemove = oldChars, toAdd;
	for (auto ch : peer->nameFirstChars()) {
		auto j = toRemove.find(ch);
		if (j == toRemove.cend()) {
			toAdd.insert(ch);
		} else {
			toRemove.erase(j);
			if (auto it = _index.find(ch); it != _index.cend()) {
				it->second->adjustByName(peer);
			}
		}
	}
	for (auto ch : toRemove) {
		if (auto it = _index.find(ch); it != _index.cend()) {
			it->second->del(peer->id, mainRow);
		}
	}
	if (!toAdd.empty()) {
		for (auto ch : toAdd) {
			auto j = _index.find(ch);
			if (j == _index.cend()) {
				j = _index.emplace(
					ch,
					std::make_unique<List>(_sortMode)).first;
			}
			j->second->addByName(history);
		}
	}
}

void IndexedList::adjustNames(Mode list, not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars) {
	auto mainRow = _list.getRow(peer->id);
	if (!mainRow) return;

	auto history = mainRow->history();

	PeerData::NameFirstChars toRemove = oldChars, toAdd;
	for (auto ch : peer->nameFirstChars()) {
		auto j = toRemove.find(ch);
		if (j == toRemove.cend()) {
			toAdd.insert(ch);
		} else {
			toRemove.erase(j);
		}
	}
	for (auto ch : toRemove) {
		if (_sortMode == SortMode::Date) {
			history->removeChatListEntryByLetter(list, ch);
		}
		if (auto it = _index.find(ch); it != _index.cend()) {
			it->second->del(peer->id, mainRow);
		}
	}
	for (auto ch : toAdd) {
		auto j = _index.find(ch);
		if (j == _index.cend()) {
			j = _index.emplace(
				ch,
				std::make_unique<List>(_sortMode)).first;
		}
		auto row = j->second->addToEnd(history);
		if (_sortMode == SortMode::Date) {
			history->addChatListEntryByLetter(list, ch, row);
		}
	}
}

void IndexedList::del(not_null<const PeerData*> peer, Row *replacedBy) {
	if (_list.del(peer->id, replacedBy)) {
		for (auto ch : peer->nameFirstChars()) {
			if (auto it = _index.find(ch); it != _index.cend()) {
				it->second->del(peer->id, replacedBy);
			}
		}
	}
}

void IndexedList::clear() {
	_index.clear();
}

IndexedList::~IndexedList() {
	clear();
}

} // namespace Dialogs
