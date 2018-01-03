/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"

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
	Row(History *history, Row *prev, Row *next, int pos)
		: _history(history)
		, _prev(prev)
		, _next(next)
		, _pos(pos) {
	}
	void *attached = nullptr; // for any attached data, for example View in contacts list

	History *history() const {
		return _history;
	}
	int pos() const {
		return _pos;
	}

private:
	friend class List;

	History *_history;
	Row *_prev, *_next;
	int _pos;

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
