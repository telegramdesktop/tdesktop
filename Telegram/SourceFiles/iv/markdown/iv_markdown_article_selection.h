/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "iv/iv_rich_page.h"

namespace Iv::Markdown {

enum class SelectableSegmentKind {
	TextLeaf,
	CodeBlock,
	DisplayMath,
	Table,
	Placeholder,
	Photo,
	Media,
};

struct SelectableSegment {
	SelectableSegmentKind kind = SelectableSegmentKind::TextLeaf;
	const Ui::Text::String *leaf = nullptr;
	const LaidOutBlock *block = nullptr;
	const LaidOutTableCell *cell = nullptr;
	QRect outerRect;
	QRect textRect;
	int textWidth = 0;
	style::align align = style::al_left;
	int index = -1;
	int length = 0;
	int tableSegmentIndex = -1;
	int parentSegmentIndex = -1;

	[[nodiscard]] bool isTextLeaf() const {
		return (leaf != nullptr);
	}
};

struct LogicalVisibleRange {
	int top = 0;
	int bottom = 0;
};

struct SegmentSpan {
	int from = 0;
	int till = 0;

	[[nodiscard]] bool empty() const {
		return (from >= till);
	}
};

struct PaintSearchSegmentRanges {
	std::vector<TextSelection> other;
	std::optional<TextSelection> current;

	[[nodiscard]] bool empty() const {
		return other.empty() && !current;
	}
};

void CollectSelectableSegments(
	std::vector<LaidOutBlock> *blocks,
	std::vector<SelectableSegment> *segments);
void RefreshScrollableSegmentRects(
	const std::vector<LaidOutBlock> &blocks,
	std::vector<SelectableSegment> *segments);
void RefreshScrollableSegmentRects(
	const LaidOutBlock &block,
	std::vector<SelectableSegment> *segments);
void CollectAnchors(
	const std::vector<LaidOutBlock> &blocks,
	std::vector<std::pair<QString, int>> *anchors);
[[nodiscard]] const SelectableSegment *FindSegment(
	const std::vector<SelectableSegment> *segments,
	int index);
[[nodiscard]] int SegmentLength(const SelectableSegment &segment);
[[nodiscard]] const style::TextStyle &TextStyleForSegment(
	const SelectableSegment &segment,
	const style::Markdown &st);
[[nodiscard]] style::color TextColorForSegment(
	const SelectableSegment &segment,
	const style::Markdown &st);
[[nodiscard]] std::optional<TextSelection> TextSelectionForSegment(
	const SelectableSegment &segment,
	const PaintSelectionState &selectionState);
[[nodiscard]] std::optional<TextSelection> TextSelectionForSegmentIndex(
	const PaintSelectionState &selectionState,
	int index);
[[nodiscard]] std::optional<TextSelection> PaintTextSelectionForSegment(
	const SelectableSegment &segment,
	const PaintSelectionState &selectionState);
[[nodiscard]] std::optional<TextSelection> PaintTextSelectionForSegmentIndex(
	const PaintSelectionState &selectionState,
	int index);
[[nodiscard]] PaintSearchSegmentRanges PaintSearchRangesForSegmentIndex(
	const PaintSelectionState &selectionState,
	const PaintSearchState &searchState,
	int index);
[[nodiscard]] bool WholeSegmentSelected(
	const SelectableSegment &segment,
	const PaintSelectionState &selectionState);
[[nodiscard]] bool WholeSegmentSelected(
	const PaintSelectionState &selectionState,
	int index);
[[nodiscard]] bool TableSegmentSelected(
	const PaintSelectionState &selectionState,
	int tableSegmentIndex);
[[nodiscard]] bool StructuralBlockSelected(
	const PaintSelectionState &selectionState,
	const PreparedEditBlockSource &source);
[[nodiscard]] bool StructuralListItemSelected(
	const PaintSelectionState &selectionState,
	const PreparedEditListItemSource &source);
[[nodiscard]] bool StructuralTableRowSelected(
	const PaintSelectionState &selectionState,
	const PreparedEditTableRowSource &source);
[[nodiscard]] bool StructuralTableCellSelected(
	const PaintSelectionState &selectionState,
	const PreparedEditTableCellSource &source);
[[nodiscard]] TextForMimeData TextForSegment(
	const SelectableSegment &segment,
	TextSelection selection = AllTextSelection);
[[nodiscard]] TextForMimeData TextForSelectedSegments(
	const std::vector<SelectableSegment> &segments,
	MarkdownArticleSelection selection,
	const MarkdownArticleSelectionEndpoints *endpoints,
	const PreparedEditSelection *structuralSelection = nullptr);
[[nodiscard]] std::vector<RichPage::Block> RichPageBlocksForSelectedSegments(
	const RichPage &page,
	const std::vector<SelectableSegment> &segments,
	MarkdownArticleSelection selection);

} // namespace Iv::Markdown
