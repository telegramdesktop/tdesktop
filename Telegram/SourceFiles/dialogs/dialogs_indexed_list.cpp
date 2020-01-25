/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_indexed_list.h"

#include "main/main_session.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "history/history.h"

namespace Dialogs {

IndexedList::IndexedList(SortMode sortMode)
: _sortMode(sortMode)
, _list(sortMode)
, _empty(sortMode) {
}

RowsByLetter IndexedList::addToEnd(Key key) {
	RowsByLetter result;
	if (!_list.contains(key)) {
		result.emplace(0, _list.addToEnd(key));
		for (const auto ch : key.entry()->chatListFirstLetters()) {
			auto j = _index.find(ch);
			if (j == _index.cend()) {
				j = _index.emplace(ch, _sortMode).first;
			}
			result.emplace(ch, j->second.addToEnd(key));
		}
		performFilter();
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
			j = _index.emplace(ch, _sortMode).first;
		}
		j->second.addByName(key);
	}
	performFilter();
	return result;
}

void IndexedList::adjustByDate(const RowsByLetter &links) {
	for (const auto [ch, row] : links) {
		if (ch == QChar(0)) {
			_list.adjustByDate(row);
			performFilter();
		} else {
			if (auto it = _index.find(ch); it != _index.cend()) {
				it->second.adjustByDate(row);
				performFilter();
			}
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
		performFilter();
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
	Auth().data().reorderTwoPinnedChats(
		row->key(),
		(*swapPinnedIndexWith)->key());
	performFilter();
}

void IndexedList::peerNameChanged(
		not_null<PeerData*> peer,
		const base::flat_set<QChar> &oldLetters) {
	Expects(_sortMode != SortMode::Date);

	if (const auto history = peer->owner().historyLoaded(peer)) {
		if (_sortMode == SortMode::Name) {
			adjustByName(history, oldLetters);
		} else {
			adjustNames(Dialogs::Mode::All, history, oldLetters);
		}
		performFilter();
	}
}

void IndexedList::peerNameChanged(
		Mode list,
		not_null<PeerData*> peer,
		const base::flat_set<QChar> &oldLetters) {
	Expects(_sortMode == SortMode::Date);

	if (const auto history = peer->owner().historyLoaded(peer)) {
		adjustNames(list, history, oldLetters);
		performFilter();
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
				j = _index.emplace(ch, _sortMode).first;
			}
			j->second.addByName(key);
		}
	}
}

void IndexedList::adjustNames(
		Mode list,
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
			history->removeChatListEntryByLetter(list, ch);
		}
		if (auto it = _index.find(ch); it != _index.cend()) {
			it->second.del(key, mainRow);
		}
	}
	for (auto ch : toAdd) {
		auto j = _index.find(ch);
		if (j == _index.cend()) {
			j = _index.emplace(ch, _sortMode).first;
		}
		auto row = j->second.addToEnd(key);
		if (_sortMode == SortMode::Date) {
			history->addChatListEntryByLetter(list, ch, row);
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
		performFilter();
	}
}

void IndexedList::setFilterTypes(EntryTypes types) {
	if (types == EntryType::None)
		types = EntryType::All;
	
	if (types != _filterTypes) {
		_filterTypes = types;
		performFilter();
	}
}

void IndexedList::performFilter() {
	emit performFilterStarted();

	if (_filterTypes == EntryType::All) {
		if (_pFiltered) {
			for (auto it = _list.cbegin(); it != _list.cend(); ++it) {
				(*it)->key().entry()->setRowInCurrentTab(nullptr);
			}

			_pFiltered.release();
		}

		emit performFilterFinished();
		return;
	}

	_pFiltered.reset(new List(_list.getSortMode()));

	for (auto it = _list.cbegin(); it != _list.cend(); ++it)
		if ((*it)->entry()->getEntryType() == EntryType::None
		 || ((*it)->entry()->getEntryType() & _filterTypes) != EntryType::None) {
			Row *row = _pFiltered->addToEnd((*it)->key());
			(*it)->key().entry()->setRowInCurrentTab(row);
		}

	emit performFilterFinished();
}

/* counts[private, bot, group, channel] */
void IndexedList::countUnreadMessages(UnreadState counts[4]) const {
	for (auto it = _list.cbegin(); it != _list.cend(); ++it) {
		const Entry *entry = (*it)->entry();
		const EntryTypes type = entry->getEntryType();
		const History *history = (*it)->history();
		int typeIndex = -1;

		if (type & EntryType::Private) typeIndex = 0;
		if (type & EntryType::Bot) typeIndex = 1;
		if (type & EntryType::Group) typeIndex = 2;
		if (type & (EntryType::Channel)) typeIndex = 3;

		if (typeIndex < 0)
			continue;

		auto count = entry->chatListUnreadState();
		if (count.chatsMuted && history->getUnreadMentionsCount()) {
			count.chatsMuted = 0;
			count.messagesMuted -= history->getUnreadMentionsCount();
		}
		counts[typeIndex] += count;
	}
}

List& IndexedList::current() {
	if (_filterTypes != EntryType::All && _pFiltered)
		return *_pFiltered;
	else
		return _list;
}

const List& IndexedList::current() const {
	if (_filterTypes != EntryType::All && _pFiltered)
		return *_pFiltered;
	else
		return _list;
}

bool IndexedList::isFilteredByType() const {
	return _pFiltered.get();
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

IndexedList::~IndexedList() {
	clear();
}

} // namespace Dialogs
