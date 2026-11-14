/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_page_table_remove.h"

#include "iv/editor/iv_editor_page_table_grid.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Iv::Editor {
namespace {

using Block = RichPage::Block;
using TableCell = RichPage::TableCell;

[[nodiscard]] const TableGridCellReference *FindTableGridCell(
		const TableGrid &grid,
		int rowIndex,
		int cellIndex) {
	const auto i = std::lower_bound(
		grid.cells.begin(),
		grid.cells.end(),
		std::make_pair(rowIndex, cellIndex),
		[](const TableGridCellReference &cell, std::pair<int, int> key) {
			return std::make_pair(cell.rowIndex, cell.cellIndex) < key;
		});
	return (i != grid.cells.end()
			&& i->rowIndex == rowIndex
			&& i->cellIndex == cellIndex)
		? &*i
		: nullptr;
}

void MarkTableGridSlotsBelowRow(
		TableGridOccupancy *occupancy,
		const TableGrid &grid,
		int row) {
	occupancy->resize(std::max(grid.rowCount - row, 1));
	for (const auto &cell : grid.cells) {
		const auto rowFrom = std::max(cell.rowFrom, row + 1);
		MarkTableSlots(
			occupancy,
			rowFrom - row,
			cell.columnFrom,
			cell.rowTill - rowFrom,
			cell.columnTill - cell.columnFrom);
	}
}

} // namespace

void EraseTableRowsKeepingSpans(
		Block *table,
		const Markdown::MarkdownPrepareTableRenderLimits &limits,
		int from,
		int till) {
	struct Staged {
		int rowIndex = 0;
		int cellIndex = 0;
		const TableGridCellReference *reference = nullptr;
	};
	auto &rows = table->tableRows;
	const auto rowCount = int(rows.size());
	const auto grid = BuildTableGrid(*table, limits);
	auto occupancy = TableGridOccupancy(1);
	auto staged = std::vector<Staged>();
	for (auto rowIndex = 0; rowIndex != till; ++rowIndex) {
		auto &cells = rows[rowIndex].cells;
		for (auto cellIndex = 0, count = int(cells.size());
				cellIndex != count;
				++cellIndex) {
			auto &cell = cells[cellIndex];
			const auto cellTill = rowIndex
				+ ClampTableRowspan(cell.rowspan, rowIndex, rowCount);
			const auto covered = std::min(cellTill, till)
				- std::max(rowIndex, from);
			if (covered <= 0) {
				continue;
			}
			cell.rowspan = std::max(
				NormalizeTableSpan(cell.rowspan) - covered,
				1);
			if (cellTill <= till) {
				continue;
			}
			const auto reference = FindTableGridCell(
				grid,
				rowIndex,
				cellIndex);
			if (rowIndex >= from) {
				staged.push_back({
					.rowIndex = rowIndex,
					.cellIndex = cellIndex,
					.reference = reference,
				});
			} else if (reference) {
				MarkTableSlots(
					&occupancy,
					0,
					reference->columnFrom,
					1,
					reference->columnTill - reference->columnFrom);
			}
		}
	}
	if (!staged.empty()) {
		for (auto cellIndex = 0, count = int(rows[till].cells.size());
				cellIndex != count;
				++cellIndex) {
			staged.push_back({
				.rowIndex = till,
				.cellIndex = cellIndex,
				.reference = FindTableGridCell(grid, till, cellIndex),
			});
		}
		const auto column = [](const Staged &entry) {
			return entry.reference
				? entry.reference->columnFrom
				: std::numeric_limits<int>::max();
		};
		std::stable_sort(
			staged.begin(),
			staged.end(),
			[&](const Staged &a, const Staged &b) {
				return column(a) < column(b);
			});
		if (!staged.back().reference) {
			MarkTableGridSlotsBelowRow(&occupancy, grid, till);
		}
		const auto firstFit = [&](int colspan) {
			return FirstAvailableTableColumn(
				occupancy,
				0,
				1,
				colspan,
				limits.maxColumns);
		};
		auto destination = std::vector<TableCell>();
		destination.reserve(staged.size());
		const auto fillBefore = [&](int colspan, int target) {
			for (auto fit = firstFit(colspan);
					fit >= 0 && fit < target;
					fit = firstFit(colspan)) {
				MarkTableSlots(&occupancy, 0, firstFit(1), 1, 1);
				destination.push_back(TableCell());
			}
		};
		for (const auto &entry : staged) {
			auto &cell = rows[entry.rowIndex].cells[entry.cellIndex];
			const auto colspan = NormalizeTableSpan(cell.colspan);
			if (const auto reference = entry.reference) {
				fillBefore(colspan, reference->columnFrom);
				MarkTableSlots(
					&occupancy,
					0,
					reference->columnFrom,
					1,
					reference->columnTill - reference->columnFrom);
			} else {
				// An omitted cell must not displace a rendered one.
				const auto rowspan = ClampTableRowspan(
					cell.rowspan,
					from,
					rowCount - (till - from));
				const auto placed = FirstAvailableTableColumn(
					occupancy,
					0,
					rowspan,
					colspan,
					limits.maxColumns);
				if (placed < 0) {
					fillBefore(colspan, limits.maxColumns);
				} else {
					fillBefore(colspan, placed);
					MarkTableSlots(
						&occupancy,
						0,
						placed,
						rowspan,
						ClampTableColspan(
							colspan,
							placed,
							limits.maxColumns));
				}
			}
			destination.push_back(std::move(cell));
		}
		rows[till].cells = std::move(destination);
	}
	rows.erase(rows.begin() + from, rows.begin() + till);
}

} // namespace Iv::Editor
