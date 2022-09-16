/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "ui/effects/animations.h"
#include "ui/unread_badge.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/ui/dialogs_message_view.h"

class History;
class HistoryItem;

namespace Data {
class CloudImageView;
} // namespace Data

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Dialogs::Ui {
using namespace ::Ui;
class RowPainter;
class VideoUserpic;
} // namespace Dialogs::Ui

namespace Dialogs {

enum class SortMode;

class BasicRow {
public:
	BasicRow();
	virtual ~BasicRow();

	virtual void paintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		History *historyForCornerBadge,
		crl::time now,
		bool active,
		int fullWidth,
		bool paused) const;

	void addRipple(QPoint origin, QSize size, Fn<void()> updateCallback);
	void stopLastRipple();

	void paintRipple(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		const QColor *colorOverride = nullptr) const;

	std::shared_ptr<Data::CloudImageView> &userpicView() const {
		return _userpic;
	}

private:
	mutable std::shared_ptr<Data::CloudImageView> _userpic;
	mutable std::unique_ptr<Ui::RippleAnimation> _ripple;

};

class List;
class Row : public BasicRow {
public:
	explicit Row(std::nullptr_t) {
	}
	Row(Key key, int pos);

	void updateCornerBadgeShown(
		not_null<PeerData*> peer,
		Fn<void()> updateCallback = nullptr) const;
	void paintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		History *historyForCornerBadge,
		crl::time now,
		bool active,
		int fullWidth,
		bool paused) const final override;

	[[nodiscard]] Key key() const {
		return _id;
	}
	[[nodiscard]] History *history() const {
		return _id.history();
	}
	[[nodiscard]] Data::Folder *folder() const {
		return _id.folder();
	}
	[[nodiscard]] not_null<Entry*> entry() const {
		return _id.entry();
	}
	[[nodiscard]] int pos() const {
		return _pos;
	}
	[[nodiscard]] uint64 sortKey(FilterId filterId) const;

	void validateListEntryCache() const;
	[[nodiscard]] const Ui::Text::String &listEntryCache() const {
		return _listEntryCache;
	}

	// for any attached data, for example View in contacts list
	void *attached = nullptr;

private:
	friend class List;

	struct CornerBadgeUserpic {
		InMemoryKey key;
		float64 shown = 0.;
		int frameIndex = -1;
		bool active = false;
		QImage frame;
		Ui::Animations::Simple animation;
	};

	void setCornerBadgeShown(
		bool shown,
		Fn<void()> updateCallback) const;
	void ensureCornerBadgeUserpic() const;
	static void PaintCornerBadgeFrame(
		not_null<CornerBadgeUserpic*> data,
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		std::shared_ptr<Data::CloudImageView> &view,
		bool paused);

	Key _id;
	int _pos = 0;
	mutable uint32 _listEntryCacheVersion = 0;
	mutable Ui::Text::String _listEntryCache;
	mutable std::unique_ptr<CornerBadgeUserpic> _cornerBadgeUserpic;
	mutable bool _cornerBadgeShown = false;

};

class FakeRow : public BasicRow {
public:
	FakeRow(
		Key searchInChat,
		not_null<HistoryItem*> item,
		Fn<void()> repaint);

	[[nodiscard]] Key searchInChat() const {
		return _searchInChat;
	}
	[[nodiscard]] not_null<HistoryItem*> item() const {
		return _item;
	}
	[[nodiscard]] Ui::MessageView &itemView() const {
		return _itemView;
	}
	[[nodiscard]] Fn<void()> repaint() const {
		return _repaint;
	}
	[[nodiscard]] Ui::PeerBadge &badge() const {
		return _badge;
	}
	[[nodiscard]] const Ui::Text::String &name() const;

private:
	friend class Ui::RowPainter;

	const Key _searchInChat;
	const not_null<HistoryItem*> _item;
	const Fn<void()> _repaint;
	mutable Ui::MessageView _itemView;
	mutable Ui::PeerBadge _badge;
	mutable Ui::Text::String _name;

};

} // namespace Dialogs
