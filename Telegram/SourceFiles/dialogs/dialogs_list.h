/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_row.h"

class PeerData;
namespace Dialogs {

enum class SortMode;

class List final {
public:
	List(SortMode sortMode, FilterId filterId = 0);
	List(const List &other) = delete;
	List &operator=(const List &other) = delete;
	List(List &&other) = default;
	List &operator=(List &&other) = default;
	~List() = default;

	int size() const {
		return _rows.size();
	}
	bool empty() const {
		return _rows.empty();
	}
	bool contains(Key key) const {
		return _rowByKey.find(key) != _rowByKey.end();
	}
	Row *getRow(Key key) const {
		const auto i = _rowByKey.find(key);
		return (i != _rowByKey.end()) ? i->second.get() : nullptr;
	}
	Row *rowAtY(int y, int h) const {
		const auto i = cfind(y, h);
		if (i == cend() || (*i)->pos() != ((y > 0) ? (y / h) : 0)) {
			return nullptr;
		}
		return *i;
	}

	not_null<Row*> addToEnd(Key key);
	Row *adjustByName(Key key);
	not_null<Row*> addByName(Key key);
	bool moveToTop(Key key);
	void adjustByDate(not_null<Row*> row);
	bool del(Key key, Row *replacedBy = nullptr);

	using const_iterator = std::vector<not_null<Row*>>::const_iterator;
	using iterator = const_iterator;

	const_iterator cbegin() const { return _rows.cbegin(); }
	const_iterator cend() const { return _rows.cend(); }
	const_iterator begin() const { return cbegin(); }
	const_iterator end() const { return cend(); }
	iterator begin() { return cbegin(); }
	iterator end() { return cend(); }
	const_iterator cfind(Row *value) const;
	const_iterator find(Row *value) const { return cfind(value); }
	iterator find(Row *value) { return cfind(value); }
	const_iterator cfind(int y, int h) const {
		const auto index = std::max(y, 0) / h;
		return _rows.begin() + std::min(index, size());
	}
	const_iterator find(int y, int h) const { return cfind(y, h); }
	iterator find(int y, int h) { return cfind(y, h); }

private:
	void adjustByName(not_null<Row*> row);
	void rotate(
		std::vector<not_null<Row*>>::iterator first,
		std::vector<not_null<Row*>>::iterator middle,
		std::vector<not_null<Row*>>::iterator last);

	SortMode _sortMode = SortMode();
	FilterId _filterId = 0;
	std::vector<not_null<Row*>> _rows;
	std::map<Key, std::unique_ptr<Row>> _rowByKey;

};

} // namespace Dialogs
