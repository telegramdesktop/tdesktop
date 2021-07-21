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

namespace InlineBots {
namespace Layout {

class ItemBase;
using Results = std::vector<std::unique_ptr<Result>>;

class MosaicLayout final {
public:
	struct Row {
		int maxWidth = 0;
		int height = 0;
		std::vector<ItemBase*> items;
	};
	MosaicLayout() = default;

	int rowHeightAt(int row);

	void addItems(const std::vector<ItemBase*> &items);
	void addItem(ItemBase *item, Row &row, int32 &sumWidth);

	void setFullWidth(int w);
	bool empty() const;

	int rowsCount() const;
	int columnsCountAt(int row) const;

	not_null<ItemBase*> itemAt(int row, int column) const;
	ItemBase *maybeItemAt(int row, int column) const;

	void preloadImages();

	bool rowFinalize(Row &row, int32 &sumWidth, bool force);
	void layoutRow(Row &row, int fullWidth);
	void clearRows(bool resultsDeleted);
	int validateExistingRows(const Results &results);

	int countDesiredHeight(int newWidth);

	void paint(
		Painter &p,
		int top,
		int startLeft,
		const QRect &clip,
		PaintContext &context);

private:
	int _width = 0;
	std::vector<Row> _rows;
};

} // namespace Layout
} // namespace InlineBots
