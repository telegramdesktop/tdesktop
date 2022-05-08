/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/media/info_media_common.h"
#include "layout/layout_mosaic.h"
#include "ui/text/text.h"

namespace Info::Media {

class ListSection {
public:
	ListSection(Type type, not_null<ListSectionDelegate*> delegate);

	bool addItem(not_null<BaseLayout*> item);
	void finishSection();

	[[nodiscard]] bool empty() const;

	[[nodiscard]] UniversalMsgId minId() const;

	void setTop(int top);
	[[nodiscard]] int top() const;
	void resizeToWidth(int newWidth);
	[[nodiscard]] int height() const;

	[[nodiscard]] int bottom() const;

	bool removeItem(not_null<const HistoryItem*> item);
	[[nodiscard]] std::optional<ListFoundItem> findItemByItem(
		not_null<const HistoryItem*> item) const;
	[[nodiscard]] ListFoundItem findItemDetails(
		not_null<BaseLayout*> item) const;
	[[nodiscard]] ListFoundItem findItemByPoint(QPoint point) const;

	using Items = std::vector<not_null<BaseLayout*>>;
	const Items &items() const;

	void paint(
		Painter &p,
		const ListContext &context,
		QRect clip,
		int outerWidth) const;

	void paintFloatingHeader(Painter &p, int visibleTop, int outerWidth);

private:
	[[nodiscard]] int headerHeight() const;
	void appendItem(not_null<BaseLayout*> item);
	void setHeader(not_null<BaseLayout*> item);
	[[nodiscard]] bool belongsHere(not_null<BaseLayout*> item) const;
	[[nodiscard]] Items::iterator findItemAfterTop(int top);
	[[nodiscard]] Items::const_iterator findItemAfterTop(int top) const;
	[[nodiscard]] Items::const_iterator findItemAfterBottom(
		Items::const_iterator from,
		int bottom) const;
	[[nodiscard]] QRect findItemRect(not_null<const BaseLayout*> item) const;
	[[nodiscard]] ListFoundItem completeResult(
		not_null<BaseLayout*> item,
		bool exact) const;
	[[nodiscard]] TextSelection itemSelection(
		not_null<const BaseLayout*> item,
		const ListContext &context) const;

	int recountHeight();
	void refreshHeight();

	Type _type = Type{};
	not_null<ListSectionDelegate*> _delegate;

	bool _hasFloatingHeader = false;
	Ui::Text::String _header;
	Items _items;
	base::flat_map<
		not_null<const HistoryItem*>,
		not_null<BaseLayout*>> _byItem;
	int _itemsLeft = 0;
	int _itemsTop = 0;
	int _itemWidth = 0;
	int _itemsInRow = 1;
	mutable int _rowsCount = 0;
	int _top = 0;
	int _height = 0;

	Mosaic::Layout::MosaicLayout<BaseLayout> _mosaic;

};

} // namespace Info::Media
