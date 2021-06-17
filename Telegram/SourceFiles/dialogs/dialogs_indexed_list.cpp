/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_indexed_list.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "history/history.h"

namespace Dialogs {

IndexedList::IndexedList(SortMode sortMode, FilterId filterId)
: _sortMode(sortMode)
, _filterId(filterId)
, _list(sortMode, filterId)
, _empty(sortMode, filterId) {
}

RowsByLetter IndexedList::addToEnd(Key key) {
	if (const auto row = _list.getRow(key)) {
		return { row };
	}

	auto result = RowsByLetter{ _list.addToEnd(key) };
	for (const auto ch : key.entry()->chatListFirstLetters()) {
		auto j = _index.find(ch);
		if (j == _index.cend()) {
			j = _index.emplace(ch, _sortMode, _filterId).first;
		}
		result.letters.emplace(ch, j->second.addToEnd(key));
	}
	return result;
}

Row *IndexedList::addByName(Key key) {
	if (const auto row = _list.getRow(key)) {
		return row;
	}

	const auto result = _list.addByName(key);
	for (const auto ch : key.entry()->chatListFirstLetters()) {
		auto j = _index.find(ch);
		if (j == _index.cend()) {
			j = _index.emplace(ch, _sortMode, _filterId).first;
		}
		j->second.addByName(key);
	}
	return result;
}

void IndexedList::adjustByDate(const RowsByLetter &links) {
	_list.adjustByDate(links.main);
	for (const auto &[ch, row] : links.letters) {
		if (auto it = _index.find(ch); it != _index.cend()) {
			it->second.adjustByDate(row);
		}
	}
}

void IndexedList::moveToTop(Key key) {
	if (_list.moveToTop(key)) {
		for (const auto ch : key.entry()->chatListFirstLetters()) {
			if (auto it = _index.find(ch); it != _index.cend()) {
				it->second.moveToTop(key);
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
	row->key().entry()->owner().reorderTwoPinnedChats(
		_filterId,
		row->key(),
		(*swapPinnedIndexWith)->key());
}

void IndexedList::peerNameChanged(
		not_null<PeerData*> peer,
		const base::flat_set<QChar> &oldLetters) {
	Expects(_sortMode != SortMode::Date);

	if (const auto history = peer->owner().historyLoaded(peer)) {
		if (_sortMode == SortMode::Name) {
			adjustByName(history, oldLetters);
		} else {
			adjustNames(FilterId(), history, oldLetters);
		}
	}
}

void IndexedList::peerNameChanged(
		FilterId filterId,
		not_null<PeerData*> peer,
		const base::flat_set<QChar> &oldLetters) {
	Expects(_sortMode == SortMode::Date);

	if (const auto history = peer->owner().historyLoaded(peer)) {
		adjustNames(filterId, history, oldLetters);
	}
}

void IndexedList::adjustByName(
		Key key,
		const base::flat_set<QChar> &oldLetters) {
	Expects(_sortMode == SortMode::Name);

	const auto mainRow = _list.adjustByName(key);
	if (!mainRow) return;

	auto toRemove = oldLetters;
	auto toAdd = base::flat_set<QChar>();
	for (const auto ch : key.entry()->chatListFirstLetters()) {
		auto j = toRemove.find(ch);
		if (j == toRemove.cend()) {
			toAdd.insert(ch);
		} else {
			toRemove.erase(j);
			if (auto it = _index.find(ch); it != _index.cend()) {
				it->second.adjustByName(key);
			}
		}
	}
	for (auto ch : toRemove) {
		if (auto it = _index.find(ch); it != _index.cend()) {
			it->second.del(key, mainRow);
		}
	}
	if (!toAdd.empty()) {
		for (auto ch : toAdd) {
			auto j = _index.find(ch);
			if (j == _index.cend()) {
				j = _index.emplace(ch, _sortMode, _filterId).first;
			}
			j->second.addByName(key);
		}
	}
}

void IndexedList::adjustNames(
		FilterId filterId,
		not_null<History*> history,
		const base::flat_set<QChar> &oldLetters) {
	const auto key = Dialogs::Key(history);
	auto mainRow = _list.getRow(key);
	if (!mainRow) return;

	auto toRemove = oldLetters;
	auto toAdd = base::flat_set<QChar>();
	for (const auto ch : key.entry()->chatListFirstLetters()) {
		auto j = toRemove.find(ch);
		if (j == toRemove.cend()) {
			toAdd.insert(ch);
		} else {
			toRemove.erase(j);
		}
	}
	for (auto ch : toRemove) {
		if (_sortMode == SortMode::Date) {
			history->removeChatListEntryByLetter(filterId, ch);
		}
		if (auto it = _index.find(ch); it != _index.cend()) {
			it->second.del(key, mainRow);
		}
	}
	for (auto ch : toAdd) {
		auto j = _index.find(ch);
		if (j == _index.cend()) {
			j = _index.emplace(ch, _sortMode, _filterId).first;
		}
		auto row = j->second.addToEnd(key);
		if (_sortMode == SortMode::Date) {
			history->addChatListEntryByLetter(filterId, ch, row);
		}
	}
}

void IndexedList::del(Key key, Row *replacedBy) {
	if (_list.del(key, replacedBy)) {
		for (const auto ch : key.entry()->chatListFirstLetters()) {
			if (auto it = _index.find(ch); it != _index.cend()) {
				it->second.del(key, replacedBy);
			}
		}
	}
}

void IndexedList::clear() {
	_index.clear();
}

std::vector<not_null<Row*>> IndexedList::filtered(
		const QStringList &words) const {
	const auto minimal = [&]() -> const Dialogs::List* {
		if (empty()) {
			return nullptr;
		}
		auto result = (const Dialogs::List*)nullptr;
		for (const auto &word : words) {
			if (word.isEmpty()) {
				continue;
			}
			const auto found = filtered(word[0]);
			if (!found || found->empty()) {
				return nullptr;
			} else if (!result || result->size() > found->size()) {
				result = found;
			}
		}
		return result;
	}();
	auto result = std::vector<not_null<Row*>>();
	if (!minimal || minimal->empty()) {
		return result;
	}
	result.reserve(minimal->size());
	for (const auto row : *minimal) {
		const auto &nameWords = row->entry()->chatListNameWords();
		const auto found = [&](const QString &word) {
			for (const auto &name : nameWords) {
				if (name.startsWith(word)) {
					return true;
				}
			}
			return false;
		};
		const auto allFound = [&] {
			for (const auto &word : words) {
				if (!found(word)) {
					return false;
				}
			}
			return true;
		}();
		if (allFound) {
			result.push_back(row);
		}
	}
	return result;
}

} // namespace Dialogs
