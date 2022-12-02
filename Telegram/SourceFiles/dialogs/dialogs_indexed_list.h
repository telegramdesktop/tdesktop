/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_list.h"

class History;

namespace Dialogs {

class IndexedList {
public:
	IndexedList(SortMode sortMode, FilterId filterId = 0);

	RowsByLetter addToEnd(Key key);
	Row *addByName(Key key);
	void adjustByDate(const RowsByLetter &links);
	void moveToTop(Key key);

	// row must belong to this indexed list all().
	void movePinned(Row *row, int deltaSign);

	// For sortMode != SortMode::Date && != Complex
	void peerNameChanged(
		not_null<PeerData*> peer,
		const base::flat_set<QChar> &oldChars);

	//For sortMode == SortMode::Date || == Complex
	void peerNameChanged(
		FilterId filterId,
		not_null<PeerData*> peer,
		const base::flat_set<QChar> &oldChars);

	void remove(Key key, Row *replacedBy = nullptr);
	void clear();

	[[nodiscard]] const List &all() const {
		return _list;
	}
	[[nodiscard]] const List *filtered(QChar ch) const {
		const auto i = _index.find(ch);
		return (i != _index.end()) ? &i->second : nullptr;
	}
	[[nodiscard]] std::vector<not_null<Row*>> filtered(
		const QStringList &words) const;

	// Part of List interface is duplicated here for all() list.
	[[nodiscard]] int size() const { return all().size(); }
	[[nodiscard]] bool empty() const { return all().empty(); }
	[[nodiscard]] int height() const { return all().height(); }
	[[nodiscard]] bool contains(Key key) const {
		return all().contains(key);
	}
	[[nodiscard]] Row *getRow(Key key) const { return all().getRow(key); }
	[[nodiscard]] Row *rowAtY(int y) const { return all().rowAtY(y); }

	using iterator = List::iterator;
	using const_iterator = List::const_iterator;
	[[nodiscard]] const_iterator cbegin() const { return all().cbegin(); }
	[[nodiscard]] const_iterator cend() const { return all().cend(); }
	[[nodiscard]] const_iterator begin() const { return all().cbegin(); }
	[[nodiscard]] const_iterator end() const { return all().cend(); }
	[[nodiscard]] iterator begin() { return all().begin(); }
	[[nodiscard]] iterator end() { return all().end(); }
	[[nodiscard]] const_iterator cfind(Row *value) const {
		return all().cfind(value);
	}
	[[nodiscard]] const_iterator find(Row *value) const {
		return all().cfind(value);
	}
	[[nodiscard]] iterator find(Row *value) { return all().find(value); }
	[[nodiscard]] const_iterator findByY(int y) const {
		return all().findByY(y);
	}
	[[nodiscard]] iterator findByY(int y) { return all().findByY(y); }

private:
	void adjustByName(
		Key key,
		const base::flat_set<QChar> &oldChars);
	void adjustNames(
		FilterId filterId,
		not_null<History*> history,
		const base::flat_set<QChar> &oldChars);

	SortMode _sortMode = SortMode();
	FilterId _filterId = 0;
	List _list, _empty;
	base::flat_map<QChar, List> _index;

};

} // namespace Dialogs
