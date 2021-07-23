/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "overview/overview_layout.h"

namespace Overview::Layout {

class MosaicLayout final {
public:
	struct FoundItem {
		ClickHandlerPtr link;
		ItemBase *item = nullptr;
		int index = -1;
		bool exact = false;
	};
	MosaicLayout();

	[[nodiscard]] int rowHeightAt(int row) const;
	[[nodiscard]] int countDesiredHeight(int newWidth);

	[[nodiscard]] FoundItem findByPoint(const QPoint &globalPoint) const;
	[[nodiscard]] QRect findRect(int index) const;

	void addItems(const std::vector<not_null<ItemBase*>> &items);

	void setRightSkip(int rightSkip);
	void setFullWidth(int w);
	void setOffset(int left, int top);
	[[nodiscard]] bool empty() const;

	[[nodiscard]] int rowsCount() const;
	[[nodiscard]] int columnsCountAt(int row) const;

	[[nodiscard]] not_null<ItemBase*> itemAt(int row, int column) const;
	[[nodiscard]] not_null<ItemBase*> itemAt(int index) const;
	[[nodiscard]] ItemBase *maybeItemAt(int row, int column) const;
	[[nodiscard]] ItemBase *maybeItemAt(int index) const;

	void clearRows(bool resultsDeleted);

	void preloadImages();

	void paint(
		Fn<void(const not_null<ItemBase*>, QPoint)> paintItemCallback,
		const QRect &clip) const;

private:
	struct Row {
		int maxWidth = 0;
		int height = 0;
		std::vector<ItemBase*> items;
	};

	void addItem(not_null<ItemBase*> item, Row &row, int &sumWidth);

	bool rowFinalize(Row &row, int &sumWidth, bool force);
	void layoutRow(Row &row, int fullWidth);

	int _bigWidth;
	int _width = 0;
	int _rightSkip = 0;
	QPoint _offset;
	std::vector<Row> _rows;
};

} // namespace Overview::Layout
