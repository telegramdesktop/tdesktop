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

enum class BlockContainerKind : uchar {
	Root,
	BlockChildren,
	ListItemChildren,
};

struct BlockContainerStep {
	BlockContainerKind kind = BlockContainerKind::BlockChildren;
	int blockIndex = -1;
	int listItemIndex = -1;

	friend inline bool operator==(
			const BlockContainerStep &a,
			const BlockContainerStep &b) {
		return (a.kind == b.kind)
			&& (a.blockIndex == b.blockIndex)
			&& (a.listItemIndex == b.listItemIndex);
	}
};

struct BlockContainerPath {
	std::vector<BlockContainerStep> steps;

	friend inline bool operator==(
			const BlockContainerPath &a,
			const BlockContainerPath &b) {
		return (a.steps == b.steps);
	}
};

struct BlockPath {
	BlockContainerPath container;
	int index = -1;

	friend inline bool operator==(
			const BlockPath &a,
			const BlockPath &b) {
		return (a.container == b.container)
			&& (a.index == b.index);
	}
};

struct ReplaceTarget {
	BlockPath path;
	RichPage::BlockKind kind = RichPage::BlockKind::Unsupported;
	uint64 mediaId = 0;
	int itemIndex = -1;
};

enum class LeafKind : uchar {
	BlockText,
	BlockCaption,
	ListItemText,
	TableCellText,
	MathFormula,
};

struct LeafPath {
	LeafKind kind = LeafKind::BlockText;
	BlockPath block;
	int listItemIndex = -1;
	int tableRowIndex = -1;
	int tableCellIndex = -1;

	friend inline bool operator==(
			const LeafPath &a,
			const LeafPath &b) {
		return (a.kind == b.kind)
			&& (a.block == b.block)
			&& (a.listItemIndex == b.listItemIndex)
			&& (a.tableRowIndex == b.tableRowIndex)
			&& (a.tableCellIndex == b.tableCellIndex);
	}
};

[[nodiscard]] BlockContainerPath BlockChildrenContainer(BlockPath path);
[[nodiscard]] BlockContainerPath ListItemChildrenContainer(
	BlockPath path,
	int itemIndex);
[[nodiscard]] auto ToPreparedBlockContainerPath(
	const BlockContainerPath &path)
-> Markdown::PreparedEditBlockContainerPath;
[[nodiscard]] Markdown::PreparedEditBlockPath ToPreparedBlockPath(
	const BlockPath &path);
[[nodiscard]] BlockContainerPath ToStateBlockContainerPath(
	const Markdown::PreparedEditBlockContainerPath &path);
[[nodiscard]] BlockPath ToStateBlockPath(
	const Markdown::PreparedEditBlockPath &path);

} // namespace Iv::Editor
