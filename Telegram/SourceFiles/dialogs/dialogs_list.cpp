/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_list.h"

#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"
#include "mainwidget.h"

namespace Dialogs {

List::List(SortMode sortMode)
: _last(std::make_unique<Row>(nullptr))
, _begin(_last.get())
, _end(_last.get())
, _sortMode(sortMode)
, _current(_last.get()) {
}

void List::adjustCurrent(int32 y, int32 h) const {
	if (isEmpty()) return;

	int32 pos = (y > 0) ? (y / h) : 0;
	while (_current->_pos > pos && _current != _begin) {
		_current = _current->_prev;
	}
	while (_current->_pos + 1 <= pos && _current->_next != _end) {
		_current = _current->_next;
	}
}

Row *List::addToEnd(Key key) {
	const auto result = new Row(key, _end->_prev, _end, _end->_pos);
	_end->_pos++;
	if (_begin == _end) {
		_begin = _current = result;
	} else {
		_end->_prev->_next = result;
	}
	_rowByKey.emplace(key, result);
	++_count;
	_end->_prev = result;
	if (_sortMode == SortMode::Date) {
		adjustByPos(result);
	}
	return result;
}

bool List::insertBefore(Row *row, Row *before) {
	if (row == before) {
		return false;
	}

	if (_current == row) {
		_current = row->_prev;
	}

	const auto updateTill = row->_prev;
	remove(row);

	// insert row
	row->_next = before; // update row
	row->_prev = before->_prev;
	row->_next->_prev = row; // update row->next
	if (row->_prev) { // update row->prev
		row->_prev->_next = row;
	} else {
		_begin = row;
	}

	// update pos
	for (auto n = row; n != updateTill; n = n->_next) {
		n->_next->_pos++;
		row->_pos--;
	}
	return true;
}

bool List::insertAfter(Row *row, Row *after) {
	if (row == after) {
		return false;
	}

	if (_current == row) {
		_current = row->_next;
	}

	const auto updateFrom = row->_next;
	remove(row);

	// insert row
	row->_prev = after; // update row
	row->_next = after->_next;
	row->_prev->_next = row; // update row->prev
	row->_next->_prev = row; // update row->next

	// update pos
	for (auto n = updateFrom; n != row; n = n->_next) {
		n->_pos--;
		row->_pos++;
	}
	return true;
}

Row *List::adjustByName(Key key) {
	if (_sortMode != SortMode::Name) return nullptr;

	const auto i = _rowByKey.find(key);
	if (i == _rowByKey.cend()) return nullptr;

	const auto row = i->second;
	const auto name = key.entry()->chatsListName();
	auto change = row;
	while (change->_prev
		&& change->_prev->entry()->chatsListName().compare(name, Qt::CaseInsensitive) < 0) {
		change = change->_prev;
	}
	if (!insertBefore(row, change)) {
		while (change->_next != _end
			&& change->_next->entry()->chatsListName().compare(name, Qt::CaseInsensitive) < 0) {
			change = change->_next;
		}
		insertAfter(row, change);
	}
	return row;
}

Row *List::addByName(Key key) {
	if (_sortMode != SortMode::Name) {
		return nullptr;
	}

	const auto row = addToEnd(key);
	auto change = row;
	const auto name = key.entry()->chatsListName();
	while (change->_prev
		&& change->_prev->entry()->chatsListName().compare(name, Qt::CaseInsensitive) > 0) {
		change = change->_prev;
	}
	if (!insertBefore(row, change)) {
		while (change->_next != _end
			&& change->_next->entry()->chatsListName().compare(name, Qt::CaseInsensitive) < 0) {
			change = change->_next;
		}
		insertAfter(row, change);
	}
	return row;
}

void List::adjustByPos(Row *row) {
	if (_sortMode != SortMode::Date || !_begin) return;

	Row *change = row;
	if (change != _begin && _begin->sortKey() < row->sortKey()) {
		change = _begin;
	} else {
		while (change->_prev && change->_prev->sortKey() < row->sortKey()) {
			change = change->_prev;
		}
	}
	if (!insertBefore(row, change)) {
		if (change->_next != _end && _end->_prev->sortKey() > row->sortKey()) {
			change = _end->_prev;
		} else {
			while (change->_next != _end && change->_next->sortKey() > row->sortKey()) {
				change = change->_next;
			}
		}
		insertAfter(row, change);
	}
}

bool List::moveToTop(Key key) {
	auto i = _rowByKey.find(key);
	if (i == _rowByKey.cend()) {
		return false;
	}

	insertBefore(i->second, _begin);
	return true;
}

bool List::del(Key key, Row *replacedBy) {
	auto i = _rowByKey.find(key);
	if (i == _rowByKey.cend()) {
		return false;
	}

	const auto row = i->second;
	if (App::main()) {
		emit App::main()->dialogRowReplaced(row, replacedBy);
	}

	if (row == _current) {
		_current = row->_next;
	}
	for (auto change = row->_next; change != _end; change = change->_next) {
		--change->_pos;
	}
	--_end->_pos;
	remove(row);
	delete row;
	--_count;
	_rowByKey.erase(i);

	return true;
}

void List::remove(Row *row) {
	row->_next->_prev = row->_prev; // update row->next
	if (row->_prev) { // update row->prev
		row->_prev->_next = row->_next;
	} else {
		_begin = row->_next;
	}
}

void List::clear() {
	while (_begin != _end) {
		_current = _begin;
		_begin = _begin->_next;
		delete _current;
	}
	_current = _begin;
	_rowByKey.clear();
	_count = 0;
}

List::~List() {
	clear();
}

} // namespace Dialogs
