/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_list.h"

#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_layout.h"
#include "data/data_session.h"
#include "mainwidget.h"

namespace Dialogs {

List::List(SortMode sortMode, FilterId filterId)
: _sortMode(sortMode)
, _filterId(filterId) {
}

List::const_iterator List::cfind(Row *value) const {
	return value
		? (cbegin() + value->pos())
		: cend();
}

not_null<Row*> List::addToEnd(Key key) {
	if (const auto result = getRow(key)) {
		return result;
	}
	const auto result = _rowByKey.emplace(
		key,
		std::make_unique<Row>(key, _rows.size())
	).first->second.get();
	_rows.emplace_back(result);
	if (_sortMode == SortMode::Date) {
		adjustByDate(result);
	}
	return result;
}

Row *List::adjustByName(Key key) {
	Expects(_sortMode == SortMode::Name);

	const auto row = getRow(key);
	if (!row) {
		return nullptr;
	}
	adjustByName(row);
	return row;
}

not_null<Row*> List::addByName(Key key) {
	Expects(_sortMode == SortMode::Name);

	const auto row = addToEnd(key);
	adjustByName(key);
	return row;
}

void List::adjustByName(not_null<Row*> row) {
	Expects(row->pos() >= 0 && row->pos() < _rows.size());

	const auto &key = row->entry()->chatListNameSortKey();
	const auto index = row->pos();
	const auto i = _rows.begin() + index;
	const auto before = std::find_if(i + 1, _rows.end(), [&](Row *row) {
		return row->entry()->chatListNameSortKey().compare(key) >= 0;
	});
	if (before != i + 1) {
		rotate(i, i + 1, before);
	} else if (i != _rows.begin()) {
		const auto from = std::make_reverse_iterator(i);
		const auto after = std::find_if(from, _rows.rend(), [&](Row *row) {
			return row->entry()->chatListNameSortKey().compare(key) <= 0;
		}).base();
		if (after != i) {
			rotate(after, i, i + 1);
		}
	}
}

void List::adjustByDate(not_null<Row*> row) {
	Expects(_sortMode == SortMode::Date);

	const auto key = row->sortKey(_filterId);
	const auto index = row->pos();
	const auto i = _rows.begin() + index;
	const auto before = std::find_if(i + 1, _rows.end(), [&](Row *row) {
		return (row->sortKey(_filterId) <= key);
	});
	if (before != i + 1) {
		rotate(i, i + 1, before);
	} else {
		const auto from = std::make_reverse_iterator(i);
		const auto after = std::find_if(from, _rows.rend(), [&](Row *row) {
			return (row->sortKey(_filterId) >= key);
		}).base();
		if (after != i) {
			rotate(after, i, i + 1);
		}
	}
}

bool List::moveToTop(Key key) {
	const auto i = _rowByKey.find(key);
	if (i == _rowByKey.cend()) {
		return false;
	}
	const auto index = i->second->pos();
	const auto begin = _rows.begin();
	rotate(begin, begin + index, begin + index + 1);
	return true;
}

void List::rotate(
		std::vector<not_null<Row*>>::iterator first,
		std::vector<not_null<Row*>>::iterator middle,
		std::vector<not_null<Row*>>::iterator last) {
	std::rotate(first, middle, last);

	auto count = (last - first);
	auto index = (first - _rows.begin());
	while (count--) {
		(*first++)->_pos = index++;
	}
}

bool List::del(Key key, Row *replacedBy) {
	auto i = _rowByKey.find(key);
	if (i == _rowByKey.cend()) {
		return false;
	}

	const auto row = i->second.get();
	row->entry()->owner().dialogsRowReplaced({ row, replacedBy });

	const auto index = row->pos();
	_rows.erase(_rows.begin() + index);
	for (auto i = index, count = int(_rows.size()); i != count; ++i) {
		_rows[i]->_pos = i;
	}
	_rowByKey.erase(i);
	return true;
}

} // namespace Dialogs
