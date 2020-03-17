/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "ui/effects/animations.h"
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

enum class SortMode;

class BasicRow {
public:
	BasicRow();
	~BasicRow();

	void setOnline(bool online, Fn<void()> updateCallback = nullptr) const;
	void paintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		bool allowOnline,
		bool active,
		int fullWidth) const;

	void addRipple(QPoint origin, QSize size, Fn<void()> updateCallback);
	void stopLastRipple();

	void paintRipple(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		const QColor *colorOverride = nullptr) const;

private:
	struct OnlineUserpic {
		InMemoryKey key;
		float64 online = 0.;
		bool active = false;
		QImage frame;
		Ui::Animations::Simple animation;
	};

	void ensureOnlineUserpic() const;
	static void PaintOnlineFrame(
		not_null<OnlineUserpic*> data,
		not_null<PeerData*> peer);

	mutable std::unique_ptr<Ui::RippleAnimation> _ripple;
	mutable std::unique_ptr<OnlineUserpic> _onlineUserpic;
	mutable bool _online = false;

};

class List;
class Row : public BasicRow {
public:
	explicit Row(std::nullptr_t) {
	}
	Row(Key key, int pos);

	Key key() const {
		return _id;
	}
	History *history() const {
		return _id.history();
	}
	Data::Folder *folder() const {
		return _id.folder();
	}
	not_null<Entry*> entry() const {
		return _id.entry();
	}
	int pos() const {
		return _pos;
	}
	uint64 sortKey(FilterId filterId) const;

	void validateListEntryCache() const;
	const Ui::Text::String &listEntryCache() const {
		return _listEntryCache;
	}

	// for any attached data, for example View in contacts list
	void *attached = nullptr;

private:
	friend class List;

	Key _id;
	int _pos = 0;
	mutable uint32 _listEntryCacheVersion = 0;
	mutable Ui::Text::String _listEntryCache;

};

class FakeRow : public BasicRow {
public:
	FakeRow(Key searchInChat, not_null<HistoryItem*> item);

	Key searchInChat() const {
		return _searchInChat;
	}
	not_null<HistoryItem*> item() const {
		return _item;
	}

private:
	friend class Layout::RowPainter;

	Key _searchInChat;
	not_null<HistoryItem*> _item;
	mutable const HistoryItem *_cacheFor = nullptr;
	mutable Ui::Text::String _cache;

};

} // namespace Dialogs
