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

	[[nodiscard]] int size() const {
		return _rows.size();
	}
	[[nodiscard]] bool empty() const {
		return _rows.empty();
	}
	[[nodiscard]] int height() const {
		return _rows.empty()
			? 0
			: (_rows.back()->top() + _rows.back()->height());
	}
	[[nodiscard]] bool contains(Key key) const {
		return _rowByKey.find(key) != _rowByKey.end();
	}
	[[nodiscard]] Row *getRow(Key key) const {
		const auto i = _rowByKey.find(key);
		return (i != _rowByKey.end()) ? i->second.get() : nullptr;
	}
	[[nodiscard]] Row *rowAtY(int y) const;

	not_null<Row*> addToEnd(Key key);
	Row *adjustByName(Key key);
	not_null<Row*> addByName(Key key);
	bool moveToTop(Key key);
	void adjustByDate(not_null<Row*> row);
	bool remove(Key key, Row *replacedBy = nullptr);

	using const_iterator = std::vector<not_null<Row*>>::const_iterator;
	using iterator = const_iterator;

	[[nodiscard]] const_iterator cbegin() const { return _rows.cbegin(); }
	[[nodiscard]] const_iterator cend() const { return _rows.cend(); }
	[[nodiscard]] const_iterator begin() const { return cbegin(); }
	[[nodiscard]] const_iterator end() const { return cend(); }
	[[nodiscard]] iterator begin() { return cbegin(); }
	[[nodiscard]] iterator end() { return cend(); }
	[[nodiscard]] const_iterator cfind(Row *value) const;
	[[nodiscard]] const_iterator find(Row *value) const {
		return cfind(value);
	}
	[[nodiscard]] iterator find(Row *value) { return cfind(value); }
	[[nodiscard]] iterator findByY(int y) const;

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
