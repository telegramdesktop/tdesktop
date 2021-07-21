/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_results_mosaic_layout.h"

#include "history/view/history_view_cursor_state.h"
#include "layout/layout_utils.h"
#include "styles/style_chat_helpers.h"

namespace InlineBots::Layout {
namespace {

constexpr auto kInlineItemsMaxPerRow = 5;

} // namespace

MosaicLayout::MosaicLayout(int bigWidth)
: _bigWidth(bigWidth) {
}

void MosaicLayout::setFullWidth(int w) {
	_width = w;
}

bool MosaicLayout::empty() const {
	return _rows.empty();
}

int MosaicLayout::rowsCount() const {
	return _rows.size();
}

int MosaicLayout::countDesiredHeight(int newWidth) {
	auto result = 0;
	for (auto &row : _rows) {
		layoutRow(row, newWidth);
		result += row.height;
	}
	return result;
}

not_null<ItemBase*> MosaicLayout::itemAt(int row, int column) const {
	Expects((row >= 0)
		&& (row < _rows.size())
		&& (column >= 0)
		&& (column < _rows[row].items.size()));
	return _rows[row].items[column];
}

not_null<ItemBase*> MosaicLayout::itemAt(int index) const {
	const auto &[row, column] = ::Layout::IndexToPosition(index);
	return itemAt(row, column);
}

ItemBase *MosaicLayout::maybeItemAt(int row, int column) const {
	if ((row >= 0)
		&& (row < _rows.size())
		&& (column >= 0)
		&& (column < _rows[row].items.size())) {
		return _rows[row].items[column];
	}
	return nullptr;
}

ItemBase *MosaicLayout::maybeItemAt(int index) const {
	const auto &[row, column] = ::Layout::IndexToPosition(index);
	return maybeItemAt(row, column);
}

void MosaicLayout::addItems(const std::vector<ItemBase*> &items) {
	_rows.reserve(items.size());
	auto row = Row();
	row.items.reserve(kInlineItemsMaxPerRow);
	auto sumWidth = 0;
	for (const auto &item : items) {
		addItem(item, row, sumWidth);
	}
	rowFinalize(row, sumWidth, true);
}

void MosaicLayout::addItem(
		not_null<ItemBase*> item,
		Row &row,
		int &sumWidth) {
	item->preload();

	using namespace ::Layout;
	item->setPosition(PositionToIndex(_rows.size(), row.items.size()));
	if (rowFinalize(row, sumWidth, item->isFullLine())) {
		item->setPosition(PositionToIndex(_rows.size(), 0));
	}

	sumWidth += item->maxWidth();
	if (!row.items.empty() && row.items.back()->hasRightSkip()) {
		sumWidth += st::inlineResultsSkip;
	}

	row.items.push_back(item);
}

bool MosaicLayout::rowFinalize(Row &row, int &sumWidth, bool force) {
	if (row.items.empty()) {
		return false;
	}

	const auto full = (row.items.size() >= kInlineItemsMaxPerRow);
	// Currently use the same GIFs layout for all widget sizes.
	const auto big = (sumWidth >= _bigWidth);
	if (full || big || force) {
		row.maxWidth = (full || big) ? sumWidth : 0;
		layoutRow(row, _width);
		_rows.push_back(std::move(row));
		row = Row();
		row.items.reserve(kInlineItemsMaxPerRow);
		sumWidth = 0;
		return true;
	}
	return false;
}

void MosaicLayout::layoutRow(Row &row, int fullWidth) {
	const auto count = int(row.items.size());
	Assert(count <= kInlineItemsMaxPerRow);

	// Enumerate items in the order of growing maxWidth()
	// for that sort item indices by maxWidth().
	int indices[kInlineItemsMaxPerRow];
	for (auto i = 0; i != count; ++i) {
		indices[i] = i;
	}
	std::sort(indices, indices + count, [&](int a, int b) {
		return row.items[a]->maxWidth() < row.items[b]->maxWidth();
	});

	auto desiredWidth = row.maxWidth;
	row.height = 0;
	auto availableWidth = fullWidth
		- (st::inlineResultsLeft - st::roundRadiusSmall);
	for (auto i = 0; i < count; ++i) {
		const auto index = indices[i];
		const auto &item = row.items[index];
		const auto w = desiredWidth
			? (item->maxWidth() * availableWidth / desiredWidth)
			: item->maxWidth();
		const auto actualWidth = std::max(w, st::inlineResultsMinWidth);
		row.height = std::max(row.height, item->resizeGetHeight(actualWidth));
		if (desiredWidth) {
			availableWidth -= actualWidth;
			desiredWidth -= row.items[index]->maxWidth();
			if (index > 0 && row.items[index - 1]->hasRightSkip()) {
				availableWidth -= st::inlineResultsSkip;
				desiredWidth -= st::inlineResultsSkip;
			}
		}
	}
}

void MosaicLayout::paint(
		Painter &p,
		int top,
		int startLeft,
		const QRect &clip,
		PaintContext &context) {
	const auto fromX = rtl() ? (_width - clip.x() - clip.width()) : clip.x();
	const auto toX = rtl() ? (_width - clip.x()) : (clip.x() + clip.width());
	const auto rows = _rows.size();
	for (auto row = 0; row != rows; ++row) {
		if (top >= clip.top() + clip.height()) {
			break;
		}
		auto &inlineRow = _rows[row];
		if ((top + inlineRow.height) > clip.top()) {
			auto left = startLeft;
			if (row == (rows - 1)) {
				context.lastRow = true;
			}
			for (const auto &item : inlineRow.items) {
				if (left >= toX) {
					break;
				}

				const auto w = item->width();
				if ((left + w) > fromX) {
					p.translate(left, top);
					item->paint(p, clip.translated(-left, -top), &context);
					p.translate(-left, -top);
				}
				left += w;
				if (item->hasRightSkip()) {
					left += st::inlineResultsSkip;
				}
			}
		}
		top += inlineRow.height;
	}
}

void MosaicLayout::clearRows(bool resultsDeleted) {
	if (!resultsDeleted) {
		for (const auto &row : _rows) {
			for (const auto &item : row.items) {
				item->setPosition(-1);
			}
		}
	}
	_rows.clear();
}

void MosaicLayout::preloadImages() {
	for (const auto &row : _rows) {
		for (const auto &item : row.items) {
			item->preload();
		}
	}
}

int MosaicLayout::validateExistingRows(const Results &results) {
	const auto count = results.size();
	auto until = 0;
	auto untilRow = 0;
	auto untilCol = 0;
	while (until < count) {
		auto &rowItems = _rows[untilRow].items;
		if ((untilRow >= _rows.size())
			|| (rowItems[untilCol]->getResult() != results[until].get())) {
			break;
		}
		++until;
		if (++untilCol == rowItems.size()) {
			++untilRow;
			untilCol = 0;
		}
	}
	if (until == count) { // All items are layed out.
		if (untilRow == _rows.size()) { // Nothing changed.
			return until;
		}

		{
			const auto rows = _rows.size();
			auto skip = untilCol;
			for (auto i = untilRow; i < rows; ++i) {
				for (const auto &item : _rows[i].items) {
					if (skip) {
						--skip;
					} else {
						item->setPosition(-1);
					}
				}
			}
		}
		if (!untilCol) { // All good rows are filled.
			_rows.resize(untilRow);
			return until;
		}
		_rows.resize(untilRow + 1);
		_rows[untilRow].items.resize(untilCol);
		_rows[untilRow].maxWidth = ranges::accumulate(
			_rows[untilRow].items,
			0,
			[](int w, auto &row) { return w + row->maxWidth(); });
		layoutRow(_rows[untilRow], _width);
		return until;
	}
	if (untilRow && !untilCol) { // Remove last row, maybe it is not full.
		--untilRow;
		untilCol = _rows[untilRow].items.size();
	}
	until -= untilCol;

	for (auto i = untilRow; i < _rows.size(); ++i) {
		for (const auto &item : _rows[i].items) {
			item->setPosition(-1);
		}
	}
	_rows.resize(untilRow);

	return until;
}

int MosaicLayout::columnsCountAt(int row) const {
	Expects(row >= 0 && row < _rows.size());
	return _rows[row].items.size();
}

int MosaicLayout::rowHeightAt(int row) {
	Expects(row >= 0 && row < _rows.size());
	return _rows[row].height;
}

MosaicLayout::FoundItem MosaicLayout::findByPoint(const QPoint &globalPoint) {
	auto sx = globalPoint.x();
	auto sy = globalPoint.y();
	auto row = -1;
	auto col = -1;
	auto sel = -1;
	ClickHandlerPtr link;
	ItemBase *item = nullptr;
	if (sy >= 0) {
		row = 0;
		for (auto rows = rowsCount(); row < rows; ++row) {
			const auto rowHeight = _rows[row].height;
			if (sy < rowHeight) {
				break;
			}
			sy -= rowHeight;
		}
	}
	if (sx >= 0 && row >= 0 && row < rowsCount()) {
		const auto columnsCount = _rows[row].items.size();
		col = 0;
		for (int cols = columnsCount; col < cols; ++col) {
			const auto item = itemAt(row, col);
			const auto width = item->width();
			if (sx < width) {
				break;
			}
			sx -= width;
			if (item->hasRightSkip()) {
				sx -= st::inlineResultsSkip;
			}
		}
		if (col < columnsCount) {
			item = itemAt(row, col);
			sel = ::Layout::PositionToIndex(row, + col);
			const auto result = item->getState(QPoint(sx, sy), {});
			link = result.link;
		} else {
			row = col = -1;
		}
	} else {
		row = col = -1;
	}
	return { link, item, sel };
}

} // namespace InlineBots::Layout
