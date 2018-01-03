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

class List {
public:
	List(SortMode sortMode);
	List(const List &other) = delete;
	List &operator=(const List &other) = delete;

	int size() const {
		return _count;
	}
	bool isEmpty() const {
		return size() == 0;
	}
	bool contains(PeerId peerId) const {
		return _rowByPeer.contains(peerId);
	}
	Row *getRow(PeerId peerId) const {
		return _rowByPeer.value(peerId);
	}
	Row *rowAtY(int32 y, int32 h) const {
		auto i = cfind(y, h);
		if (i == cend() || (*i)->pos() != ((y > 0) ? (y / h) : 0)) {
			return nullptr;
		}
		return *i;
	}

	Row *addToEnd(History *history);
	Row *adjustByName(const PeerData *peer);
	Row *addByName(History *history);
	bool moveToTop(PeerId peerId);
	void adjustByPos(Row *row);
	bool del(PeerId peerId, Row *replacedBy = nullptr);
	void remove(Row *row);
	void clear();

	class const_iterator {
	public:
		using value_type = Row*;
		using pointer = Row**;
		using reference = Row*&;

		explicit const_iterator(Row *p) : _p(p) {
		}
		inline Row* operator*() const { return _p; }
		inline Row* const* operator->() const { return &_p; }
		inline bool operator==(const const_iterator &other) const { return _p == other._p; }
		inline bool operator!=(const const_iterator &other) const { return !(*this == other); }
		inline const_iterator &operator++() { _p = next(_p); return *this; }
		inline const_iterator operator++(int) { const_iterator result(*this); ++(*this); return result; }
		inline const_iterator &operator--() { _p = prev(_p); return *this; }
		inline const_iterator operator--(int) { const_iterator result(*this); --(*this); return result; }
		inline const_iterator operator+(int j) const { const_iterator result = *this; return result += j; }
		inline const_iterator operator-(int j) const { const_iterator result = *this; return result -= j; }
		inline const_iterator &operator+=(int j) { if (j < 0) return (*this -= (-j)); while (j--) ++*this; return *this; }
		inline const_iterator &operator-=(int j) { if (j < 0) return (*this += (-j)); while (j--) --*this; return *this; }

	private:
		Row *_p;
		friend class List;

	};
	friend class const_iterator;
	using iterator = const_iterator;

	const_iterator cbegin() const { return const_iterator(_begin); }
	const_iterator cend() const { return const_iterator(_end); }
	const_iterator begin() const { return cbegin(); }
	const_iterator end() const { return cend(); }
	iterator begin() { return iterator(_begin); }
	iterator end() { return iterator(_end); }
	const_iterator cfind(Row *value) const { return value ? const_iterator(value) : cend(); }
	const_iterator find(Row *value) const { return cfind(value); }
	iterator find(Row *value) { return value ? iterator(value) : end(); }
	const_iterator cfind(int y, int h) const {
		adjustCurrent(y, h);
		return iterator(_current);
	}
	const_iterator find(int y, int h) const { return cfind(y, h); }
	iterator find(int y, int h) {
		adjustCurrent(y, h);
		return iterator(_current);
	}

	~List();

private:
	void adjustCurrent(int y, int h) const;
	bool insertBefore(Row *row, Row *before);
	bool insertAfter(Row *row, Row *after);
	static Row *next(Row *row) {
		return row->_next;
	}
	static Row *prev(Row *row) {
		return row->_prev;
	}

	std::unique_ptr<Row> _last;
	Row *_begin;
	Row *_end;
	SortMode _sortMode;
	int _count = 0;

	typedef QHash<PeerId, Row*> RowByPeer;
	RowByPeer _rowByPeer;

	mutable Row *_current; // cache
};

} // namespace Dialogs
