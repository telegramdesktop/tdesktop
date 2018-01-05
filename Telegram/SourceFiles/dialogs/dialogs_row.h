/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "dialogs/dialogs_key.h"

class History;
class HistoryItem;

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Dialogs {
namespace Layout {
class RowPainter;
} // namespace Layout

class RippleRow {
public:
	RippleRow();
	~RippleRow();

	void addRipple(QPoint origin, QSize size, base::lambda<void()> updateCallback);
	void stopLastRipple();

	void paintRipple(Painter &p, int x, int y, int outerWidth, TimeMs ms, const QColor *colorOverride = nullptr) const;

private:
	mutable std::unique_ptr<Ui::RippleAnimation> _ripple;

};

class List;
class Row : public RippleRow {
public:
	explicit Row(std::nullptr_t) {
	}
	Row(Key key, Row *prev, Row *next, int pos)
	: _id(key)
	, _prev(prev)
	, _next(next)
	, _pos(pos) {
	}

	Key key() const {
		return _id;
	}
	History *history() const {
		return _id.history();
	}
	Data::Feed *feed() const {
		return _id.feed();
	}
	not_null<Entry*> entry() const {
		return _id.entry();
	}
	QString name() const {
		return _id.name();
	}
	int pos() const {
		return _pos;
	}
	uint64 sortKey() const {
		return _id.entry()->sortKeyInChatList();
	}

	// for any attached data, for example View in contacts list
	void *attached = nullptr;

private:
	friend class List;

	Key _id;
	Row *_prev = nullptr;
	Row *_next = nullptr;
	int _pos = 0;

};

class FakeRow : public RippleRow {
public:
	FakeRow(PeerData *searchInPeer, not_null<HistoryItem*> item);

	PeerData *searchInPeer() const {
		return _searchInPeer;
	}
	not_null<HistoryItem*> item() const {
		return _item;
	}

private:
	friend class Layout::RowPainter;

	PeerData *_searchInPeer = nullptr;
	not_null<HistoryItem*> _item;
	mutable const HistoryItem *_cacheFor = nullptr;
	mutable Text _cache;

};

} // namespace Dialogs
