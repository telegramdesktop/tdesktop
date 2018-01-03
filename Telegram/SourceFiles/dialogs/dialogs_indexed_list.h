/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_common.h"
#include "dialogs/dialogs_list.h"

class History;

namespace Dialogs {

class IndexedList {
public:
	IndexedList(SortMode sortMode);

	RowsByLetter addToEnd(History *history);
	Row *addByName(History *history);
	void adjustByPos(const RowsByLetter &links);
	void moveToTop(not_null<PeerData*> peer);

	// row must belong to this indexed list all().
	void movePinned(Row *row, int deltaSign);

	// For sortMode != SortMode::Date
	void peerNameChanged(not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars);

	//For sortMode == SortMode::Date
	void peerNameChanged(Mode list, not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars);

	void del(not_null<const PeerData*> peer, Row *replacedBy = nullptr);
	void clear();

	const List &all() const {
		return _list;
	}
	const List *filtered(QChar ch) const {
		if (auto it = _index.find(ch); it != _index.cend()) {
			return it->second.get();
		}
		return &_empty;
	}

	~IndexedList();

	// Part of List interface is duplicated here for all() list.
	int size() const { return all().size(); }
	bool isEmpty() const { return all().isEmpty(); }
	bool contains(PeerId peerId) const { return all().contains(peerId); }
	Row *getRow(PeerId peerId) const { return all().getRow(peerId); }
	Row *rowAtY(int32 y, int32 h) const { return all().rowAtY(y, h); }

	using iterator = List::iterator;
	using const_iterator = List::const_iterator;
	const_iterator cbegin() const { return all().cbegin(); }
	const_iterator cend() const { return all().cend(); }
	const_iterator begin() const { return all().cbegin(); }
	const_iterator end() const { return all().cend(); }
	iterator begin() { return all().begin(); }
	iterator end() { return all().end(); }
	const_iterator cfind(Row *value) const { return all().cfind(value); }
	const_iterator find(Row *value) const { return all().cfind(value); }
	iterator find(Row *value) { return all().find(value); }
	const_iterator cfind(int y, int h) const { return all().cfind(y, h); }
	const_iterator find(int y, int h) const { return all().cfind(y, h); }
	iterator find(int y, int h) { return all().find(y, h); }

private:
	void adjustByName(not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars);
	void adjustNames(Mode list, not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars);

	SortMode _sortMode;
	List _list, _empty;
	base::flat_map<QChar, std::unique_ptr<List>> _index;

};

} // namespace Dialogs
