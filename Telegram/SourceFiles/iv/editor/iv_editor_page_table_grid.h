/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_rich_page.h"
#include "iv/markdown/iv_markdown_prepare.h"

#include <vector>

namespace Iv::Editor {

using TableGridOccupancyRow = std::vector<char>;
using TableGridOccupancy = std::vector<TableGridOccupancyRow>;

struct TableGridCellReference {
	int rowIndex = -1;
	int cellIndex = -1;
	int rowFrom = -1;
	int rowTill = -1;
	int columnFrom = -1;
	int columnTill = -1;
};

struct TableGrid {
	std::vector<TableGridCellReference> cells;
	TableGridOccupancy occupancy;
	int rowCount = 0;
	int columnCount = 0;
};

// Two grid builders on purpose. The limited overload clamps rows, columns and
// total cells to the article render limits, the other one only bounds columns
// by the widest row. They are not interchangeable.
[[nodiscard]] int NormalizeTableSpan(int span);

[[nodiscard]] int ClampTableRowspan(
	int rawRowspan,
	int row,
	int rowCount);

[[nodiscard]] int ClampTableColspan(
	int rawColspan,
	int column,
	int maxColumns);

[[nodiscard]] bool CanOccupyTableSlots(
	const TableGridOccupancy &occupancy,
	int row,
	int column,
	int rowspan,
	int colspan);

[[nodiscard]] int FirstAvailableTableColumn(
	const TableGridOccupancy &occupancy,
	int row,
	int rowspan,
	int colspan,
	int maxColumns);

void MarkTableSlots(
	TableGridOccupancy *occupancy,
	int row,
	int column,
	int rowspan,
	int colspan);

[[nodiscard]] int TableGridColumnCount(
	const TableGridOccupancy &occupancy);

[[nodiscard]] int TableMaxColumns(const RichPage::Block &table);

[[nodiscard]] TableGrid BuildTableGrid(
	const RichPage::Block &table,
	const Markdown::MarkdownPrepareTableRenderLimits &limits);

[[nodiscard]] TableGrid BuildTableGrid(const RichPage::Block &table);

template <typename Range>
bool TableGridCellIntersectsRange(
		const TableGridCellReference &cell,
		const Range &range) {
	return (cell.rowFrom < range.rowTill)
		&& (cell.rowTill > range.rowFrom)
		&& (cell.columnFrom < range.columnTill)
		&& (cell.columnTill > range.columnFrom);
}

template <typename Range>
std::vector<TableGridCellReference> SelectedTableGridCells(
		const TableGrid &grid,
		const Range &range) {
	auto result = std::vector<TableGridCellReference>();
	result.reserve(grid.cells.size());
	for (const auto &cell : grid.cells) {
		if (TableGridCellIntersectsRange(cell, range)) {
			result.push_back(cell);
		}
	}
	return result;
}

} // namespace Iv::Editor
