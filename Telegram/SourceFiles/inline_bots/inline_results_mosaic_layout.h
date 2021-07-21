/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "inline_bots/inline_bot_layout_item.h"

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace InlineBots::Layout {

using Results = std::vector<std::unique_ptr<Result>>;

class MosaicLayout final {
public:
	struct FoundItem {
		ClickHandlerPtr link;
		ItemBase *item = nullptr;
		int index = -1;
	};
	MosaicLayout(int bigWidth);

	[[nodiscard]] int rowHeightAt(int row);
	[[nodiscard]] int countDesiredHeight(int newWidth);

	[[nodiscard]] FoundItem findByPoint(const QPoint &globalPoint);

	void addItems(const std::vector<ItemBase*> &items);

	void setFullWidth(int w);
	[[nodiscard]] bool empty() const;

	[[nodiscard]] int rowsCount() const;
	[[nodiscard]] int columnsCountAt(int row) const;

	[[nodiscard]] not_null<ItemBase*> itemAt(int row, int column) const;
	[[nodiscard]] not_null<ItemBase*> itemAt(int index) const;
	[[nodiscard]] ItemBase *maybeItemAt(int row, int column) const;
	[[nodiscard]] ItemBase *maybeItemAt(int index) const;

	void clearRows(bool resultsDeleted);
	[[nodiscard]]int validateExistingRows(const Results &results);

	void preloadImages();

	void paint(
		Painter &p,
		int top,
		int startLeft,
		const QRect &clip,
		PaintContext &context);

private:
	struct Row {
		int maxWidth = 0;
		int height = 0;
		std::vector<ItemBase*> items;
	};

	void addItem(not_null<ItemBase*> item, Row &row, int &sumWidth);

	bool rowFinalize(Row &row, int &sumWidth, bool force);
	void layoutRow(Row &row, int fullWidth);

	const int _bigWidth;
	int _width = 0;
	std::vector<Row> _rows;
};

} // namespace InlineBots::Layout
