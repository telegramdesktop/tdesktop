/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_list.h"

#include "dialogs/dialogs_entry.h"
#include "dialogs/ui/dialogs_layout.h"
#include "data/data_session.h"
#include "mainwidget.h"

namespace Dialogs {

List::List(SortMode sortMode, FilterId filterId)
: _sortMode(sortMode)
, _filterId(filterId) {
}

List::const_iterator List::cfind(Row *value) const {
	return value
		? (cbegin() + value->index())
		: cend();
}

not_null<Row*> List::addToEnd(Key key) {
	if (const auto result = getRow(key)) {
		return result;
	}
	const auto result = _rowByKey.emplace(
		key,
		std::make_unique<Row>(key, _rows.size(), height())
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
	Expects(row->index() >= 0 && row->index() < _rows.size());

	const auto &key = row->entry()->chatListNameSortKey();
	const auto index = row->index();
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
	const auto index = row->index();
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
	const auto index = i->second->index();
	const auto begin = _rows.begin();
	rotate(begin, begin + index, begin + index + 1);
	return true;
}

void List::rotate(
		std::vector<not_null<Row*>>::iterator first,
		std::vector<not_null<Row*>>::iterator middle,
		std::vector<not_null<Row*>>::iterator last) {
	auto top = (*first)->top();
	std::rotate(first, middle, last);

	auto count = (last - first);
	auto index = (first - _rows.begin());
	while (count--) {
		const auto row = *first++;
		row->_index = index++;
		row->_top = top;
		top += row->height();
	}
}

bool List::remove(Key key, Row *replacedBy) {
	auto i = _rowByKey.find(key);
	if (i == _rowByKey.cend()) {
		return false;
	}

	const auto row = i->second.get();
	row->entry()->owner().dialogsRowReplaced({ row, replacedBy });

	auto top = row->top();
	const auto index = row->index();
	_rows.erase(_rows.begin() + index);
	for (auto i = index, count = int(_rows.size()); i != count; ++i) {
		const auto row = _rows[i];
		row->_index = i;
		row->_top = top;
		top += row->height();
	}
	_rowByKey.erase(i);
	return true;
}

Row *List::rowAtY(int y) const {
	const auto i = findByY(y);
	if (i == cend()) {
		return nullptr;
	}
	const auto row = *i;
	const auto top = row->top();
	const auto bottom = top + row->height();
	return (top <= y && bottom > y) ? row.get() : nullptr;
}

List::iterator List::findByY(int y) const {
	return ranges::lower_bound(_rows, y, ranges::less(), [](const Row *row) {
		return row->top() + row->height();
	});
}

} // namespace Dialogs
