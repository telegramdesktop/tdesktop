/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "base/value_ordering.h"

class History;
class HistoryItem;

namespace Data {
class Feed;
} // namespace Data

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Dialogs {
namespace Layout {
class RowPainter;
} // namespace Layout

struct Key {
	Key() = default;
	Key(History *history) : value(history) {
	}
	Key(not_null<History*> history) : value(history) {
	}
	Key(Data::Feed *feed) : value(feed) {
	}
	Key(not_null<Data::Feed*> feed) : value(feed) {
	}
	const QString &name() const {
		if (const auto p = base::get_if<not_null<History*>>(&value)) {
			return (*p)->peer->name;
		}
		// #TODO feeds name
		static const auto empty = QString();
		return empty;
	}
	const PeerData::NameFirstChars &nameFirstChars() const {
		if (const auto p = base::get_if<not_null<History*>>(&value)) {
			return (*p)->peer->nameFirstChars();
		}
		// #TODO feeds name
		static const auto empty = PeerData::NameFirstChars();
		return empty;
	}
	uint64 sortKey() const {
		if (const auto p = base::get_if<not_null<History*>>(&value)) {
			return (*p)->sortKeyInChatList();
		}
		// #TODO feeds sort in chats list
		return 0ULL;
	}
	History *history() const {
		if (const auto p = base::get_if<not_null<History*>>(&value)) {
			return *p;
		}
		return nullptr;
	}
	Data::Feed *feed() const {
		if (const auto p = base::get_if<not_null<Data::Feed*>>(&value)) {
			return *p;
		}
		return nullptr;
	}

	inline bool operator<(const Key &other) const {
		return value < other.value;
	}
	inline bool operator==(const Key &other) const {
		return value == other.value;
	}

	// Not working :(
	//friend inline auto value_ordering_helper(const Key &key) {
	//	return key.value;
	//}

	base::optional_variant<
		not_null<History*>,
		not_null<Data::Feed*>> value;

};

struct RowDescriptor {
	RowDescriptor() = default;
	RowDescriptor(Key key, MsgId msgId) : key(key), msgId(msgId) {
	}

	Key key;
	MsgId msgId = 0;
};

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
	QString name() const {
		return _id.name();
	}
	int pos() const {
		return _pos;
	}
	uint64 sortKey() const {
		return _id.sortKey();
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
