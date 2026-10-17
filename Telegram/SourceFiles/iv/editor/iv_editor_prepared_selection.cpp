/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_prepared_selection.h"

#include "iv/editor/iv_editor_state.h"

#include <algorithm>

namespace Iv::Editor {
namespace {

using StateBlockContainerKind = State::BlockContainerKind;
using StateBlockContainerPath = State::BlockContainerPath;
using StateBlockPath = State::BlockPath;
using StateLeafKind = State::LeafKind;
using StateLeafPath = State::LeafPath;
using PreparedBlockContainerKind = Markdown::PreparedEditBlockContainerKind;
using PreparedBlockContainerPath = Markdown::PreparedEditBlockContainerPath;
using PreparedBlockContainerStep = Markdown::PreparedEditBlockContainerStep;
using PreparedBlockPath = Markdown::PreparedEditBlockPath;
using PreparedBlockRange = Markdown::PreparedEditBlockRange;
using PreparedListItemRange = Markdown::PreparedEditListItemRange;
using PreparedSelection = Markdown::PreparedEditSelection;
using PreparedSelectionKind = Markdown::PreparedEditSelectionKind;
using PreparedEditBlockContainerPath
	= Markdown::PreparedEditBlockContainerPath;
using PreparedEditBlockContainerStep
	= Markdown::PreparedEditBlockContainerStep;
using PreparedEditBlockContainerKind
	= Markdown::PreparedEditBlockContainerKind;
using PreparedEditBlockPath = Markdown::PreparedEditBlockPath;
using PreparedEditBlockRange = Markdown::PreparedEditBlockRange;
using PreparedEditBlockSource = Markdown::PreparedEditBlockSource;
using PreparedEditHit = Markdown::PreparedEditHit;
using PreparedEditHitKind = Markdown::PreparedEditHitKind;
using PreparedEditLeafKind = Markdown::PreparedEditLeafKind;
using PreparedEditLeafSource = Markdown::PreparedEditLeafSource;
using PreparedEditListItemRange = Markdown::PreparedEditListItemRange;
using PreparedEditListItemSource = Markdown::PreparedEditListItemSource;
using PreparedEditSelection = Markdown::PreparedEditSelection;
using PreparedEditSelectionKind = Markdown::PreparedEditSelectionKind;
using PreparedEditTableCellRange = Markdown::PreparedEditTableCellRange;
using PreparedEditTableCellSource = Markdown::PreparedEditTableCellSource;
using PreparedEditTableRowSource = Markdown::PreparedEditTableRowSource;

[[nodiscard]] bool PreparedContainerHasPrefix(
		const PreparedBlockContainerPath &path,
		const PreparedBlockContainerPath &prefix) {
	if (path.steps.size() < prefix.steps.size()) {
		return false;
	}
	return std::equal(
		prefix.steps.begin(),
		prefix.steps.end(),
		path.steps.begin());
}

[[nodiscard]] int CompareIntegers(int a, int b) {
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

[[nodiscard]] int ComparePreparedEditBlockContainerSteps(
		const PreparedEditBlockContainerStep &a,
		const PreparedEditBlockContainerStep &b) {
	if (const auto result = CompareIntegers(
			static_cast<int>(a.kind),
			static_cast<int>(b.kind))) {
		return result;
	} else if (const auto result = CompareIntegers(
			a.blockIndex,
			b.blockIndex)) {
		return result;
	}
	return CompareIntegers(a.listItemIndex, b.listItemIndex);
}

[[nodiscard]] int ComparePreparedEditBlockPaths(
		const PreparedEditBlockPath &a,
		const PreparedEditBlockPath &b) {
	if (const auto result = ComparePreparedEditBlockContainerPaths(
			a.container,
			b.container)) {
		return result;
	}
	return CompareIntegers(a.index, b.index);
}

[[nodiscard]] bool ValidPreparedEditBlockPath(
		const PreparedEditBlockPath &path) {
	return (path.index >= 0);
}

[[nodiscard]] PreparedEditBlockSource PreparedEditBlockSourceFromPath(
		PreparedEditBlockPath path) {
	return { .path = std::move(path) };
}

[[nodiscard]] StructuralOwner StructuralOwnerFromBlock(
		const PreparedEditBlockSource &source) {
	if (!ValidPreparedEditBlockPath(source.path)) {
		return {};
	}
	return {
		.kind = StructuralOwnerKind::Block,
		.block = source,
	};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromListItem(
		const PreparedEditListItemSource &source) {
	if (!ValidPreparedEditBlockPath(source.block)
		|| source.listItemIndex < 0) {
		return {};
	}
	return {
		.kind = StructuralOwnerKind::ListItem,
		.block = PreparedEditBlockSourceFromPath(source.block),
		.listItem = source,
	};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromTableRow(
		const PreparedEditTableRowSource &source) {
	if (!ValidPreparedEditBlockPath(source.block)
		|| source.tableRowIndex < 0) {
		return {};
	}
	return {
		.kind = StructuralOwnerKind::TableRow,
		.block = PreparedEditBlockSourceFromPath(source.block),
		.tableRow = source,
	};
}

[[nodiscard]] PreparedEditTableRowSource PreparedEditTableRowFromCell(
		const PreparedEditTableCellSource &source) {
	return {
		.block = source.block,
		.tableRowIndex = source.tableRowIndex,
	};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromTableCell(
		const PreparedEditTableCellSource &source) {
	if (!ValidPreparedEditBlockPath(source.block)
		|| source.tableRowIndex < 0
		|| source.tableCellIndex < 0
		|| source.column < 0
		|| source.colspan <= 0
		|| source.rowspan <= 0) {
		return {};
	}
	return {
		.kind = StructuralOwnerKind::TableCell,
		.block = PreparedEditBlockSourceFromPath(source.block),
		.tableRow = PreparedEditTableRowFromCell(source),
		.tableCell = source,
	};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromLeaf(
		const PreparedEditLeafSource &source) {
	if (!ValidPreparedEditBlockPath(source.block)) {
		return {};
	}
	switch (source.kind) {
	case PreparedEditLeafKind::ListItemText:
		return StructuralOwnerFromListItem({
			.block = source.block,
			.listItemIndex = source.listItemIndex,
		});
	case PreparedEditLeafKind::TableCellText:
		return {};
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::BlockCaption:
	case PreparedEditLeafKind::MathFormula:
		return StructuralOwnerFromBlock(
			PreparedEditBlockSourceFromPath(source.block));
	}
	return {};
}

[[nodiscard]] bool SameTableRangeBlock(
		const PreparedEditTableCellRange &a,
		const PreparedEditTableCellRange &b) {
	return !a.empty()
		&& !b.empty()
		&& SamePreparedEditBlockPath(a.block, b.block);
}

struct LiftedPreparedEditBlocks {
	PreparedEditBlockContainerPath container;
	int first = -1;
	int second = -1;
};

[[nodiscard]] PreparedEditBlockContainerPath PreparedEditBlockContainerPrefix(
		const PreparedEditBlockContainerPath &path,
		int count) {
	auto result = PreparedEditBlockContainerPath();
	const auto till = std::clamp(count, 0, int(path.steps.size()));
	result.steps.insert(
		result.steps.end(),
		path.steps.begin(),
		path.steps.begin() + till);
	return result;
}

[[nodiscard]] int CommonPreparedEditBlockContainerSize(
		const PreparedEditBlockContainerPath &a,
		const PreparedEditBlockContainerPath &b) {
	const auto common = std::min(a.steps.size(), b.steps.size());
	for (auto i = size_t(); i != common; ++i) {
		if (ComparePreparedEditBlockContainerSteps(
				a.steps[i],
				b.steps[i]) != 0) {
			return int(i);
		}
	}
	return int(common);
}

[[nodiscard]] int LiftedPreparedEditBlockIndex(
		const PreparedEditBlockPath &path,
		int commonContainerSize) {
	if (commonContainerSize == int(path.container.steps.size())) {
		return path.index;
	} else if (commonContainerSize >= 0
		&& commonContainerSize < int(path.container.steps.size())) {
		return path.container.steps[commonContainerSize].blockIndex;
	}
	return -1;
}

[[nodiscard]] std::optional<LiftedPreparedEditBlocks>
LiftPreparedEditBlocksToCommonContainer(
		const PreparedEditBlockPath &a,
		const PreparedEditBlockPath &b) {
	if (!ValidPreparedEditBlockPath(a) || !ValidPreparedEditBlockPath(b)) {
		return std::nullopt;
	}
	const auto common = CommonPreparedEditBlockContainerSize(
		a.container,
		b.container);
	auto result = LiftedPreparedEditBlocks{
		.container = PreparedEditBlockContainerPrefix(a.container, common),
		.first = LiftedPreparedEditBlockIndex(a, common),
		.second = LiftedPreparedEditBlockIndex(b, common),
	};
	if (result.first < 0 || result.second < 0) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] auto ListItemSourcesFromBlockPath(
		const PreparedEditBlockPath &path)
-> std::vector<PreparedEditListItemSource> {
	auto result = std::vector<PreparedEditListItemSource>();
	for (auto i = int(path.container.steps.size()); i != 0; --i) {
		const auto stepIndex = i - 1;
		const auto &step = path.container.steps[stepIndex];
		if (step.kind != PreparedEditBlockContainerKind::ListItemChildren
			|| step.blockIndex < 0
			|| step.listItemIndex < 0) {
			continue;
		}
		result.push_back({
			.block = {
				.container = PreparedEditBlockContainerPrefix(
					path.container,
					stepIndex),
				.index = step.blockIndex,
			},
			.listItemIndex = step.listItemIndex,
		});
	}
	return result;
}

} // namespace

[[nodiscard]] bool IndexInRange(int index, int from, int till) {
	return (index >= from) && (index < till);
}

[[nodiscard]] bool PreparedPathInBlockRange(
		const PreparedBlockPath &path,
		const PreparedBlockRange &range) {
	if (path.container == range.container) {
		return IndexInRange(path.index, range.from, range.till);
	}
	if (!PreparedContainerHasPrefix(path.container, range.container)
		|| (path.container.steps.size() <= range.container.steps.size())) {
		return false;
	}
	const auto &step = path.container.steps[range.container.steps.size()];
	return IndexInRange(step.blockIndex, range.from, range.till);
}

[[nodiscard]] bool PreparedPathInListItemRange(
		const PreparedBlockPath &path,
		const PreparedListItemRange &range) {
	if (!PreparedContainerHasPrefix(path.container, range.block.container)
		|| (path.container.steps.size() <= range.block.container.steps.size())) {
		return false;
	}
	const auto &step = path.container.steps[range.block.container.steps.size()];
	return (step.kind == PreparedBlockContainerKind::ListItemChildren)
		&& (step.blockIndex == range.block.index)
		&& IndexInRange(step.listItemIndex, range.from, range.till);
}

[[nodiscard]] bool PreparedContainerNestedInSelection(
		const PreparedBlockContainerPath &container,
		const PreparedSelection &selection) {
	const auto marker = PreparedBlockPath{
		.container = container,
		.index = 0,
	};
	switch (selection.kind) {
	case PreparedSelectionKind::Blocks:
		return (container.steps.size() > selection.blocks.container.steps.size())
			&& PreparedPathInBlockRange(marker, selection.blocks);
	case PreparedSelectionKind::ListItems:
		return (container.steps.size()
			> selection.listItems.block.container.steps.size())
			&& PreparedPathInListItemRange(marker, selection.listItems);
	case PreparedSelectionKind::TableRows:
	case PreparedSelectionKind::TableCells:
	case PreparedSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool PreparedBlockPathInSelection(
		const PreparedBlockPath &path,
		const PreparedSelection &selection) {
	switch (selection.kind) {
	case PreparedSelectionKind::Blocks:
		return PreparedPathInBlockRange(path, selection.blocks);
	case PreparedSelectionKind::ListItems:
		return PreparedPathInListItemRange(path, selection.listItems);
	case PreparedSelectionKind::TableRows:
	case PreparedSelectionKind::TableCells:
	case PreparedSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] NormalizedIntegerRange NormalizeIntegerRange(int a, int b) {
	if (a < 0 || b < 0) {
		return {};
	}
	return {
		.from = std::min(a, b),
		.till = std::max(a, b) + 1,
	};
}

[[nodiscard]] PreparedEditSelection BlockSelectionFromIndexes(
		PreparedEditBlockContainerPath container,
		int first,
		int second) {
	const auto range = NormalizeIntegerRange(first, second);
	if (range.empty()) {
		return {};
	}
	return {
		.kind = PreparedEditSelectionKind::Blocks,
		.blocks = {
			.container = std::move(container),
			.from = range.from,
			.till = range.till,
		},
	};
}

[[nodiscard]] int ComparePreparedEditBlockContainerPaths(
		const PreparedEditBlockContainerPath &a,
		const PreparedEditBlockContainerPath &b) {
	const auto common = std::min(a.steps.size(), b.steps.size());
	for (auto i = size_t(); i != common; ++i) {
		if (const auto result = ComparePreparedEditBlockContainerSteps(
				a.steps[i],
				b.steps[i])) {
			return result;
		}
	}
	return CompareIntegers(int(a.steps.size()), int(b.steps.size()));
}

[[nodiscard]] bool SamePreparedEditBlockPath(
		const PreparedEditBlockPath &a,
		const PreparedEditBlockPath &b) {
	return (ComparePreparedEditBlockPaths(a, b) == 0);
}

[[nodiscard]] StructuralOwner StructuralOwnerFromHit(
		const PreparedEditHit &hit) {
	if (!hit.valid()) {
		return {};
	}
	switch (hit.kind) {
	case PreparedEditHitKind::Block:
		if (hit.block) {
			return StructuralOwnerFromBlock(*hit.block);
		}
		break;
	case PreparedEditHitKind::ListItem:
		if (hit.listItem) {
			return StructuralOwnerFromListItem(*hit.listItem);
		}
		break;
	case PreparedEditHitKind::TableRow:
		if (hit.tableRow) {
			return StructuralOwnerFromTableRow(*hit.tableRow);
		}
		break;
	case PreparedEditHitKind::TableCell:
		if (hit.tableCell) {
			return StructuralOwnerFromTableCell(*hit.tableCell);
		}
		break;
	case PreparedEditHitKind::Leaf:
		if (hit.leaf) {
			return StructuralOwnerFromLeaf(*hit.leaf);
		}
		break;
	case PreparedEditHitKind::None:
		break;
	}
	return hit.leaf ? StructuralOwnerFromLeaf(*hit.leaf) : StructuralOwner();
}

[[nodiscard]] std::optional<PreparedEditTableCellSource> TableCellFromOwner(
		const StructuralOwner &owner) {
	return owner.tableCell;
}

[[nodiscard]] PreparedEditTableCellRange TableRangeFromCell(
		const PreparedEditTableCellSource &source) {
	if (source.tableRowIndex < 0
		|| source.column < 0
		|| source.rowspan <= 0
		|| source.colspan <= 0) {
		return {};
	}
	return {
		.block = source.block,
		.rowFrom = source.tableRowIndex,
		.rowTill = source.tableRowIndex + source.rowspan,
		.columnFrom = source.column,
		.columnTill = source.column + source.colspan,
	};
}

[[nodiscard]] bool TableRangeContainsCell(
		const PreparedEditTableCellRange &range,
		const PreparedEditTableCellSource &source) {
	const auto cell = TableRangeFromCell(source);
	return SameTableRangeBlock(range, cell)
		&& (range.rowFrom <= cell.rowFrom)
		&& (range.rowTill >= cell.rowTill)
		&& (range.columnFrom <= cell.columnFrom)
		&& (range.columnTill >= cell.columnTill);
}

[[nodiscard]] PreparedEditTableCellRange TableRangesUnion(
		const PreparedEditTableCellRange &a,
		const PreparedEditTableCellRange &b) {
	if (!SameTableRangeBlock(a, b)) {
		return {};
	}
	return {
		.block = a.block,
		.rowFrom = std::min(a.rowFrom, b.rowFrom),
		.rowTill = std::max(a.rowTill, b.rowTill),
		.columnFrom = std::min(a.columnFrom, b.columnFrom),
		.columnTill = std::max(a.columnTill, b.columnTill),
	};
}

[[nodiscard]] std::optional<PreparedEditTableRowSource> TableRowFromOwner(
		const StructuralOwner &owner) {
	return owner.tableRow;
}

[[nodiscard]] std::optional<PreparedEditListItemSource> ListItemFromOwner(
		const StructuralOwner &owner) {
	return owner.listItem;
}

[[nodiscard]] std::optional<PreparedEditListItemSource> ListItemSourceFromLeaf(
		const PreparedEditLeafSource &source) {
	if (source.kind != PreparedEditLeafKind::ListItemText
		|| !ValidPreparedEditBlockPath(source.block)
		|| source.listItemIndex < 0) {
		return std::nullopt;
	}
	return PreparedEditListItemSource{
		.block = source.block,
		.listItemIndex = source.listItemIndex,
	};
}

[[nodiscard]] PreparedListItemRange ListRangeFromItem(
		const PreparedEditListItemSource &source) {
	if (!ValidPreparedEditBlockPath(source.block) || source.listItemIndex < 0) {
		return {};
	}
	return {
		.block = source.block,
		.from = source.listItemIndex,
		.till = source.listItemIndex + 1,
	};
}

[[nodiscard]] bool IsBlockOwner(const StructuralOwner &owner) {
	return (owner.kind == StructuralOwnerKind::Block);
}

[[nodiscard]] std::optional<PreparedEditBlockPath> BlockPathFromOwner(
		const StructuralOwner &owner) {
	if (owner.kind == StructuralOwnerKind::Block && owner.block) {
		return owner.block->path;
	} else if (owner.kind == StructuralOwnerKind::ListItem
		&& owner.listItem) {
		return owner.listItem->block;
	} else if (owner.kind == StructuralOwnerKind::TableRow
		&& owner.tableRow) {
		return owner.tableRow->block;
	} else if (owner.kind == StructuralOwnerKind::TableCell
		&& owner.tableCell) {
		return owner.tableCell->block;
	}
	return std::nullopt;
}

[[nodiscard]] PreparedEditSelection LiftedBlockSelection(
		const PreparedEditBlockPath &anchor,
		const PreparedEditBlockPath &focus) {
	const auto lifted = LiftPreparedEditBlocksToCommonContainer(anchor, focus);
	if (!lifted) {
		return {};
	}
	return BlockSelectionFromIndexes(
		lifted->container,
		lifted->first,
		lifted->second);
}

[[nodiscard]] auto ListItemSourcesFromOwner(
		const StructuralOwner &owner,
		const std::optional<PreparedEditBlockPath> &block)
-> std::vector<PreparedEditListItemSource> {
	auto result = std::vector<PreparedEditListItemSource>();
	if (const auto listItem = ListItemFromOwner(owner)) {
		result.push_back(*listItem);
	}
	if (!block) {
		return result;
	}
	for (const auto &source : ListItemSourcesFromBlockPath(*block)) {
		if (std::find(result.begin(), result.end(), source) == result.end()) {
			result.push_back(source);
		}
	}
	return result;
}

[[nodiscard]] auto ListContextSources(
		const std::optional<PreparedEditListItemSource> &source,
		const std::optional<PreparedEditBlockPath> &block)
-> std::vector<PreparedEditListItemSource> {
	auto result = std::vector<PreparedEditListItemSource>();
	if (source) {
		result.push_back(*source);
	}
	if (!block) {
		return result;
	}
	for (const auto &candidate : ListItemSourcesFromBlockPath(*block)) {
		if (std::find(result.begin(), result.end(), candidate) == result.end()) {
			result.push_back(candidate);
		}
	}
	return result;
}

[[nodiscard]] PreparedEditSelection ListItemSelectionFromSources(
		const std::vector<PreparedEditListItemSource> &anchorSources,
		const std::vector<PreparedEditListItemSource> &focusSources) {
	for (const auto &anchorListItem : anchorSources) {
		for (const auto &focusListItem : focusSources) {
			if (!SamePreparedEditBlockPath(
					anchorListItem.block,
					focusListItem.block)) {
				continue;
			}
			const auto range = NormalizeIntegerRange(
				anchorListItem.listItemIndex,
				focusListItem.listItemIndex);
			if (!range.empty()) {
				return {
					.kind = PreparedEditSelectionKind::ListItems,
					.listItems = {
						.block = anchorListItem.block,
						.from = range.from,
						.till = range.till,
					},
				};
			}
		}
	}
	return {};
}

[[nodiscard]] PreparedEditHit PreparedEditHitFromBlockSelection(
		const PreparedBlockRange &range,
		bool forward) {
	if (range.empty()) {
		return {};
	}
	const auto index = forward ? (range.till - 1) : range.from;
	if (index < 0) {
		return {};
	}
	return {
		.kind = PreparedEditHitKind::Block,
		.block = PreparedEditBlockSource{
			.path = {
				.container = range.container,
				.index = index,
			},
		},
	};
}

[[nodiscard]] PreparedEditHit PreparedEditHitFromListItemSelection(
		const PreparedListItemRange &range,
		bool forward) {
	if (range.empty()) {
		return {};
	}
	const auto index = forward ? (range.till - 1) : range.from;
	if (index < 0) {
		return {};
	}
	return {
		.kind = PreparedEditHitKind::ListItem,
		.listItem = PreparedEditListItemSource{
			.block = range.block,
			.listItemIndex = index,
		},
	};
}

[[nodiscard]] PreparedEditSelection SelectionFromStructuralOwner(
		const StructuralOwner &owner) {
	switch (owner.kind) {
	case StructuralOwnerKind::Block:
		return owner.block
			? BlockSelectionFromIndexes(
				owner.block->path.container,
				owner.block->path.index,
				owner.block->path.index)
			: PreparedEditSelection();
	case StructuralOwnerKind::ListItem:
		return owner.listItem
			? PreparedEditSelection{
				.kind = PreparedEditSelectionKind::ListItems,
				.listItems = {
					.block = owner.listItem->block,
					.from = owner.listItem->listItemIndex,
					.till = owner.listItem->listItemIndex + 1,
				},
			}
			: PreparedEditSelection();
	case StructuralOwnerKind::TableRow:
		return owner.tableRow
			? PreparedEditSelection{
				.kind = PreparedEditSelectionKind::TableRows,
				.tableRows = {
					.block = owner.tableRow->block,
					.from = owner.tableRow->tableRowIndex,
					.till = owner.tableRow->tableRowIndex + 1,
				},
			}
			: PreparedEditSelection();
	case StructuralOwnerKind::TableCell:
		return owner.tableCell
			? PreparedEditSelection{
				.kind = PreparedEditSelectionKind::TableCells,
				.tableCells = TableRangeFromCell(*owner.tableCell),
			}
			: PreparedEditSelection();
	case StructuralOwnerKind::None:
		break;
	}
	return {};
}

[[nodiscard]] PreparedEditSelection EdgeSelection(
		const PreparedEditSelection &selection,
		bool forward) {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		if (selection.blocks.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::Blocks,
			.blocks = {
				.container = selection.blocks.container,
				.from = forward
					? (selection.blocks.till - 1)
					: selection.blocks.from,
				.till = forward
					? selection.blocks.till
					: (selection.blocks.from + 1),
			},
		};
	case PreparedEditSelectionKind::ListItems:
		if (selection.listItems.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::ListItems,
			.listItems = {
				.block = selection.listItems.block,
				.from = forward
					? (selection.listItems.till - 1)
					: selection.listItems.from,
				.till = forward
					? selection.listItems.till
					: (selection.listItems.from + 1),
			},
		};
	case PreparedEditSelectionKind::TableRows:
		if (selection.tableRows.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::TableRows,
			.tableRows = {
				.block = selection.tableRows.block,
				.from = forward
					? (selection.tableRows.till - 1)
					: selection.tableRows.from,
				.till = forward
					? selection.tableRows.till
					: (selection.tableRows.from + 1),
			},
		};
	case PreparedEditSelectionKind::TableCells:
		if (selection.tableCells.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::TableCells,
			.tableCells = {
				.block = selection.tableCells.block,
				.rowFrom = forward
					? (selection.tableCells.rowTill - 1)
					: selection.tableCells.rowFrom,
				.rowTill = forward
					? selection.tableCells.rowTill
					: (selection.tableCells.rowFrom + 1),
				.columnFrom = forward
					? (selection.tableCells.columnTill - 1)
					: selection.tableCells.columnFrom,
				.columnTill = forward
					? selection.tableCells.columnTill
					: (selection.tableCells.columnFrom + 1),
			},
		};
	case PreparedEditSelectionKind::None:
		break;
	}
	return {};
}

[[nodiscard]] bool IsMultiListItemSelection(
		const PreparedEditSelection &selection) {
	return !selection.empty()
		&& (selection.kind == PreparedEditSelectionKind::ListItems)
		&& (selection.listItems.till > selection.listItems.from + 1);
}

} // namespace Iv::Editor
