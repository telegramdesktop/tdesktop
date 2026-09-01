/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_page_table_grid.h"

#include <algorithm>

namespace Iv::Editor {
namespace {

using Block = RichPage::Block;

} // namespace

int NormalizeTableSpan(int span) {
	return std::max(span, 1);
}

int ClampTableRowspan(
		int rawRowspan,
		int row,
		int rowCount) {
	if ((row < 0) || (row >= rowCount) || (rowCount <= 0)) {
		return 0;
	}
	const auto remainingRows = int64(rowCount) - row;
	return int(std::min<int64>(NormalizeTableSpan(rawRowspan), remainingRows));
}

int ClampTableColspan(
		int rawColspan,
		int column,
		int maxColumns) {
	if ((column < 0) || (column >= maxColumns) || (maxColumns <= 0)) {
		return 0;
	}
	const auto remainingColumns = int64(maxColumns) - column;
	return int(std::min<int64>(
		NormalizeTableSpan(rawColspan),
		remainingColumns));
}

bool CanOccupyTableSlots(
		const TableGridOccupancy &occupancy,
		int row,
		int column,
		int rowspan,
		int colspan) {
	if ((row < 0)
		|| (column < 0)
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (row >= int(occupancy.size()))) {
		return false;
	}
	const auto rowLimit = int(std::min<int64>(
		int64(row) + rowspan,
		occupancy.size()));
	const auto columnLimit64 = int64(column) + colspan;
	if (columnLimit64 <= column) {
		return false;
	}
	const auto columnLimit = int(std::min<int64>(
		columnLimit64,
		std::numeric_limits<int>::max()));
	for (auto currentRow = row; currentRow < rowLimit; ++currentRow) {
		const auto &occupied = occupancy[currentRow];
		const auto occupiedLimit = std::min(columnLimit, int(occupied.size()));
		for (auto currentColumn = column;
			currentColumn < occupiedLimit;
			++currentColumn) {
			if (occupied[currentColumn]) {
				return false;
			}
		}
	}
	return true;
}

int FirstAvailableTableColumn(
		const TableGridOccupancy &occupancy,
		int row,
		int rowspan,
		int colspan,
		int maxColumns) {
	if ((row < 0)
		|| (row >= int(occupancy.size()))
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (maxColumns <= 0)) {
		return -1;
	}
	for (auto column = 0; column < maxColumns; ++column) {
		const auto effectiveColspan = ClampTableColspan(
			colspan,
			column,
			maxColumns);
		if (effectiveColspan <= 0) {
			continue;
		}
		if (CanOccupyTableSlots(
				occupancy,
				row,
				column,
				rowspan,
				effectiveColspan)) {
			return column;
		}
	}
	return -1;
}

void MarkTableSlots(
		TableGridOccupancy *occupancy,
		int row,
		int column,
		int rowspan,
		int colspan) {
	if ((row < 0)
		|| (column < 0)
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (row >= int(occupancy->size()))) {
		return;
	}
	const auto rowLimit = int(std::min<int64>(
		int64(row) + rowspan,
		occupancy->size()));
	const auto columnLimit64 = int64(column) + colspan;
	if (columnLimit64 <= column) {
		return;
	}
	const auto columnLimit = int(std::min<int64>(
		columnLimit64,
		std::numeric_limits<int>::max()));
	for (auto currentRow = row; currentRow < rowLimit; ++currentRow) {
		auto &occupied = (*occupancy)[currentRow];
		if (columnLimit > int(occupied.size())) {
			occupied.resize(columnLimit, false);
		}
		for (auto currentColumn = column;
			currentColumn < columnLimit;
			++currentColumn) {
			occupied[currentColumn] = true;
		}
	}
}

int TableGridColumnCount(
		const TableGridOccupancy &occupancy) {
	auto result = 0;
	for (const auto &row : occupancy) {
		result = std::max(result, int(row.size()));
	}
	return result;
}

int TableMaxColumns(const RichPage::Block &table) {
	auto result = 0;
	for (const auto &row : table.tableRows) {
		auto columns = 0;
		for (const auto &cell : row.cells) {
			columns += NormalizeTableSpan(cell.colspan);
		}
		result = std::max(result, columns);
	}
	return result;
}

TableGrid BuildTableGrid(
		const Block &table,
		const Markdown::MarkdownPrepareTableRenderLimits &limits) {
	auto result = TableGrid();
	result.rowCount = std::min(int(table.tableRows.size()), limits.maxRows);
	if (result.rowCount < 0) {
		result.rowCount = 0;
	}
	result.occupancy = TableGridOccupancy(result.rowCount);
	auto occupiedSlotCountSoFar = int64(0);

	for (auto rowIndex = 0; rowIndex != result.rowCount; ++rowIndex) {
		const auto &row = table.tableRows[rowIndex];
		for (auto cellIndex = 0, cellCount = int(row.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			const auto &cell = row.cells[cellIndex];
			const auto normalizedColspan = NormalizeTableSpan(cell.colspan);
			const auto rowspan = ClampTableRowspan(
				cell.rowspan,
				rowIndex,
				result.rowCount);
			if (rowspan <= 0) {
				continue;
			}
			const auto column = FirstAvailableTableColumn(
				result.occupancy,
				rowIndex,
				rowspan,
				normalizedColspan,
				limits.maxColumns);
			if (column < 0) {
				continue;
			}
			const auto colspan = ClampTableColspan(
				normalizedColspan,
				column,
				limits.maxColumns);
			if (colspan <= 0) {
				continue;
			}
			const auto occupiedSlotGrowth = int64(rowspan) * colspan;
			if (occupiedSlotGrowth > limits.maxCells
				|| (occupiedSlotCountSoFar + occupiedSlotGrowth)
					> limits.maxCells) {
				continue;
			}
			result.cells.push_back({
				.rowIndex = rowIndex,
				.cellIndex = cellIndex,
				.rowFrom = rowIndex,
				.rowTill = rowIndex + rowspan,
				.columnFrom = column,
				.columnTill = column + colspan,
			});
			MarkTableSlots(
				&result.occupancy,
				rowIndex,
				column,
				rowspan,
				colspan);
			occupiedSlotCountSoFar += occupiedSlotGrowth;
		}
	}
	result.columnCount = TableGridColumnCount(result.occupancy);
	return result;
}

TableGrid BuildTableGrid(const Block &table) {
	auto result = TableGrid();
	result.rowCount = int(table.tableRows.size());
	result.occupancy = TableGridOccupancy(result.rowCount);
	const auto maxColumns = TableMaxColumns(table);
	if (result.rowCount <= 0 || maxColumns <= 0) {
		return result;
	}
	for (auto rowIndex = 0; rowIndex != result.rowCount; ++rowIndex) {
		const auto &row = table.tableRows[rowIndex];
		for (auto cellIndex = 0, cellCount = int(row.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			const auto &cell = row.cells[cellIndex];
			const auto normalizedColspan = NormalizeTableSpan(cell.colspan);
			const auto rowspan = ClampTableRowspan(
				cell.rowspan,
				rowIndex,
				result.rowCount);
			if (rowspan <= 0) {
				continue;
			}
			const auto column = FirstAvailableTableColumn(
				result.occupancy,
				rowIndex,
				rowspan,
				normalizedColspan,
				maxColumns);
			if (column < 0) {
				continue;
			}
			const auto colspan = ClampTableColspan(
				normalizedColspan,
				column,
				maxColumns);
			if (colspan <= 0) {
				continue;
			}
			result.cells.push_back({
				.rowIndex = rowIndex,
				.cellIndex = cellIndex,
				.rowFrom = rowIndex,
				.rowTill = rowIndex + rowspan,
				.columnFrom = column,
				.columnTill = column + colspan,
			});
			MarkTableSlots(
				&result.occupancy,
				rowIndex,
				column,
				rowspan,
				colspan);
		}
	}
	result.columnCount = TableGridColumnCount(result.occupancy);
	return result;
}

} // namespace Iv::Editor
