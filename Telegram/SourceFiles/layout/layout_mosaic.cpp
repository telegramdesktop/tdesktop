/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "layout/layout_mosaic.h"

namespace Mosaic::Layout {

AbstractMosaicLayout::AbstractMosaicLayout(int bigWidth)
: _bigWidth(bigWidth) {
}

int AbstractMosaicLayout::rowHeightAt(int row) const {
	Expects(row >= 0 && row < _rows.size());

	return _rows[row].height;
}

int AbstractMosaicLayout::countDesiredHeight(int newWidth) {
	auto result = 0;
	for (auto &row : _rows) {
		layoutRow(row, newWidth ? newWidth : _width);
		result += row.height;
	}
	return result;
}

FoundItem AbstractMosaicLayout::findByPoint(const QPoint &globalPoint) const {
	auto sx = globalPoint.x() - _offset.x();
	auto sy = globalPoint.y() - _offset.y();
	auto row = -1;
	auto col = -1;
	auto sel = -1;
	bool exact = true;
	if (sy >= 0) {
		row = 0;
		for (auto rows = rowsCount(); row < rows; ++row) {
			const auto rowHeight = _rows[row].height;
			if (sy < rowHeight) {
				break;
			}
			sy -= rowHeight;
		}
	} else {
		row = 0;
		exact = false;
	}
	if (row >= rowsCount()) {
		row = rowsCount() - 1;
		exact = false;
	}
	if (sx < 0) {
		sx = 0;
		exact = false;
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
			sx -= _rightSkip;
		}
		if (col >= columnsCount) {
			col = columnsCount - 1;
			exact = false;
		}

		sel = ::Layout::PositionToIndex(row, + col);
	} else {
		row = col = -1;
	}
	return { sel, exact, QPoint(sx, sy) };
}

QRect AbstractMosaicLayout::findRect(int index) const {
	const auto clip = QRect(0, 0, _width, 100);
	const auto fromX = style::RightToLeft()
		? (_width - clip.x() - clip.width())
		: clip.x();
	const auto toX = style::RightToLeft()
		? (_width - clip.x())
		: (clip.x() + clip.width());
	const auto rows = _rows.size();
	auto top = 0;
	for (auto row = 0; row != rows; ++row) {
		auto &inlineRow = _rows[row];
		// if ((top + inlineRow.height) > clip.top()) {
			auto left = 0;
			if (row == (rows - 1)) {
//				context.lastRow = true;
			}
			for (const auto &item : inlineRow.items) {
				if (left >= toX) {
					break;
				}

				const auto w = item->width();
				if ((left + w) > fromX) {
					if (item->position() == index) {
						return QRect(
							left + _offset.x(),
							top + _offset.y(),
							item->width(),
							item->height());
					}
				}
				left += w;
				left += _rightSkip;
			}
		// }
		top += inlineRow.height;
	}
	return QRect();
}

void AbstractMosaicLayout::addItems(
		gsl::span<const not_null<AbstractLayoutItem*>> items) {
	_rows.reserve(items.size());
	auto row = Row();
	row.items.reserve(kInlineItemsMaxPerRow);
	auto sumWidth = 0;
	for (const auto &item : items) {
		addItem(item, row, sumWidth);
	}
	rowFinalize(row, sumWidth, true);
}

void AbstractMosaicLayout::setRightSkip(int rightSkip) {
	_rightSkip = rightSkip;
}

void AbstractMosaicLayout::setOffset(int left, int top) {
	_offset = { left, top };
}

void AbstractMosaicLayout::setFullWidth(int w) {
	_width = w;
}

bool AbstractMosaicLayout::empty() const {
	return _rows.empty();
}

int AbstractMosaicLayout::rowsCount() const {
	return _rows.size();
}

not_null<AbstractLayoutItem*> AbstractMosaicLayout::itemAt(
		int row,
		int column) const {
	Expects((row >= 0)
		&& (row < _rows.size())
		&& (column >= 0)
		&& (column < _rows[row].items.size()));

	return _rows[row].items[column];
}

not_null<AbstractLayoutItem*> AbstractMosaicLayout::itemAt(int index) const {
	const auto &[row, column] = ::Layout::IndexToPosition(index);
	return itemAt(row, column);
}

AbstractLayoutItem *AbstractMosaicLayout::maybeItemAt(
		int row,
		int column) const {
	if ((row >= 0)
		&& (row < _rows.size())
		&& (column >= 0)
		&& (column < _rows[row].items.size())) {
		return _rows[row].items[column];
	}
	return nullptr;
}

AbstractLayoutItem *AbstractMosaicLayout::maybeItemAt(int index) const {
	const auto &[row, column] = ::Layout::IndexToPosition(index);
	return maybeItemAt(row, column);
}

void AbstractMosaicLayout::clearRows(bool resultsDeleted) {
	if (!resultsDeleted) {
		for (const auto &row : _rows) {
			for (const auto &item : row.items) {
				item->setPosition(-1);
			}
		}
	}
	_rows.clear();
}

void AbstractMosaicLayout::forEach(
		Fn<void(not_null<const AbstractLayoutItem*>)> callback) {
	for (const auto &row : _rows) {
		for (const auto &item : row.items) {
			callback(item);
		}
	}
}

void AbstractMosaicLayout::paint(
		Fn<void(not_null<AbstractLayoutItem*>, QPoint)> paintItem,
		const QRect &clip) const {
	auto top = _offset.y();
	const auto fromX = style::RightToLeft()
		? (_width - clip.x() - clip.width())
		: clip.x();
	const auto toX = style::RightToLeft()
		? (_width - clip.x())
		: (clip.x() + clip.width());
	const auto rows = _rows.size();
	for (auto row = 0; row != rows; ++row) {
		if (top >= clip.top() + clip.height()) {
			break;
		}
		auto &inlineRow = _rows[row];
		if ((top + inlineRow.height) > clip.top()) {
			auto left = _offset.x();
			if (row == (rows - 1)) {
//				context.lastRow = true;
			}
			for (const auto &item : inlineRow.items) {
				if (left >= toX) {
					break;
				}

				const auto w = item->width();
				if ((left + w) > fromX) {
					paintItem(item, QPoint(left, top));
				}
				left += w;
				left += _rightSkip;
			}
		}
		top += inlineRow.height;
	}
}

int AbstractMosaicLayout::validateExistingRows(
		Fn<bool(not_null<const AbstractLayoutItem*>, int)> checkItem,
		int count) {
	auto until = 0;
	auto untilRow = 0;
	auto untilCol = 0;
	while (until < count) {
		if ((untilRow >= _rows.size())
			|| checkItem(_rows[untilRow].items[untilCol], until)) {
			break;
		}
		++until;
		if (++untilCol == _rows[untilRow].items.size()) {
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

void AbstractMosaicLayout::addItem(
		not_null<AbstractLayoutItem*> item,
		Row &row,
		int &sumWidth) {
	// item->preload();

	using namespace ::Layout;
	item->setPosition(PositionToIndex(_rows.size(), row.items.size()));
	if (rowFinalize(row, sumWidth, false)) {
		item->setPosition(PositionToIndex(_rows.size(), 0));
	}

	sumWidth += item->maxWidth();
	if (!row.items.empty() && _rightSkip) {
		sumWidth += _rightSkip;
	}

	row.items.push_back(item);
}

bool AbstractMosaicLayout::rowFinalize(Row &row, int &sumWidth, bool force) {
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

void AbstractMosaicLayout::layoutRow(Row &row, int fullWidth) {
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
		row.height = std::max(
			row.height,
			item->resizeGetHeight(actualWidth));
		if (desiredWidth) {
			availableWidth -= actualWidth;
			desiredWidth -= row.items[index]->maxWidth();
			if (index > 0 && _rightSkip) {
				availableWidth -= _rightSkip;
				desiredWidth -= _rightSkip;
			}
		}
	}
}

} // namespace Mosaic::Layout
