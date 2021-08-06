/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "layout/abstract_layout_item.h"
#include "layout/layout_position.h"

#include "styles/style_chat_helpers.h"

namespace Mosaic::Layout {

struct FoundItem {
	int index = -1;
	bool exact = false;
	QPoint relative;
};

class AbstractMosaicLayout {
public:
	AbstractMosaicLayout(int bigWidth);

	[[nodiscard]] int rowHeightAt(int row) const;
	[[nodiscard]] int countDesiredHeight(int newWidth);

	[[nodiscard]] FoundItem findByPoint(const QPoint &globalPoint) const;

	[[nodiscard]] QRect findRect(int index) const;

	void setRightSkip(int rightSkip);
	void setOffset(int left, int top);
	void setFullWidth(int w);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] int rowsCount() const;

	void clearRows(bool resultsDeleted);

protected:
	void addItems(gsl::span<const not_null<AbstractLayoutItem*>> items);

	[[nodiscard]] not_null<AbstractLayoutItem*> itemAt(int row, int column) const;
	[[nodiscard]] not_null<AbstractLayoutItem*> itemAt(int index) const;

	[[nodiscard]] AbstractLayoutItem *maybeItemAt(int row, int column) const;
	[[nodiscard]] AbstractLayoutItem *maybeItemAt(int index) const;

	void forEach(Fn<void(not_null<const AbstractLayoutItem*>)> callback);

	void paint(
		Fn<void(not_null<AbstractLayoutItem*>, QPoint)> paintItem,
		const QRect &clip) const;
	int validateExistingRows(
		Fn<bool(not_null<const AbstractLayoutItem*>, int)> checkItem,
		int count);

private:
	static constexpr auto kInlineItemsMaxPerRow = 5;
	struct Row {
		int maxWidth = 0;
		int height = 0;
		std::vector<AbstractLayoutItem*> items;
	};

	void addItem(not_null<AbstractLayoutItem*> item, Row &row, int &sumWidth);
	bool rowFinalize(Row &row, int &sumWidth, bool force);
	void layoutRow(Row &row, int fullWidth);

	int _bigWidth;
	int _width = 0;
	int _rightSkip = 0;
	QPoint _offset;
	std::vector<Row> _rows;

};

template <
	typename ItemBase,
	typename = std::enable_if_t<
		std::is_base_of_v<AbstractLayoutItem, ItemBase>>>
class MosaicLayout final : public AbstractMosaicLayout {
	using Parent = AbstractMosaicLayout;

public:
	using Parent::Parent;

	void addItems(const std::vector<not_null<ItemBase*>> &items) {
		Parent::addItems({
			reinterpret_cast<const not_null<AbstractLayoutItem*>*>(
				items.data()),
			items.size() });
	}

	[[nodiscard]] not_null<ItemBase*> itemAt(int row, int column) const {
		return Downcast(Parent::itemAt(row, column));
	}
	[[nodiscard]] not_null<ItemBase*> itemAt(int index) const {
		return Downcast(Parent::itemAt(index));
	}
	[[nodiscard]] ItemBase *maybeItemAt(int row, int column) const {
		return Downcast(Parent::maybeItemAt(row, column));
	}
	[[nodiscard]] ItemBase *maybeItemAt(int index) const {
		return Downcast(Parent::maybeItemAt(index));
	}

	void forEach(Fn<void(not_null<const ItemBase*>)> callback) {
		Parent::forEach([&](
				not_null<const AbstractLayoutItem*> item) {
			callback(Downcast(item));
		});
	}

	void paint(
			Fn<void(not_null<ItemBase*>, QPoint)> paintItem,
			const QRect &clip) const {
		Parent::paint([&](
				not_null<AbstractLayoutItem*> item,
				QPoint point) {
			paintItem(Downcast(item), point);
		}, clip);
	}

	int validateExistingRows(
			Fn<bool(not_null<const ItemBase*>, int)> checkItem,
			int count) {
		return Parent::validateExistingRows([&](
				not_null<const AbstractLayoutItem*> item,
				int until) {
			return checkItem(Downcast(item), until);
		}, count);
	}

private:
	[[nodiscard]] static not_null<ItemBase*> Downcast(
			not_null<AbstractLayoutItem*> item) {
		return static_cast<ItemBase*>(item.get());
	}
	[[nodiscard]] static ItemBase *Downcast(
			AbstractLayoutItem *item) {
		return static_cast<ItemBase*>(item);
	}
	[[nodiscard]] static not_null<const ItemBase*> Downcast(
			not_null<const AbstractLayoutItem*> item) {
		return static_cast<const ItemBase*>(item.get());
	}
	[[nodiscard]] static const ItemBase *Downcast(
			const AbstractLayoutItem *item) {
		return static_cast<const ItemBase*>(item);
	}

};

} // namespace Mosaic::Layout
