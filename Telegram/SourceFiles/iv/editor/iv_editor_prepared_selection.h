/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_prepare.h"

#include <optional>
#include <vector>

namespace Iv::Editor {

struct NormalizedIntegerRange {
	int from = -1;
	int till = -1;

	[[nodiscard]] bool empty() const {
		return (from < 0) || (till <= from);
	}
};

enum class StructuralOwnerKind {
	None,
	Block,
	ListItem,
	TableRow,
	TableCell,
};

struct StructuralOwner {
	StructuralOwnerKind kind = StructuralOwnerKind::None;
	std::optional<Markdown::PreparedEditBlockSource> block;
	std::optional<Markdown::PreparedEditListItemSource> listItem;
	std::optional<Markdown::PreparedEditTableRowSource> tableRow;
	std::optional<Markdown::PreparedEditTableCellSource> tableCell;

	[[nodiscard]] bool valid() const {
		return (kind != StructuralOwnerKind::None);
	}
};

[[nodiscard]] bool IndexInRange(int index, int from, int till);

[[nodiscard]] bool PreparedPathInBlockRange(
	const Markdown::PreparedEditBlockPath &path,
	const Markdown::PreparedEditBlockRange &range);

[[nodiscard]] bool PreparedPathInListItemRange(
	const Markdown::PreparedEditBlockPath &path,
	const Markdown::PreparedEditListItemRange &range);

[[nodiscard]] bool PreparedContainerNestedInSelection(
	const Markdown::PreparedEditBlockContainerPath &container,
	const Markdown::PreparedEditSelection &selection);

[[nodiscard]] bool PreparedBlockPathInSelection(
	const Markdown::PreparedEditBlockPath &path,
	const Markdown::PreparedEditSelection &selection);

[[nodiscard]] NormalizedIntegerRange NormalizeIntegerRange(int a, int b);

[[nodiscard]] Markdown::PreparedEditSelection BlockSelectionFromIndexes(
	Markdown::PreparedEditBlockContainerPath container,
	int first,
	int second);

[[nodiscard]] int ComparePreparedEditBlockContainerPaths(
	const Markdown::PreparedEditBlockContainerPath &a,
	const Markdown::PreparedEditBlockContainerPath &b);

[[nodiscard]] bool SamePreparedEditBlockPath(
	const Markdown::PreparedEditBlockPath &a,
	const Markdown::PreparedEditBlockPath &b);

[[nodiscard]] StructuralOwner StructuralOwnerFromHit(
	const Markdown::PreparedEditHit &hit);

[[nodiscard]] auto TableCellFromOwner(
	const StructuralOwner &owner)
-> std::optional<Markdown::PreparedEditTableCellSource>;

[[nodiscard]] Markdown::PreparedEditTableCellRange TableRangeFromCell(
	const Markdown::PreparedEditTableCellSource &source);

[[nodiscard]] bool TableRangeContainsCell(
	const Markdown::PreparedEditTableCellRange &range,
	const Markdown::PreparedEditTableCellSource &source);

[[nodiscard]] Markdown::PreparedEditTableCellRange TableRangesUnion(
	const Markdown::PreparedEditTableCellRange &a,
	const Markdown::PreparedEditTableCellRange &b);

[[nodiscard]] auto TableRowFromOwner(
	const StructuralOwner &owner)
-> std::optional<Markdown::PreparedEditTableRowSource>;

[[nodiscard]] auto ListItemFromOwner(
	const StructuralOwner &owner)
-> std::optional<Markdown::PreparedEditListItemSource>;

[[nodiscard]] auto ListItemSourceFromLeaf(
	const Markdown::PreparedEditLeafSource &source)
-> std::optional<Markdown::PreparedEditListItemSource>;

[[nodiscard]] Markdown::PreparedEditListItemRange ListRangeFromItem(
	const Markdown::PreparedEditListItemSource &source);

[[nodiscard]] bool IsBlockOwner(const StructuralOwner &owner);

[[nodiscard]] auto BlockPathFromOwner(
	const StructuralOwner &owner)
-> std::optional<Markdown::PreparedEditBlockPath>;

[[nodiscard]] Markdown::PreparedEditSelection LiftedBlockSelection(
	const Markdown::PreparedEditBlockPath &anchor,
	const Markdown::PreparedEditBlockPath &focus);

[[nodiscard]] auto ListItemSourcesFromOwner(
	const StructuralOwner &owner,
	const std::optional<Markdown::PreparedEditBlockPath> &block)
-> std::vector<Markdown::PreparedEditListItemSource>;

[[nodiscard]] auto ListContextSources(
	const std::optional<Markdown::PreparedEditListItemSource> &source,
	const std::optional<Markdown::PreparedEditBlockPath> &block)
-> std::vector<Markdown::PreparedEditListItemSource>;

[[nodiscard]] Markdown::PreparedEditSelection ListItemSelectionFromSources(
	const std::vector<Markdown::PreparedEditListItemSource> &anchorSources,
	const std::vector<Markdown::PreparedEditListItemSource> &focusSources);

[[nodiscard]] Markdown::PreparedEditHit PreparedEditHitFromBlockSelection(
	const Markdown::PreparedEditBlockRange &range,
	bool forward);

[[nodiscard]] Markdown::PreparedEditHit PreparedEditHitFromListItemSelection(
	const Markdown::PreparedEditListItemRange &range,
	bool forward);

[[nodiscard]] Markdown::PreparedEditSelection SelectionFromStructuralOwner(
	const StructuralOwner &owner);

[[nodiscard]] Markdown::PreparedEditSelection EdgeSelection(
	const Markdown::PreparedEditSelection &selection,
	bool forward);

[[nodiscard]] bool IsMultiListItemSelection(
	const Markdown::PreparedEditSelection &selection);

} // namespace Iv::Editor
