/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_selection.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"

#include "styles/style_iv.h"

#include <algorithm>
#include <limits>

namespace Iv::Markdown {
namespace {

constexpr auto kCodeTabColumns = 4;
const auto kPhotoCopyLabel = u"Photo"_q;

[[nodiscard]] TextForMimeData CopyTextForMathSource(QString source) {
	const auto length = int(source.size());
	auto result = TextForMimeData::Simple(std::move(source));
	if (length > 0) {
		result.tags.push_back({
			.offset = 0,
			.length = length,
			.id = Ui::InputField::kTagIvMath,
		});
	}
	return result;
}

struct TableCopySlot {
	const LaidOutTableCell *cell = nullptr;
	bool origin = false;
};

[[nodiscard]] auto BuildTableCopyGrid(const LaidOutBlock &block)
-> std::vector<std::vector<TableCopySlot>> {
	const auto rowCount = int(block.tableRows.size());
	const auto columnCount = int(block.tableColumnWidths.size());
	auto result = std::vector<std::vector<TableCopySlot>>(
		std::max(rowCount, 0),
		std::vector<TableCopySlot>(std::max(columnCount, 0)));
	for (auto rowIndex = 0; rowIndex != rowCount; ++rowIndex) {
		for (const auto &cell : block.tableRows[rowIndex].cells) {
			const auto fromRow = std::clamp(rowIndex, 0, rowCount);
			const auto toRow = std::clamp(rowIndex + cell.rowspan, 0, rowCount);
			const auto fromColumn = std::clamp(cell.column, 0, columnCount);
			const auto toColumn = std::clamp(
				cell.column + cell.colspan,
				0,
				columnCount);
			for (auto currentRow = fromRow; currentRow != toRow; ++currentRow) {
				for (auto currentColumn = fromColumn;
					currentColumn != toColumn;
					++currentColumn) {
					auto &slot = result[currentRow][currentColumn];
					slot.cell = &cell;
					slot.origin = (currentRow == rowIndex)
						&& (currentColumn == cell.column);
				}
			}
		}
	}
	return result;
}

[[nodiscard]] QRect ClipRectHorizontally(QRect rect, QRect clip) {
	if (rect.isEmpty() || clip.isEmpty()) {
		return QRect();
	}
	const auto left = std::max(rect.x(), clip.x());
	const auto right = std::min(
		rect.x() + rect.width(),
		clip.x() + clip.width());
	return QRect(
		left,
		rect.y(),
		std::max(right - left, 0),
		rect.height());
}

[[nodiscard]] QRect VisibleTextRect(QRect textRect, QRect outerRect) {
	return ClipRectHorizontally(textRect, outerRect);
}

void RefreshBlockSegmentRect(
		const LaidOutBlock &block,
		SelectableSegment *segment) {
	if (!segment) {
		return;
	}
	switch (segment->kind) {
	case SelectableSegmentKind::DisplayMath:
		segment->outerRect = block.visibleFormulaRect;
		break;
	case SelectableSegmentKind::Table:
		segment->outerRect = block.visibleTableRect;
		break;
	case SelectableSegmentKind::Placeholder:
	case SelectableSegmentKind::Photo:
	case SelectableSegmentKind::Media:
		segment->outerRect = block.visibleMediaRect;
		break;
	case SelectableSegmentKind::CodeBlock:
		segment->outerRect = block.outer;
		segment->textRect = VisibleTextRect(
			block.textRect,
			block.contentRect.isEmpty() ? block.outer : block.contentRect);
		break;
	case SelectableSegmentKind::TextLeaf: {
		auto sourceRect = QRect();
		auto containerRect = block.outer;
		auto interactionRect = QRect();
		if (segment->leaf == &block.leaf) {
			sourceRect = block.textRect;
			if (block.kind == PreparedBlockKind::Details) {
				interactionRect = block.headerRect;
				containerRect = block.headerRect;
			} else if (block.kind == PreparedBlockKind::CodeBlock) {
				containerRect = block.contentRect;
			}
		} else if (segment->leaf == &block.labelLeaf) {
			sourceRect = block.labelRect;
			containerRect = block.headerRect.isEmpty()
				? block.outer
				: block.headerRect;
		} else if (segment->leaf == &block.subtitleLeaf) {
			sourceRect = block.subtitleRect;
			containerRect = block.headerRect.isEmpty()
				? block.outer
				: block.headerRect;
		} else if (segment->leaf == &block.actionLeaf) {
			sourceRect = block.actionRect;
		}
		const auto visibleRect = VisibleTextRect(sourceRect, containerRect);
		segment->outerRect = interactionRect.isEmpty()
			? visibleRect
			: interactionRect;
		segment->textRect = visibleRect;
	} break;
	}
}

[[nodiscard]] TextForMimeData CopyTextForDisplayMath(const LaidOutBlock &block) {
	return CopyTextForMathSource(block.copyText);
}

[[nodiscard]] TextForMimeData CopyTextForCodeBlock(
		const LaidOutBlock &block,
		TextSelection selection = AllTextSelection) {
	if (selection == AllTextSelection) {
		auto rich = block.codeText;
		if (!rich.text.isEmpty()) {
			rich.entities.push_back(EntityInText(
				EntityType::Pre,
				0,
				rich.text.size(),
				block.codeLanguage));
			SortEntities(&rich);
		}
		return TextForMimeData::Rich(std::move(rich));
	}
	auto from = 0;
	auto to = 0;
	auto displayPosition = 0;
	auto column = 0;
	auto found = false;
	const auto &text = block.codeText.text;
	for (auto i = 0, count = int(text.size()); i != count; ++i) {
		const auto ch = text[i];
		const auto width = (ch == QChar::Tabulation)
			? (kCodeTabColumns - (column % kCodeTabColumns))
			: 1;
		const auto nextDisplayPosition = displayPosition + width;
		if (selection.to <= displayPosition) {
			break;
		}
		if (selection.from < nextDisplayPosition
			&& selection.to > displayPosition) {
			if (!found) {
				from = i;
				found = true;
			}
			to = i + 1;
		}
		displayPosition = nextDisplayPosition;
		if (Ui::Text::IsNewline(ch)) {
			column = 0;
		} else {
			column += width;
		}
	}
	if (!found || to <= from) {
		return TextForMimeData();
	}
	auto rich = Ui::Text::Mid(block.codeText, from, to - from);
	if (!rich.text.isEmpty()) {
		rich.entities.push_back(EntityInText(
			EntityType::Pre,
			0,
			rich.text.size(),
			block.codeLanguage));
		SortEntities(&rich);
	}
	return TextForMimeData::Rich(std::move(rich));
}

[[nodiscard]] TextForMimeData CopyTextForTable(const LaidOutBlock &block) {
	const auto rowCount = int(block.tableRows.size());
	const auto columnCount = int(block.tableColumnWidths.size());
	auto result = TextForMimeData();
	const auto grid = BuildTableCopyGrid(block);
	if (!block.leaf.isEmpty()) {
		result.append(block.leaf.toTextForMimeData());
		if (rowCount > 0 && columnCount > 0) {
			result.append(u"\n"_q);
		}
	}
	for (auto rowIndex = 0; rowIndex != rowCount; ++rowIndex) {
		if (rowIndex) {
			result.append(u"\n"_q);
		}
		for (auto column = 0; column != columnCount; ++column) {
			if (column) {
				result.append(u"\t"_q);
			}
			const auto &slot = grid[rowIndex][column];
			if (slot.origin && slot.cell) {
				result.append(slot.cell->leaf.toTextForMimeData());
			}
		}
	}
	return result;
}

[[nodiscard]] TextForMimeData CopyTextForTableRange(
		const LaidOutBlock &block,
		int rowFrom,
		int rowTill,
		int columnFrom,
		int columnTill) {
	const auto rowCount = int(block.tableRows.size());
	const auto columnCount = int(block.tableColumnWidths.size());
	rowFrom = std::clamp(rowFrom, 0, rowCount);
	rowTill = std::clamp(rowTill, 0, rowCount);
	columnFrom = std::clamp(columnFrom, 0, columnCount);
	columnTill = std::clamp(columnTill, 0, columnCount);
	if (rowTill <= rowFrom || columnTill <= columnFrom) {
		return TextForMimeData();
	}
	const auto grid = BuildTableCopyGrid(block);
	auto result = TextForMimeData();
	auto emitted = std::vector<const LaidOutTableCell*>();
	for (auto rowIndex = rowFrom; rowIndex != rowTill; ++rowIndex) {
		if (rowIndex != rowFrom) {
			result.append(u"\n"_q);
		}
		for (auto column = columnFrom; column != columnTill; ++column) {
			if (column != columnFrom) {
				result.append(u"\t"_q);
			}
			const auto &slot = grid[rowIndex][column];
			if (slot.cell
				&& (std::find(
					emitted.begin(),
					emitted.end(),
					slot.cell) == emitted.end())) {
				result.append(slot.cell->leaf.toTextForMimeData());
				emitted.push_back(slot.cell);
			}
		}
	}
	return result;
}

[[nodiscard]] TextForMimeData CopyTextForMediaBlock(
		const QString &label,
		const Ui::Text::String &captionLeaf) {
	auto result = label.isEmpty()
		? TextForMimeData()
		: TextForMimeData::Simple(label);
	if (!captionLeaf.isEmpty()) {
		if (!result.empty()) {
			result.append(u"\n"_q);
		}
		result.append(captionLeaf.toTextForMimeData());
	}
	return result;
}

[[nodiscard]] TextForMimeData CopyTextForSingleMediaBlock(
		const LaidOutBlock &block,
		const QString &fallback = QString()) {
	const auto selectionData = block.mediaBlock
		? block.mediaBlock->selectionData()
		: MediaBlockSelectionData();
	const auto label = !selectionData.copyText.isEmpty()
		? selectionData.copyText
		: !block.copyText.isEmpty()
		? block.copyText
		: !block.labelText.isEmpty()
		? block.labelText
		: fallback;
	return CopyTextForMediaBlock(label, block.leaf);
}

[[nodiscard]] TextForMimeData CopyTextForPhotoBlock(const LaidOutBlock &block) {
	return CopyTextForSingleMediaBlock(block, kPhotoCopyLabel);
}

[[nodiscard]] TextForMimeData CopyTextForPlaceholderBlock(
		const LaidOutBlock &block) {
	return CopyTextForMediaBlock(
		block.labelText.isEmpty() ? block.copyText : block.labelText,
		block.leaf);
}

[[nodiscard]] int AddSelectableSegment(
		std::vector<SelectableSegment> *segments,
		SelectableSegment segment) {
	segment.index = int(segments->size());
	segment.length = std::max(segment.length, 0);
	segments->push_back(std::move(segment));
	return segment.index;
}

[[nodiscard]] int CompareSelectionPositions(
		MarkdownArticleSelectionPosition a,
		MarkdownArticleSelectionPosition b) {
	if (a.segment != b.segment) {
		return (a.segment < b.segment) ? -1 : 1;
	}
	if (a.offset != b.offset) {
		return (a.offset < b.offset) ? -1 : 1;
	}
	return 0;
}

[[nodiscard]] MarkdownArticleSelection NormalizeSelection(
		MarkdownArticleSelection selection) {
	if (selection.empty()) {
		return {};
	}
	if (CompareSelectionPositions(selection.from, selection.to) > 0) {
		std::swap(selection.from, selection.to);
	}
	return selection;
}

[[nodiscard]] int LastTableCellSegmentIndex(
		const std::vector<SelectableSegment> *segments,
		int tableSegmentIndex) {
	auto result = tableSegmentIndex;
	if (!segments) {
		return result;
	}
	for (const auto &segment : *segments) {
		if (segment.tableSegmentIndex == tableSegmentIndex) {
			result = std::max(result, segment.index);
		}
	}
	return result;
}

[[nodiscard]] std::optional<int> SingleTableCellSelection(
		const PaintSelectionState &selectionState,
		int tableSegmentIndex) {
	if (selectionState.empty()
		|| !selectionState.endpoints
		|| tableSegmentIndex < 0) {
		return std::nullopt;
	}
	const auto normalized = NormalizeSelection(selectionState.selection);
	if (normalized.empty()) {
		return std::nullopt;
	}
	const auto lastCellSegment = LastTableCellSegmentIndex(
		selectionState.segments,
		tableSegmentIndex);
	const auto spansWholeTable = (normalized.from.segment < tableSegmentIndex)
		&& (normalized.to.segment > lastCellSegment);
	auto tableHit = false;
	auto cellSegment = -1;
	auto multipleCells = false;
	const auto consider = [&](MarkdownArticleSelectionEndpoint endpoint) {
		if (!endpoint.valid() || !endpoint.direct) {
			return;
		}
		const auto segment = FindSegment(selectionState.segments, endpoint.segment);
		if (!segment) {
			return;
		}
		if (segment->index == tableSegmentIndex) {
			tableHit = true;
			return;
		}
		if (segment->tableSegmentIndex != tableSegmentIndex) {
			return;
		}
		if (cellSegment < 0) {
			cellSegment = segment->index;
		} else if (cellSegment != segment->index) {
			multipleCells = true;
		}
	};
	consider(selectionState.endpoints->from);
	consider(selectionState.endpoints->to);
	if (tableHit || multipleCells || cellSegment < 0 || spansWholeTable) {
		return std::nullopt;
	}
	return cellSegment;
}

[[nodiscard]] std::optional<TextSelection> BaseTextSelectionForSegment(
		const SelectableSegment &segment,
		MarkdownArticleSelection selection) {
	if (selection.empty() || !segment.isTextLeaf()) {
		return std::nullopt;
	}
	selection = NormalizeSelection(selection);
	if (selection.empty()
		|| selection.from.segment > segment.index
		|| selection.to.segment < segment.index) {
		return std::nullopt;
	}
	auto from = 0;
	auto to = SegmentLength(segment);
	if (selection.from.segment == segment.index) {
		from = selection.from.offset;
	}
	if (selection.to.segment == segment.index) {
		to = selection.to.offset;
	}
	from = std::clamp(from, 0, SegmentLength(segment));
	to = std::clamp(to, 0, SegmentLength(segment));
	if (from >= to) {
		return std::nullopt;
	}
	return TextSelection(uint16(from), uint16(to));
}

[[nodiscard]] bool RangeSelectsWholeSegment(
		const SelectableSegment &segment,
		MarkdownArticleSelection selection) {
	selection = NormalizeSelection(selection);
	if (selection.empty()
		|| selection.from.segment > segment.index
		|| selection.to.segment < segment.index) {
		return false;
	}
	auto from = 0;
	auto to = SegmentLength(segment);
	if (selection.from.segment == segment.index) {
		from = selection.from.offset;
	}
	if (selection.to.segment == segment.index) {
		to = selection.to.offset;
	}
	from = std::clamp(from, 0, SegmentLength(segment));
	to = std::clamp(to, 0, SegmentLength(segment));
	return (from < to);
}

struct RichPageSliceEndpoint {
	int top = -1;
	int listItem = -1;
	std::optional<PreparedEditLeafSource> trimLeaf;
};

[[nodiscard]] bool SegmentContributesToSelection(
		const SelectableSegment &segment,
		MarkdownArticleSelection selection) {
	if (segment.isTextLeaf()) {
		const auto text = BaseTextSelectionForSegment(segment, selection);
		return text && !text->empty();
	}
	return RangeSelectsWholeSegment(segment, selection);
}

[[nodiscard]] auto ResolveRichPageSliceEndpoint(
		const RichPage &page,
		const SelectableSegment &segment)
-> std::optional<RichPageSliceEndpoint> {
	if (!segment.block) {
		return std::nullopt;
	}
	auto leafSource = std::optional<PreparedEditLeafSource>();
	if (segment.cell && segment.cell->editLeaf) {
		leafSource = segment.cell->editLeaf;
	} else if (segment.block->editLeaf) {
		leafSource = segment.block->editLeaf;
	}
	auto path = std::optional<PreparedEditBlockPath>();
	if (leafSource) {
		path = leafSource->block;
	} else if (segment.block->editListItem) {
		path = segment.block->editListItem->block;
	} else if (segment.block->editBlock) {
		path = segment.block->editBlock->path;
	}
	if (!path) {
		return std::nullopt;
	}
	const auto &steps = path->container.steps;
	auto first = 0;
	while (first < int(steps.size())
		&& steps[first].kind == PreparedEditBlockContainerKind::Root) {
		++first;
	}
	const auto stepsEmpty = (first >= int(steps.size()));
	auto result = RichPageSliceEndpoint();
	if (stepsEmpty) {
		result.top = path->index;
		if (leafSource
			&& leafSource->kind == PreparedEditLeafKind::ListItemText) {
			result.listItem = leafSource->listItemIndex;
		}
	} else {
		result.top = steps[first].blockIndex;
		if (steps[first].kind
			== PreparedEditBlockContainerKind::ListItemChildren) {
			result.listItem = steps[first].listItemIndex;
		}
	}
	if (result.top < 0 || result.top >= int(page.blocks.size())) {
		return std::nullopt;
	}
	if (stepsEmpty
		&& leafSource
		&& segment.kind == SelectableSegmentKind::TextLeaf
		&& (leafSource->kind == PreparedEditLeafKind::BlockText
			|| leafSource->kind == PreparedEditLeafKind::BlockCaption
			|| leafSource->kind == PreparedEditLeafKind::ListItemText)) {
		result.trimLeaf = *leafSource;
	}
	return result;
}

[[nodiscard]] TextWithEntities *RichPageSliceTrimTarget(
		RichPage::Block *block,
		const PreparedEditLeafSource &source) {
	if (source.kind == PreparedEditLeafKind::BlockText) {
		return &block->text.text;
	} else if (source.kind == PreparedEditLeafKind::BlockCaption) {
		return &block->caption.text;
	} else if (source.kind == PreparedEditLeafKind::ListItemText) {
		if (source.listItemIndex < 0
			|| source.listItemIndex >= int(block->listItems.size())) {
			return nullptr;
		}
		return &block->listItems[source.listItemIndex].text.text;
	}
	return nullptr;
}

void ApplyRichPageSliceStartTrim(TextWithEntities *target, int offset) {
	const auto from = std::clamp(offset, 0, int(target->text.size()));
	if (from > 0) {
		*target = Ui::Text::Mid(*target, from);
	}
}

void ApplyRichPageSliceEndTrim(TextWithEntities *target, int offset) {
	const auto to = std::clamp(offset, 0, int(target->text.size()));
	if (to < int(target->text.size())) {
		*target = Ui::Text::Mid(*target, 0, to);
	}
}

[[nodiscard]] bool RichPageSliceEdgeEmpty(
		const RichPage::Block &block,
		bool trimmed) {
	if (block.kind == RichPage::BlockKind::List) {
		return block.listItems.empty();
	}
	const auto flow = (block.kind == RichPage::BlockKind::Paragraph)
		|| (block.kind == RichPage::BlockKind::Heading)
		|| (block.kind == RichPage::BlockKind::Footer);
	return flow && trimmed && block.text.text.text.isEmpty();
}

[[nodiscard]] bool IndexInRange(int index, int from, int till) {
	return (index >= from) && (index < till);
}

[[nodiscard]] bool RangesIntersect(
		int firstFrom,
		int firstTill,
		int secondFrom,
		int secondTill) {
	return (firstFrom < secondTill) && (secondFrom < firstTill);
}

[[nodiscard]] const PreparedEditSelection *StructuralSelection(
		const PaintSelectionState &selectionState,
		PreparedEditSelectionKind kind) {
	if (!selectionState.hasStructuralSelection()) {
		return nullptr;
	}
	const auto selection = selectionState.structuralSelection;
	return (selection->kind == kind) ? selection : nullptr;
}

[[nodiscard]] bool ContainerHasPrefix(
		const PreparedEditBlockContainerPath &path,
		const PreparedEditBlockContainerPath &prefix) {
	if (path.steps.size() < prefix.steps.size()) {
		return false;
	}
	return std::equal(
		prefix.steps.begin(),
		prefix.steps.end(),
		path.steps.begin());
}

[[nodiscard]] bool PathInBlockRange(
		const PreparedEditBlockPath &path,
		const PreparedEditBlockRange &range) {
	if (path.container == range.container) {
		return IndexInRange(path.index, range.from, range.till);
	}
	if (!ContainerHasPrefix(path.container, range.container)
		|| (path.container.steps.size() <= range.container.steps.size())) {
		return false;
	}
	const auto &step = path.container.steps[range.container.steps.size()];
	return IndexInRange(step.blockIndex, range.from, range.till);
}

[[nodiscard]] bool PathInListItemRange(
		const PreparedEditBlockPath &path,
		const PreparedEditListItemRange &range) {
	if (!ContainerHasPrefix(path.container, range.block.container)
		|| (path.container.steps.size() <= range.block.container.steps.size())) {
		return false;
	}
	const auto &step = path.container.steps[range.block.container.steps.size()];
	return (step.kind == PreparedEditBlockContainerKind::ListItemChildren)
		&& (step.blockIndex == range.block.index)
		&& IndexInRange(step.listItemIndex, range.from, range.till);
}

[[nodiscard]] bool BlockSourceInStructuralSelection(
		const PreparedEditBlockSource &source,
		const PreparedEditSelection &selection) {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		return PathInBlockRange(source.path, selection.blocks);
	case PreparedEditSelectionKind::ListItems:
		return PathInListItemRange(source.path, selection.listItems);
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool ListItemSourceInStructuralSelection(
		const PreparedEditListItemSource &source,
		const PreparedEditSelection &selection) {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		return PathInBlockRange(source.block, selection.blocks);
	case PreparedEditSelectionKind::ListItems:
		return (source.block == selection.listItems.block)
			&& IndexInRange(
				source.listItemIndex,
				selection.listItems.from,
				selection.listItems.till);
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool BlockBelongsToStructuralSelection(
		const LaidOutBlock &block,
		const PreparedEditSelection &selection) {
	return (block.editListItem
		&& ListItemSourceInStructuralSelection(*block.editListItem, selection))
		|| (block.editBlock
			&& BlockSourceInStructuralSelection(*block.editBlock, selection));
}

[[nodiscard]] bool WholeSegmentStructurallySelected(
		const SelectableSegment &segment,
		const PaintSelectionState &selectionState) {
	if (!selectionState.hasStructuralSelection() || !segment.block) {
		return false;
	}
	switch (segment.kind) {
	case SelectableSegmentKind::CodeBlock:
	case SelectableSegmentKind::DisplayMath:
	case SelectableSegmentKind::Table:
	case SelectableSegmentKind::Placeholder:
	case SelectableSegmentKind::Photo:
	case SelectableSegmentKind::Media:
		return BlockBelongsToStructuralSelection(
			*segment.block,
			*selectionState.structuralSelection);
	case SelectableSegmentKind::TextLeaf:
		return false;
	}
	return false;
}

[[nodiscard]] std::optional<TextSelection> StructuralTextSelectionForSegment(
		const SelectableSegment &segment,
		const PaintSelectionState &selectionState) {
	if (!selectionState.hasStructuralSelection()
		|| !segment.isTextLeaf()
		|| !segment.block) {
		return std::nullopt;
	}
	switch (selectionState.structuralSelection->kind) {
	case PreparedEditSelectionKind::Blocks:
	case PreparedEditSelectionKind::ListItems:
		return BlockBelongsToStructuralSelection(
			*segment.block,
			*selectionState.structuralSelection)
			? std::make_optional(AllTextSelection)
			: std::nullopt;
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<TextSelection> ResolveTextSelectionForSegment(
		const SelectableSegment &segment,
		const PaintSelectionState &selectionState,
		bool suppressStructuralSelection) {
	if (!selectionState.segments) {
		return std::nullopt;
	}
	if (segment.tableSegmentIndex >= 0) {
		if (const auto singleCell = SingleTableCellSelection(
				selectionState,
				segment.tableSegmentIndex);
			singleCell && *singleCell != segment.index) {
			return std::nullopt;
		}
	}
	if (segment.tableSegmentIndex >= 0
		&& TableSegmentSelected(selectionState, segment.tableSegmentIndex)) {
		return std::nullopt;
	}
	if (const auto structural = StructuralTextSelectionForSegment(
			segment,
			selectionState)) {
		return suppressStructuralSelection ? std::nullopt : structural;
	}
	return BaseTextSelectionForSegment(segment, selectionState.selection);
}

[[nodiscard]] const LaidOutBlock *FindTableBlock(
		const std::vector<SelectableSegment> &segments,
		const PreparedEditBlockPath &path) {
	for (const auto &segment : segments) {
		if (!segment.block
			|| (segment.block->kind != PreparedBlockKind::Table)
			|| !segment.block->editBlock
			|| (segment.block->editBlock->path != path)) {
			continue;
		}
		return segment.block;
	}
	return nullptr;
}

[[nodiscard]] TextForMimeData TextForStructuralTableSelection(
		const std::vector<SelectableSegment> &segments,
		const PreparedEditSelection &selection) {
	switch (selection.kind) {
	case PreparedEditSelectionKind::TableRows: {
		const auto block = FindTableBlock(segments, selection.tableRows.block);
		return block
			? CopyTextForTableRange(
				*block,
				selection.tableRows.from,
				selection.tableRows.till,
				0,
				int(block->tableColumnWidths.size()))
			: TextForMimeData();
	}
	case PreparedEditSelectionKind::TableCells: {
		const auto block = FindTableBlock(segments, selection.tableCells.block);
		return block
			? CopyTextForTableRange(
				*block,
				selection.tableCells.rowFrom,
				selection.tableCells.rowTill,
				selection.tableCells.columnFrom,
				selection.tableCells.columnTill)
			: TextForMimeData();
	}
	case PreparedEditSelectionKind::Blocks:
	case PreparedEditSelectionKind::ListItems:
	case PreparedEditSelectionKind::None:
		return TextForMimeData();
	}
	return TextForMimeData();
}

[[nodiscard]] const style::TextStyle &HeadingTextStyle(
		int level,
		const style::Markdown &st) {
	switch (std::clamp(level, 1, 6)) {
	case 1: return st.heading1;
	case 2: return st.heading2;
	case 3: return st.heading3;
	case 4: return st.heading4;
	case 5: return st.heading5;
	case 6: return st.heading6;
	}
	return st.heading6;
}

[[nodiscard]] bool IsEmbedPostAuthorSegment(
		const SelectableSegment &segment) {
	return segment.block
		&& (segment.block->kind == PreparedBlockKind::EmbedPost)
		&& (segment.leaf == &segment.block->labelLeaf);
}

[[nodiscard]] bool IsEmbedPostDateSegment(
		const SelectableSegment &segment) {
	return segment.block
		&& (segment.block->kind == PreparedBlockKind::EmbedPost)
		&& (segment.leaf == &segment.block->subtitleLeaf);
}

} // namespace

void CollectSelectableSegments(
		std::vector<LaidOutBlock> *blocks,
		std::vector<SelectableSegment> *segments) {
	if (!blocks) {
		return;
	}
	for (auto &block : *blocks) {
		block.segmentIndex = -1;
		block.secondarySegmentIndex = -1;
		block.tertiarySegmentIndex = -1;
		switch (block.kind) {
		case PreparedBlockKind::Paragraph:
		case PreparedBlockKind::Thinking:
		case PreparedBlockKind::Heading:
		case PreparedBlockKind::Details: {
			if (block.leaf.isEmpty() && block.textRect.isEmpty()) {
				break;
			}
			auto segment = SelectableSegment();
			segment.kind = SelectableSegmentKind::TextLeaf;
			segment.leaf = &block.leaf;
			segment.block = &block;
			segment.outerRect = (block.kind == PreparedBlockKind::Details)
				? block.headerRect
				: block.textRect;
			segment.textRect = block.textRect;
			segment.textWidth = block.textWidth;
			segment.align = block.flowTextAlign;
			segment.length = block.leaf.length();
			block.segmentIndex = AddSelectableSegment(
				segments,
				std::move(segment));
		} break;
		case PreparedBlockKind::CodeBlock: {
			auto segment = SelectableSegment();
			segment.kind = SelectableSegmentKind::CodeBlock;
			segment.leaf = &block.leaf;
			segment.block = &block;
			segment.outerRect = block.outer;
			segment.textRect = block.textRect;
			segment.textWidth = block.textWidth;
			segment.length = block.leaf.length();
			block.segmentIndex = AddSelectableSegment(
				segments,
				std::move(segment));
		} break;
		case PreparedBlockKind::DisplayMath: {
			auto segment = SelectableSegment();
			segment.kind = SelectableSegmentKind::DisplayMath;
			segment.block = &block;
			segment.outerRect = block.visibleFormulaRect;
			segment.align = block.formulaAlign;
			segment.length = 1;
			block.segmentIndex = AddSelectableSegment(
				segments,
				std::move(segment));
		} break;
		case PreparedBlockKind::Table: {
			if (!block.textRect.isEmpty()) {
				auto textSegment = SelectableSegment();
				textSegment.kind = SelectableSegmentKind::TextLeaf;
				textSegment.leaf = &block.leaf;
				textSegment.block = &block;
				textSegment.outerRect = block.textRect;
				textSegment.textRect = block.textRect;
				textSegment.textWidth = block.textWidth;
				textSegment.align = block.flowTextAlign;
				textSegment.length = block.leaf.length();
				block.secondarySegmentIndex = AddSelectableSegment(
					segments,
					std::move(textSegment));
			}
			auto segment = SelectableSegment();
			segment.kind = SelectableSegmentKind::Table;
			segment.block = &block;
			segment.outerRect = block.visibleTableRect;
			segment.length = 1;
			block.segmentIndex = AddSelectableSegment(
				segments,
				std::move(segment));
			if (block.secondarySegmentIndex >= 0) {
				(*segments)[block.secondarySegmentIndex].parentSegmentIndex
					= block.segmentIndex;
			}
			for (auto &row : block.tableRows) {
				for (auto &cell : row.cells) {
					auto cellSegment = SelectableSegment();
					cellSegment.kind = SelectableSegmentKind::TextLeaf;
					cellSegment.leaf = &cell.leaf;
					cellSegment.block = &block;
					cellSegment.cell = &cell;
					cellSegment.outerRect = cell.outer;
					cellSegment.textRect = cell.textRect;
					cellSegment.textWidth = cell.textWidth;
					cellSegment.align = cell.align;
					cellSegment.length = cell.leaf.length();
					cellSegment.tableSegmentIndex = block.segmentIndex;
					cell.tableSegmentIndex = block.segmentIndex;
					cell.segmentIndex = AddSelectableSegment(
						segments,
						std::move(cellSegment));
				}
			}
		} break;
		case PreparedBlockKind::RelatedArticle:
		case PreparedBlockKind::Placeholder:
		case PreparedBlockKind::Photo:
		case PreparedBlockKind::Video:
		case PreparedBlockKind::Audio:
		case PreparedBlockKind::Map:
		case PreparedBlockKind::Channel:
		case PreparedBlockKind::GroupedMedia: {
			auto segment = SelectableSegment();
			segment.kind = (block.kind == PreparedBlockKind::Photo)
				? SelectableSegmentKind::Photo
				: (block.kind == PreparedBlockKind::Placeholder)
				? SelectableSegmentKind::Placeholder
				: SelectableSegmentKind::Media;
			segment.block = &block;
			segment.outerRect = block.mediaRect;
			segment.length = 1;
			block.segmentIndex = AddSelectableSegment(
				segments,
				std::move(segment));
			if (!block.textRect.isEmpty()) {
				auto textSegment = SelectableSegment();
				textSegment.kind = SelectableSegmentKind::TextLeaf;
				textSegment.leaf = &block.leaf;
				textSegment.block = &block;
				textSegment.outerRect = block.textRect;
				textSegment.textRect = block.textRect;
				textSegment.textWidth = block.textWidth;
				textSegment.length = block.leaf.length();
				textSegment.parentSegmentIndex = block.segmentIndex;
				block.secondarySegmentIndex = AddSelectableSegment(
					segments,
					std::move(textSegment));
			}
		} break;
		case PreparedBlockKind::EmbedPost: {
			if (!block.labelRect.isEmpty() && !block.labelLeaf.isEmpty()) {
				auto authorSegment = SelectableSegment();
				authorSegment.kind = SelectableSegmentKind::TextLeaf;
				authorSegment.leaf = &block.labelLeaf;
				authorSegment.block = &block;
				authorSegment.outerRect = block.labelRect;
				authorSegment.textRect = block.labelRect;
				authorSegment.textWidth = block.labelWidth;
				authorSegment.length = block.labelLeaf.length();
				block.segmentIndex = AddSelectableSegment(
					segments,
					std::move(authorSegment));
			}
			if (!block.subtitleRect.isEmpty() && !block.subtitleLeaf.isEmpty()) {
				auto dateSegment = SelectableSegment();
				dateSegment.kind = SelectableSegmentKind::TextLeaf;
				dateSegment.leaf = &block.subtitleLeaf;
				dateSegment.block = &block;
				dateSegment.outerRect = block.subtitleRect;
				dateSegment.textRect = block.subtitleRect;
				dateSegment.textWidth = block.subtitleWidth;
				dateSegment.length = block.subtitleLeaf.length();
				block.secondarySegmentIndex = AddSelectableSegment(
					segments,
					std::move(dateSegment));
			}
			CollectSelectableSegments(&block.children, segments);
			if (!block.textRect.isEmpty() && !block.leaf.isEmpty()) {
				auto captionSegment = SelectableSegment();
				captionSegment.kind = SelectableSegmentKind::TextLeaf;
				captionSegment.leaf = &block.leaf;
				captionSegment.block = &block;
				captionSegment.outerRect = block.textRect;
				captionSegment.textRect = block.textRect;
				captionSegment.textWidth = block.textWidth;
				captionSegment.length = block.leaf.length();
				block.tertiarySegmentIndex = AddSelectableSegment(
					segments,
					std::move(captionSegment));
			}
			continue;
		}
		case PreparedBlockKind::List:
		case PreparedBlockKind::ListItem:
		case PreparedBlockKind::Quote:
		case PreparedBlockKind::Rule:
			break;
		}
		CollectSelectableSegments(&block.children, segments);
	}
}

void RefreshScrollableSegmentRects(
		const std::vector<LaidOutBlock> &blocks,
		std::vector<SelectableSegment> *segments) {
	if (!segments) {
		return;
	}
	for (const auto &block : blocks) {
		RefreshScrollableSegmentRects(block, segments);
	}
}

void RefreshScrollableSegmentRects(
		const LaidOutBlock &block,
		std::vector<SelectableSegment> *segments) {
	if (!segments) {
		return;
	}
	const auto refreshIndex = [&](int index) {
		if (index < 0 || index >= int(segments->size())) {
			return;
		}
		RefreshBlockSegmentRect(block, &(*segments)[index]);
	};
	refreshIndex(block.segmentIndex);
	refreshIndex(block.secondarySegmentIndex);
	refreshIndex(block.tertiarySegmentIndex);
	if (block.kind == PreparedBlockKind::Table) {
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				if (cell.segmentIndex < 0
					|| cell.segmentIndex >= int(segments->size())) {
					continue;
				}
				auto &segment = (*segments)[cell.segmentIndex];
				segment.outerRect = cell.outer;
				segment.textRect = VisibleTextRect(
					cell.textRect,
					cell.outer);
			}
		}
	}
	RefreshScrollableSegmentRects(block.children, segments);
}

void CollectAnchors(
		const std::vector<LaidOutBlock> &blocks,
		std::vector<std::pair<QString, int>> *anchors) {
	if (!anchors) {
		return;
	}
	for (const auto &block : blocks) {
		if (!block.anchorId.isEmpty() || !block.anchorIds.empty()) {
			const auto top = !block.textRect.isEmpty()
				? block.textRect.top()
				: [&] {
					for (const auto &child : block.children) {
						if (child.outer.height() > 0) {
							return child.outer.top();
						}
					}
					return block.outer.top();
				}();
			if (!block.anchorId.isEmpty()) {
				anchors->push_back({ block.anchorId, top });
			}
			for (const auto &anchorId : block.anchorIds) {
				anchors->push_back({ anchorId, top });
			}
		}
		CollectAnchors(block.children, anchors);
	}
}

const SelectableSegment *FindSegment(
		const std::vector<SelectableSegment> *segments,
		int index) {
	if (!segments || index < 0 || index >= int(segments->size())) {
		return nullptr;
	}
	return &(*segments)[index];
}

int SegmentLength(const SelectableSegment &segment) {
	return std::max(segment.length, 0);
}

const style::TextStyle &TextStyleForSegment(
		const SelectableSegment &segment,
		const style::Markdown &st) {
	if (segment.cell) {
		return segment.cell->header
			? st.table.headerStyle
			: st.table.bodyStyle;
	} else if (IsEmbedPostAuthorSegment(segment)) {
		return st.embedPost.authorStyle;
	} else if (IsEmbedPostDateSegment(segment)) {
		return st.embedPost.dateStyle;
	} else if (segment.kind == SelectableSegmentKind::CodeBlock) {
		return st.code;
	} else if (segment.block && segment.block->quoteAuthor) {
		return st.quoteAuthorStyle;
	} else if (!segment.block) {
		return st.body;
	}
	switch (segment.block->kind) {
	case PreparedBlockKind::Details:
		return st.details.summaryStyle;
	case PreparedBlockKind::CodeBlock:
		return st.code;
	case PreparedBlockKind::Heading:
		return HeadingTextStyle(segment.block->headingLevel, st);
	default:
		return st.body;
	}
}

style::color TextColorForSegment(
		const SelectableSegment &segment,
		const style::Markdown &st) {
	if (IsEmbedPostAuthorSegment(segment)) {
		return st.embedPost.authorFg;
	} else if (IsEmbedPostDateSegment(segment)) {
		return st.embedPost.dateFg;
	} else if (segment.block
		&& (segment.block->kind == PreparedBlockKind::Thinking)) {
		return st.supplementaryTextColor;
	} else if (segment.kind == SelectableSegmentKind::CodeBlock
		|| (segment.block
			&& segment.block->kind == PreparedBlockKind::CodeBlock)) {
		return st.textPalette.monoFg;
	} else if (segment.block && segment.block->supplementary) {
		return st.supplementaryTextColor;
	}
	return st.textColor;
}

bool TableSegmentSelected(
		const PaintSelectionState &selectionState,
		int tableSegmentIndex) {
	if (!selectionState.segments || tableSegmentIndex < 0) {
		return false;
	}
	if (selectionState.hasStructuralSelection()) {
		if (const auto segment = FindSegment(
				selectionState.segments,
				tableSegmentIndex);
			segment && WholeSegmentStructurallySelected(
				*segment,
				selectionState)) {
			return true;
		}
	}
	if (SingleTableCellSelection(selectionState, tableSegmentIndex)) {
		return false;
	}
	const auto normalized = NormalizeSelection(selectionState.selection);
	if (normalized.empty()) {
		return false;
	}
	auto selectedCells = 0;
	auto selectedCellIndex = -1;
	for (const auto &segment : *selectionState.segments) {
		if (segment.tableSegmentIndex != tableSegmentIndex
			|| segment.index == tableSegmentIndex) {
			continue;
		}
		const auto textSelection = BaseTextSelectionForSegment(
			segment,
			normalized);
		if (!textSelection || textSelection->empty()) {
			continue;
		}
		if (++selectedCells == 1) {
			selectedCellIndex = segment.index;
		} else {
			return true;
		}
	}
	const auto table = FindSegment(selectionState.segments, tableSegmentIndex);
	if (!table || !RangeSelectsWholeSegment(*table, normalized)) {
		return false;
	}
	if (selectedCells != 1) {
		return true;
	}
	if (normalized.from.segment == tableSegmentIndex
		|| normalized.to.segment == tableSegmentIndex) {
		return true;
	}
	const auto lower = std::min(tableSegmentIndex, selectedCellIndex);
	const auto upper = std::max(tableSegmentIndex, selectedCellIndex);
	return (normalized.from.segment < lower)
		&& (normalized.to.segment > upper);
}

bool StructuralBlockSelected(
		const PaintSelectionState &selectionState,
		const PreparedEditBlockSource &source) {
	const auto selection = StructuralSelection(
		selectionState,
		PreparedEditSelectionKind::Blocks);
	if (!selection) {
		return false;
	}
	const auto &range = selection->blocks;
	return (source.path.container == range.container)
		&& IndexInRange(source.path.index, range.from, range.till);
}

bool StructuralListItemSelected(
		const PaintSelectionState &selectionState,
		const PreparedEditListItemSource &source) {
	const auto selection = StructuralSelection(
		selectionState,
		PreparedEditSelectionKind::ListItems);
	if (!selection) {
		return false;
	}
	const auto &range = selection->listItems;
	return (source.block == range.block)
		&& IndexInRange(source.listItemIndex, range.from, range.till);
}

bool StructuralTableRowSelected(
		const PaintSelectionState &selectionState,
		const PreparedEditTableRowSource &source) {
	const auto selection = StructuralSelection(
		selectionState,
		PreparedEditSelectionKind::TableRows);
	if (!selection) {
		return false;
	}
	const auto &range = selection->tableRows;
	return (source.block == range.block)
		&& IndexInRange(source.tableRowIndex, range.from, range.till);
}

bool StructuralTableCellSelected(
		const PaintSelectionState &selectionState,
		const PreparedEditTableCellSource &source) {
	const auto selection = StructuralSelection(
		selectionState,
		PreparedEditSelectionKind::TableCells);
	if (!selection) {
		return false;
	}
	const auto &range = selection->tableCells;
	if (range.empty()
		|| source.tableRowIndex < 0
		|| source.column < 0
		|| source.colspan <= 0
		|| source.rowspan <= 0) {
		return false;
	}
	return (source.block == range.block)
		&& RangesIntersect(
			source.tableRowIndex,
			source.tableRowIndex + source.rowspan,
			range.rowFrom,
			range.rowTill)
		&& RangesIntersect(
			source.column,
			source.column + source.colspan,
			range.columnFrom,
			range.columnTill);
}

std::optional<TextSelection> TextSelectionForSegment(
		const SelectableSegment &segment,
		const PaintSelectionState &selectionState) {
	return ResolveTextSelectionForSegment(
		segment,
		selectionState,
		false);
}

std::optional<TextSelection> TextSelectionForSegmentIndex(
		const PaintSelectionState &selectionState,
		int index) {
	const auto segment = FindSegment(selectionState.segments, index);
	return segment
		? TextSelectionForSegment(*segment, selectionState)
		: std::nullopt;
}

std::optional<TextSelection> PaintTextSelectionForSegment(
		const SelectableSegment &segment,
		const PaintSelectionState &selectionState) {
	return ResolveTextSelectionForSegment(
		segment,
		selectionState,
		true);
}

std::optional<TextSelection> PaintTextSelectionForSegmentIndex(
		const PaintSelectionState &selectionState,
		int index) {
	const auto segment = FindSegment(selectionState.segments, index);
	return segment
		? PaintTextSelectionForSegment(*segment, selectionState)
		: std::nullopt;
}

PaintSearchSegmentRanges PaintSearchRangesForSegmentIndex(
		const PaintSelectionState &selectionState,
		const PaintSearchState &searchState,
		int index) {
	auto result = PaintSearchSegmentRanges();
	if (searchState.empty() || index < 0) {
		return result;
	}
	const auto segment = FindSegment(selectionState.segments, index);
	if (!segment || !segment->isTextLeaf()) {
		return result;
	}
	const auto length = std::min(
		SegmentLength(*segment),
		int(std::numeric_limits<uint16>::max()));
	const auto &matches = *searchState.matches;
	for (auto i = 0; i != int(matches.size()); ++i) {
		const auto &match = matches[i];
		if (match.segment > index) {
			break;
		} else if (match.segment != index) {
			continue;
		}
		const auto from = std::clamp(match.from, 0, length);
		const auto to = std::clamp(match.to, 0, length);
		if (from >= to) {
			continue;
		}
		const auto range = TextSelection(uint16(from), uint16(to));
		if (i == searchState.current) {
			result.current = range;
		} else {
			result.other.push_back(range);
		}
	}
	return result;
}

bool WholeSegmentSelected(
		const SelectableSegment &segment,
		const PaintSelectionState &selectionState) {
	if (!selectionState.segments || segment.isTextLeaf()) {
		return false;
	}
	if (WholeSegmentStructurallySelected(segment, selectionState)) {
		return true;
	}
	if (segment.kind == SelectableSegmentKind::Table) {
		return TableSegmentSelected(selectionState, segment.index);
	}
	return RangeSelectsWholeSegment(segment, selectionState.selection);
}

bool WholeSegmentSelected(
		const PaintSelectionState &selectionState,
		int index) {
	const auto segment = FindSegment(selectionState.segments, index);
	return segment
		? WholeSegmentSelected(*segment, selectionState)
		: false;
}

TextForMimeData TextForSegment(
		const SelectableSegment &segment,
		TextSelection selection) {
	switch (segment.kind) {
	case SelectableSegmentKind::TextLeaf:
		return segment.leaf
			? segment.leaf->toTextForMimeData(selection)
			: TextForMimeData();
	case SelectableSegmentKind::CodeBlock:
		return segment.block
			? CopyTextForCodeBlock(*segment.block, selection)
			: TextForMimeData();
	case SelectableSegmentKind::DisplayMath:
		return segment.block
			? CopyTextForDisplayMath(*segment.block)
			: TextForMimeData();
	case SelectableSegmentKind::Table:
		return segment.block
			? CopyTextForTable(*segment.block)
			: TextForMimeData();
	case SelectableSegmentKind::Placeholder:
		return segment.block
			? CopyTextForPlaceholderBlock(*segment.block)
			: TextForMimeData();
	case SelectableSegmentKind::Photo:
		return segment.block
			? CopyTextForPhotoBlock(*segment.block)
			: TextForMimeData();
	case SelectableSegmentKind::Media:
		return segment.block
			? CopyTextForSingleMediaBlock(*segment.block)
			: TextForMimeData();
	}
	return TextForMimeData();
}

TextForMimeData TextForSelectedSegments(
		const std::vector<SelectableSegment> &segments,
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const PreparedEditSelection *structuralSelection) {
	if (structuralSelection
		&& !structuralSelection->empty()
		&& (structuralSelection->kind == PreparedEditSelectionKind::TableRows
			|| structuralSelection->kind
				== PreparedEditSelectionKind::TableCells)) {
		return TextForStructuralTableSelection(segments, *structuralSelection);
	}
	if (selection.empty()
		&& (!structuralSelection || structuralSelection->empty())) {
		return TextForMimeData();
	}
	const auto selectionState = PaintSelectionState{
		.segments = &segments,
		.selection = selection,
		.endpoints = endpoints,
		.structuralSelection = structuralSelection,
	};
	auto pieces = std::vector<TextForMimeData>();
	for (const auto &segment : segments) {
		if (segment.isTextLeaf()) {
			if (segment.parentSegmentIndex >= 0
				&& WholeSegmentSelected(
					selectionState,
					segment.parentSegmentIndex)) {
				continue;
			}
			if (const auto textSelection = TextSelectionForSegment(
					segment,
					selectionState);
				textSelection && !textSelection->empty()) {
				if (auto text = TextForSegment(segment, *textSelection);
					!text.empty()) {
					pieces.push_back(std::move(text));
				}
			}
			continue;
		}
		if (!WholeSegmentSelected(segment, selectionState)) {
			continue;
		}
		if (auto text = TextForSegment(segment); !text.empty()) {
			pieces.push_back(std::move(text));
		}
	}
	if (pieces.empty()) {
		return TextForMimeData();
	} else if (pieces.size() == 1) {
		return std::move(pieces.front());
	}
	auto result = TextForMimeData();
	for (auto i = 0, count = int(pieces.size()); i != count; ++i) {
		if (i) {
			result.append(u"\n"_q);
		}
		result.append(std::move(pieces[i]));
	}
	return result;
}

std::vector<RichPage::Block> RichPageBlocksForSelectedSegments(
		const RichPage &page,
		const std::vector<SelectableSegment> &segments,
		MarkdownArticleSelection selection) {
	// Each endpoint maps to a top-level page block index through the
	// edit-source paths of the nearest contributing segment inside the
	// selection, skipping empty-edge nodes and coarsely falling back
	// past unresolvable segments; only directly addressable low-nesting
	// text leaves get edge-trimmed.
	selection = NormalizeSelection(selection);
	if (selection.empty()
		|| selection.from.segment == selection.to.segment
		|| page.blocks.empty()) {
		return {};
	}
	auto start = std::optional<RichPageSliceEndpoint>();
	auto startSegment = -1;
	for (auto i = selection.from.segment; i <= selection.to.segment; ++i) {
		const auto segment = FindSegment(&segments, i);
		if (!segment || !SegmentContributesToSelection(*segment, selection)) {
			continue;
		}
		if (auto resolved = ResolveRichPageSliceEndpoint(page, *segment)) {
			start = std::move(resolved);
			startSegment = i;
			break;
		}
	}
	auto end = std::optional<RichPageSliceEndpoint>();
	auto endSegment = -1;
	for (auto i = selection.to.segment; i >= selection.from.segment; --i) {
		const auto segment = FindSegment(&segments, i);
		if (!segment || !SegmentContributesToSelection(*segment, selection)) {
			continue;
		}
		if (auto resolved = ResolveRichPageSliceEndpoint(page, *segment)) {
			end = std::move(resolved);
			endSegment = i;
			break;
		}
	}
	if (!start || !end) {
		return {};
	}
	const auto startDirect = (startSegment == selection.from.segment);
	const auto endDirect = (endSegment == selection.to.segment);
	const auto firstTop = start->top;
	const auto lastTop = end->top;
	if (firstTop > lastTop) {
		return {};
	}
	auto result = std::vector<RichPage::Block>(
		page.blocks.begin() + firstTop,
		page.blocks.begin() + lastTop + 1);
	const auto startTarget = (startDirect
		&& start->trimLeaf
		&& selection.from.offset > 0)
		? RichPageSliceTrimTarget(&result.front(), *start->trimLeaf)
		: nullptr;
	const auto endTarget = (endDirect && end->trimLeaf)
		? RichPageSliceTrimTarget(&result.back(), *end->trimLeaf)
		: nullptr;
	if (startTarget && startTarget == endTarget) {
		const auto size = int(startTarget->text.size());
		const auto from = std::clamp(selection.from.offset, 0, size);
		const auto to = std::clamp(selection.to.offset, from, size);
		*startTarget = Ui::Text::Mid(*startTarget, from, to - from);
	} else {
		if (startTarget) {
			ApplyRichPageSliceStartTrim(startTarget, selection.from.offset);
		}
		if (endTarget) {
			ApplyRichPageSliceEndTrim(endTarget, selection.to.offset);
		}
	}
	const auto trimListStart = startDirect
		&& (start->listItem >= 0)
		&& (result.front().kind == RichPage::BlockKind::List)
		&& (start->listItem < int(result.front().listItems.size()));
	const auto trimListEnd = endDirect
		&& (end->listItem >= 0)
		&& (result.back().kind == RichPage::BlockKind::List)
		&& (end->listItem < int(result.back().listItems.size()));
	if (trimListStart && trimListEnd && firstTop == lastTop) {
		if (start->listItem <= end->listItem) {
			auto &items = result.front().listItems;
			items.erase(items.begin() + end->listItem + 1, items.end());
			items.erase(items.begin(), items.begin() + start->listItem);
		}
	} else {
		if (trimListStart) {
			auto &items = result.front().listItems;
			items.erase(items.begin(), items.begin() + start->listItem);
		}
		if (trimListEnd) {
			auto &items = result.back().listItems;
			items.erase(items.begin() + end->listItem + 1, items.end());
		}
	}
	if (!result.empty()
		&& RichPageSliceEdgeEmpty(result.front(), startTarget != nullptr)) {
		result.erase(result.begin());
	}
	if (!result.empty()
		&& RichPageSliceEdgeEmpty(result.back(), endTarget != nullptr)) {
		result.pop_back();
	}
	return result;
}

} // namespace Iv::Markdown
