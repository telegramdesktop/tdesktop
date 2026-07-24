/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/editor/iv_editor_clipboard.h"
#include "iv/iv_rich_page.h"
#include "iv/markdown/iv_markdown_prepare.h"

#include <memory>
#include <optional>
#include <vector>

namespace Iv::Editor {

class State final {
public:
	enum class FieldMode : uchar {
		Rich,
		Raw,
	};

	enum class ApplyResult {
		Failed,
		Unchanged,
		Changed,
	};

	enum class PreparedMutationKind {
		None,
		LeafOnly,
		FullRebuild,
	};

	enum class InsertBlockType : uchar {
		Heading,
		Blockquote,
		Code,
		Math,
		Footer,
		Divider,
		Anchor,
		OrderedList,
		BulletList,
		TaskList,
		Pullquote,
		Photo,
		Video,
		Audio,
		Details,
		Table,
		Map,
	};

	struct InsertAction {
		InsertBlockType type;
		int headingLevel = 1;
		double latitude = 0.;
		double longitude = 0.;
	};

	[[nodiscard]] static bool BlockConversionExpandsToActiveLine(
		InsertBlockType type);

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

	struct Snapshot {
		RichPage richPage;
		std::optional<LeafPath> activeLeaf;
		std::optional<LeafPath> temporaryDownParagraph;
	};

	struct InsertionAnchor {
		BlockContainerPath container;
		int blockIndex = -1;
	};

	enum class RemovalKind : uchar {
		Block,
		ListItem,
		TableCell,
	};

	struct RemovalTarget {
		RemovalKind kind = RemovalKind::Block;
		BlockPath block;
		int listItemIndex = -1;
		int tableRowIndex = -1;
		int tableCellIndex = -1;

		friend inline bool operator==(
				const RemovalTarget &a,
				const RemovalTarget &b) {
			return (a.kind == b.kind)
				&& (a.block == b.block)
				&& (a.listItemIndex == b.listItemIndex)
				&& (a.tableRowIndex == b.tableRowIndex)
				&& (a.tableCellIndex == b.tableCellIndex);
		}
	};

	struct TextNodeDescriptor {
		LeafPath leaf;
		InsertionAnchor insertionAnchor;
		RemovalTarget removalTarget;
		FieldMode mode = FieldMode::Rich;
	};

	struct BoundaryTarget {
		enum class Action : uchar {
			None,
			Text,
			StructuralSelection,
			RemoveActiveOwner,
		};

		Action action = Action::None;
		int textOrdinal = -1;
		Markdown::PreparedEditSelection structuralSelection;
	};

	struct TableSelectionInfo {
		bool valid = false;
		bool allHeader = false;
		bool allAlignLeft = false;
		bool allAlignCenter = false;
		bool allAlignRight = false;
		bool allAlignTop = false;
		bool allAlignMiddle = false;
		bool allAlignBottom = false;
		bool singleCell = false;
		bool canSplitCell = false;
		bool canUniteCells = false;
		int selectedRows = 0;
		int selectedColumns = 0;
		int totalRows = 0;
		int totalColumns = 0;
		bool bordered = false;
		bool striped = false;
	};

	struct ListSelectionInfo {
		bool valid = false;
		bool taskList = false;
		bool wholeList = false;
		bool singleItem = false;
		bool reversed = false;
		bool allOrderedDecimal = false;
		bool allOrderedLowerAlpha = false;
		bool allOrderedUpperAlpha = false;
		bool allOrderedLowerRoman = false;
		bool allOrderedUpperRoman = false;
		int selectedItems = 0;
		RichPage::ListKind listKind = RichPage::ListKind::Bullet;
	};

	enum class ListStyle : uchar {
		Ordered,
		Bullet,
		Task,
	};

	State();
	State(
		std::shared_ptr<RichPage> richPage,
		std::shared_ptr<Markdown::MediaRuntime> mediaRuntime,
		RichMessageLimits limits = {});

	[[nodiscard]] const RichPage &richPage() const;
	[[nodiscard]] bool articleEmpty() const;
	[[nodiscard]] const Markdown::MarkdownArticleContent &prepared() const;
	[[nodiscard]] const std::vector<TextNodeDescriptor> &textNodes() const;
	[[nodiscard]] Snapshot snapshot() const;
	void restoreSnapshot(Snapshot snapshot);
	[[nodiscard]] std::optional<LeafPath> activeLeafPath() const;
	[[nodiscard]] int textOrdinalForLeafPath(const LeafPath &path) const;
	[[nodiscard]] int textOrdinalForLeaf(
		const Markdown::PreparedEditLeafSource &source) const;
	[[nodiscard]] auto preparedLeafSourceForOrdinal(int ordinal) const
	-> std::optional<Markdown::PreparedEditLeafSource>;
	[[nodiscard]] PreparedMutationKind lastPreparedMutationKind() const;
	[[nodiscard]] auto activePreparedLeafSource() const
	-> std::optional<Markdown::PreparedEditLeafSource>;
	[[nodiscard]] int textNodeCount() const;
	[[nodiscard]] int activeTextOrdinal() const;
	[[nodiscard]] bool setActiveTextByOrdinal(int ordinal);
	[[nodiscard]] TextWithEntities activeText() const;
	[[nodiscard]] ApplyResult applyActiveText(TextWithEntities text);
	[[nodiscard]] FieldMode activeFieldMode() const;
	[[nodiscard]] QString activeRawText() const;
	[[nodiscard]] QString activePlaceholderText() const;
	[[nodiscard]] ApplyResult applyActiveRawText(QString text);
	[[nodiscard]] std::optional<RichMessageLimitError> lastLimitError() const;
	[[nodiscard]] std::optional<QString> codeBlockLanguage(int ordinal) const;
	[[nodiscard]] bool setCodeBlockLanguage(int ordinal, QString language);
	[[nodiscard]] int activeTextLength() const;
	[[nodiscard]] std::optional<int> previousEditableOrdinal() const;
	[[nodiscard]] std::optional<int> nextEditableOrdinal() const;
	[[nodiscard]] std::optional<int> firstTableCellOrdinalFromActiveTitle() const;
	[[nodiscard]] std::optional<int> adjacentRowTableCellOrdinal(
		bool down) const;
	[[nodiscard]] std::optional<int> tableTitleOrdinalFromActiveCell() const;
	[[nodiscard]] std::optional<int> ordinalAfterActiveTable() const;
	[[nodiscard]] BoundaryTarget activeBoundaryTarget(bool forward) const;
	[[nodiscard]] std::vector<BoundaryTarget> boundarySteps(
		bool forward) const;
	[[nodiscard]] bool isActiveTopLevelParagraph() const;
	[[nodiscard]] bool isActiveTopLevelParagraphOrHeading() const;
	[[nodiscard]] bool hasActiveListItemSurface() const;
	[[nodiscard]] bool activeSurfaceAllowsSeparateLineFormula() const;
	[[nodiscard]] bool activeLeafUsesQuoteCaptionColor() const;
	[[nodiscard]] bool activeLeafUsesQuotePlaceholderColor() const;
	[[nodiscard]] bool activeBlockBodyCanEscape() const;
	[[nodiscard]] std::optional<int> moveActiveSpecialBlockDown();
	[[nodiscard]] std::optional<int> escapeActiveBlockBody();
	[[nodiscard]] BoundaryTarget removeTemporaryDownParagraphAndMove();
	enum class EnterPosition : uchar {
		End,
		Beginning,
		Middle,
	};
	struct ActiveEnterContext {
		EnterPosition position = EnterPosition::End;
		TextWithEntities head;
		TextWithEntities tail;
	};
	[[nodiscard]] std::optional<int> submitActiveSingleLineField(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveHeadingEnter(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveFooterEnter(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveListEnter(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveParagraphEnter(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveQuoteEnter(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> removeActiveOwnerAndSelectAdjacent(
		bool forward);
	[[nodiscard]] std::optional<int> removeStructuralSelection(
		const Markdown::PreparedEditSelection &selection,
		bool forward);
	[[nodiscard]] bool toggleTaskState(
		const Markdown::PreparedEditListItemSource &source);
	[[nodiscard]] bool toggleDetailsOpen(
		const Markdown::PreparedEditBlockSource &source);
	[[nodiscard]] ListSelectionInfo listSelectionInfo(
		const Markdown::PreparedEditListItemRange &range) const;
	[[nodiscard]] std::optional<Markdown::PreparedEditListItemRange>
	listContextRangeForSelection(
		const Markdown::PreparedEditSelection &selection,
		const Markdown::PreparedEditListItemSource &source) const;
	[[nodiscard]] bool setListStyle(
		const Markdown::PreparedEditListItemRange &range,
		ListStyle style);
	[[nodiscard]] bool setListOrderedType(
		const Markdown::PreparedEditListItemRange &range,
		Markdown::PreparedOrderedListType type);
	[[nodiscard]] bool setListOrderedReversed(
		const Markdown::PreparedEditListItemRange &range,
		bool reversed);
	[[nodiscard]] bool setListItemOrderedType(
		const Markdown::PreparedEditListItemRange &range,
		std::optional<Markdown::PreparedOrderedListType> type);
	[[nodiscard]] TableSelectionInfo tableSelectionInfo(
		const Markdown::PreparedEditTableCellRange &range) const;
	[[nodiscard]] std::optional<Markdown::PreparedEditTableCellRange>
	tableContextRangeForSelection(
		const Markdown::PreparedEditSelection &selection,
		const Markdown::PreparedEditTableCellSource &source) const;
	[[nodiscard]] std::optional<BlockPath> convertBlockPath(
		const Markdown::PreparedEditBlockPath &path) const;
	[[nodiscard]] std::optional<BlockPath> convertBlockPath(
		const Markdown::PreparedEditBlockSource &source) const;
	[[nodiscard]] bool canRemoveStructuralSelection(
		const Markdown::PreparedEditSelection &selection) const;
	[[nodiscard]] auto structuredClipboardDataForSelection(
		const Markdown::PreparedEditSelection &selection) const
	-> std::optional<ClipboardData>;
	[[nodiscard]] std::shared_ptr<const RichPage> richPageForTableSelection(
		const Markdown::PreparedEditSelection &selection) const;
	[[nodiscard]] bool insertPreparedBlocksAfterTableSelection(
		const Markdown::PreparedEditSelection &selection,
		std::vector<RichPage::Block> blocks);
	enum class TableInPlaceApplyResult : uchar {
		Applied,
		Unchanged,
		StructureMismatch,
		Failed,
	};
	[[nodiscard]] TableInPlaceApplyResult replaceTableSelectionCellsInPlace(
		const Markdown::PreparedEditSelection &selection,
		const RichPage &page);
	[[nodiscard]] bool addTableRow(
		const Markdown::PreparedEditTableCellRange &range,
		bool after);
	[[nodiscard]] bool addTableColumn(
		const Markdown::PreparedEditTableCellRange &range,
		bool after);
	[[nodiscard]] bool setTableHeader(
		const Markdown::PreparedEditTableCellRange &range,
		bool header);
	[[nodiscard]] bool setTableAlignment(
		const Markdown::PreparedEditTableCellRange &range,
		RichPage::TableAlignment alignment);
	[[nodiscard]] bool setTableVerticalAlignment(
		const Markdown::PreparedEditTableCellRange &range,
		RichPage::TableVerticalAlignment alignment);
	[[nodiscard]] bool splitTableCell(
		const Markdown::PreparedEditTableCellRange &range);
	[[nodiscard]] bool uniteTableCells(
		const Markdown::PreparedEditTableCellRange &range);
	[[nodiscard]] bool removeTableRows(
		const Markdown::PreparedEditTableCellRange &range);
	[[nodiscard]] bool removeTableColumns(
		const Markdown::PreparedEditTableCellRange &range);
	[[nodiscard]] bool removeTable(
		const Markdown::PreparedEditTableCellRange &range);
	[[nodiscard]] bool setTableBordered(
		const Markdown::PreparedEditTableCellRange &range,
		bool bordered);
	[[nodiscard]] bool setTableStriped(
		const Markdown::PreparedEditTableCellRange &range,
		bool striped);
	[[nodiscard]] std::optional<int> ensureTrailingParagraphActive();

	// Inserts an empty paragraph at the very start of the top-level blocks
	// list, so content can always be added above a non-trivial first block
	// (a table, a list, ...). Returns the ordinal of the inserted paragraph
	// when focusInserted, otherwise the new ordinal of the still-active leaf.
	[[nodiscard]] std::optional<int> insertLeadingParagraphActive(
		bool focusInserted);
	void resyncAfterExternalRichPageMutation();
	void insertHeading1AfterActive();
	void insertBlockquoteAfterActive();
	struct ActiveTextInsertContext {
		TextWithEntities before;
		TextWithEntities selected;
		TextWithEntities after;
	};
	struct ActiveTextBlockActionResult {
		ApplyResult result = ApplyResult::Failed;
		std::optional<LeafPath> destinationLeaf;
		int selectionFrom = 0;
		int selectionTo = 0;
	};
	struct DisplayMathEditResult {
		ApplyResult result = ApplyResult::Failed;
		std::optional<LeafPath> inlineLeaf;
		int selectionFrom = 0;
		int selectionTo = 0;
	};
	struct ParagraphBoundaryJoinResult {
		ApplyResult result = ApplyResult::Failed;
		std::optional<LeafPath> destinationLeaf;
		int selectionFrom = 0;
		int selectionTo = 0;
	};
	struct StructuralSelectionDropResult {
		ApplyResult result = ApplyResult::Failed;
		BoundaryTarget destination;
	};
	[[nodiscard]] DisplayMathEditResult editActiveDisplayMath(
		QString source,
		bool separateLine);
	[[nodiscard]] ParagraphBoundaryJoinResult joinActiveParagraphBoundary(
		bool forward);
	[[nodiscard]] bool insertBlockAfterActive(
		InsertAction action,
		std::optional<ActiveTextInsertContext> context = std::nullopt);
	[[nodiscard]] ActiveTextBlockActionResult applyActiveTextBlockAction(
		InsertAction action,
		ActiveTextInsertContext context);
	[[nodiscard]] ActiveTextBlockActionResult
		replaceActiveTextSelectionWithText(
			TextWithEntities text,
			ActiveTextInsertContext context);
	[[nodiscard]] bool insertPreparedBlockAfterActive(RichPage::Block block);
	[[nodiscard]] bool insertPreparedBlocksAfterActive(
		std::vector<RichPage::Block> blocks,
		std::optional<ActiveTextInsertContext> context = std::nullopt);
	[[nodiscard]] bool pasteClipboardListItemsAfterActive(
		const ClipboardListItemsData &data,
		std::optional<ActiveTextInsertContext> context = std::nullopt);
	[[nodiscard]] bool replaceStructuralSelectionWithBlock(
		const Markdown::PreparedEditSelection &selection,
		InsertAction action,
		std::optional<ActiveTextInsertContext> context = std::nullopt,
		BoundaryTarget *destination = nullptr);
	[[nodiscard]] bool toggleCodeBlockForStructuralSelection(
		const Markdown::PreparedEditSelection &selection);
	[[nodiscard]] bool replaceStructuralSelectionWithPreparedBlocks(
		const Markdown::PreparedEditSelection &selection,
		std::vector<RichPage::Block> blocks,
		std::optional<ActiveTextInsertContext> context = std::nullopt);
	[[nodiscard]] bool replaceStructuralSelectionWithClipboardListItems(
		const Markdown::PreparedEditSelection &selection,
		const ClipboardListItemsData &data,
		std::optional<ActiveTextInsertContext> context = std::nullopt);
	[[nodiscard]] StructuralSelectionDropResult
	moveStructuralSelectionToDropTarget(
		const Markdown::PreparedEditSelection &selection,
		const Markdown::PreparedEditDropTarget &target);
	[[nodiscard]] bool insertPreparedBlocksAtDropTarget(
		std::vector<RichPage::Block> blocks,
		const Markdown::PreparedEditBlockDropTarget &target);
	enum class TextFormattingAction : uchar {
		Bold,
		Italic,
		Underline,
		StrikeOut,
		Spoiler,
		PlainText,
	};
	struct TextNodeSpan {
		LeafPath leaf;
		int from = 0;
		int till = 0;
	};
	struct TextSelectionDropResult {
		ApplyResult result = ApplyResult::Failed;
		std::optional<LeafPath> destinationLeaf;
		int selectionFrom = 0;
		int selectionTo = 0;
	};
	[[nodiscard]] std::vector<TextNodeSpan> resolveTextSpansForPreparedLeafRange(
		const Markdown::PreparedEditLeafSource &source,
		int from,
		int till) const;
	[[nodiscard]] TextSelectionDropResult moveTextSelectionToDropTarget(
		const std::vector<TextNodeSpan> &source,
		const Markdown::PreparedEditDropTarget &target);
	[[nodiscard]] TextSelectionDropResult moveTextSelectionToDropTarget(
		const TextNodeSpan &source,
		const Markdown::PreparedEditDropTarget &target);
	[[nodiscard]] ApplyResult applyFormattingToTextSpans(
		const std::vector<TextNodeSpan> &spans,
		TextFormattingAction action,
		std::optional<bool> enabled = std::nullopt);
	[[nodiscard]] bool toggleSpoilerOnBlocks(
		const std::vector<BlockPath> &blocks,
		std::optional<bool> enabled = std::nullopt);
	[[nodiscard]] bool toggleSpoilerOnGroupedItem(
		const BlockPath &path,
		int itemIndex,
		std::optional<bool> enabled = std::nullopt);
	[[nodiscard]] std::optional<ReplaceTarget> replaceTargetForBlock(
		const BlockPath &path) const;
	[[nodiscard]] std::optional<ReplaceTarget> replaceTargetForGroupedItem(
		const BlockPath &path,
		int itemIndex) const;
	[[nodiscard]] bool replaceBlockWithPreparedBlock(
		const ReplaceTarget &target,
		RichPage::Block block);
	[[nodiscard]] std::optional<int> removeBlock(
		const BlockPath &path,
		bool forward);
	[[nodiscard]] bool canGroupPhotoVideoBlocks(
		const Markdown::PreparedEditSelection &selection) const;
	[[nodiscard]] bool groupPhotoVideoBlocks(
		const Markdown::PreparedEditSelection &selection,
		RichPage::GroupedMediaIntent intent);
	[[nodiscard]] bool ungroupGroupedMediaBlock(const BlockPath &path);
	[[nodiscard]] bool removeGroupedItem(
		const BlockPath &path,
		int itemIndex);
	[[nodiscard]] bool addItemsToGroupedMedia(
		const BlockPath &path,
		int insertedCount);
	[[nodiscard]] bool setGroupedMediaIntent(
		const BlockPath &path,
		RichPage::GroupedMediaIntent intent);
	[[nodiscard]] Markdown::PreparedEditSelection preparedSelectionForBlock(
		const BlockPath &path) const;

private:
	struct StructuralBlockRange {
		BlockContainerPath container;
		int from = -1;
		int till = -1;
	};

	struct StructuralListItemRange {
		BlockPath block;
		int from = -1;
		int till = -1;
	};

	struct StructuralTableRowRange {
		BlockPath block;
		int from = -1;
		int till = -1;
	};

	struct StructuralTableCellRange {
		BlockPath block;
		int rowFrom = -1;
		int rowTill = -1;
		int columnFrom = -1;
		int columnTill = -1;
	};

	struct ActiveQuote {
		BlockPath path;
		bool activeLeafIsLastEditableBodyLeaf = false;
	};

	struct ActiveListItemSurface {
		BlockPath path;
		int itemIndex = -1;
	};

	struct ParagraphTarget {
		LeafPath leaf;
		bool inserted = false;
	};

	struct ActiveTextInsertTarget {
		LeafPath leaf;
		InsertionAnchor anchor;
	};

	struct ActiveTextSelectionTarget {
		LeafPath leaf;
		int selectionFrom = 0;
		int selectionTo = 0;
	};

	struct RebuiltBoundaryTarget {
		BoundaryTarget::Action action = BoundaryTarget::Action::None;
		LeafPath leaf;
		BlockPath block;
		int listItemIndex = -1;
	};

	template <typename Result>
	struct CheckedMutationResult {
		bool apply = false;
		Result result;
	};

	[[nodiscard]] std::optional<BlockContainerPath> convertBlockContainerPath(
		const Markdown::PreparedEditBlockContainerPath &path) const;
	[[nodiscard]] std::optional<LeafPath> convertLeafPath(
		const Markdown::PreparedEditLeafSource &source) const;
	[[nodiscard]] std::optional<StructuralBlockRange> validateBlockRange(
		const Markdown::PreparedEditBlockRange &range) const;
	[[nodiscard]] std::optional<StructuralListItemRange> validateListItemRange(
		const Markdown::PreparedEditListItemRange &range) const;
	[[nodiscard]] std::optional<StructuralTableRowRange> validateTableRowRange(
		const Markdown::PreparedEditTableRowRange &range) const;
	[[nodiscard]] auto validateTableCellRange(
		const Markdown::PreparedEditTableCellRange &range) const
	-> std::optional<StructuralTableCellRange>;
	[[nodiscard]] bool leafWillBeRemoved(
		const LeafPath &path,
		const StructuralBlockRange &range) const;
	[[nodiscard]] bool leafWillBeRemoved(
		const LeafPath &path,
		const StructuralListItemRange &range) const;
	[[nodiscard]] bool leafWillBeRemoved(
		const LeafPath &path,
		const StructuralTableRowRange &range) const;
	[[nodiscard]] bool leafWillBeRemoved(
		const LeafPath &path,
		const StructuralTableCellRange &range) const;
	[[nodiscard]] bool leafBelongsToBlock(
		const LeafPath &leaf,
		const BlockPath &path) const;
	[[nodiscard]] std::optional<LeafPath> firstSelectedLeaf(
		const StructuralTableCellRange &range) const;
	[[nodiscard]] std::optional<LeafPath> adjacentLeafOutsideRange(
		const StructuralBlockRange &range,
		bool forward) const;
	[[nodiscard]] std::optional<LeafPath> adjacentLeafOutsideRange(
		const StructuralListItemRange &range,
		bool forward) const;
	[[nodiscard]] std::optional<LeafPath> adjacentLeafOutsideRange(
		const StructuralTableRowRange &range,
		bool forward) const;
	[[nodiscard]] std::optional<LeafPath> adjacentLeafOutsideRange(
		const StructuralTableCellRange &range,
		bool forward) const;
	[[nodiscard]] std::optional<LeafPath> fallbackFocusLeaf() const;
	[[nodiscard]] std::optional<LeafPath> plannedFocusForRange(
		const StructuralBlockRange &range,
		bool forward) const;
	[[nodiscard]] std::optional<LeafPath> plannedFocusForRange(
		const StructuralListItemRange &range,
		bool forward) const;
	[[nodiscard]] std::optional<LeafPath> plannedFocusForRange(
		const StructuralTableRowRange &range,
		bool forward) const;
	[[nodiscard]] std::optional<LeafPath> plannedFocusForRange(
		const StructuralTableCellRange &range,
		bool forward) const;
	[[nodiscard]] std::vector<RichPage::Block> *blockContainer(
		const BlockContainerPath &path);
	[[nodiscard]] const std::vector<RichPage::Block> *blockContainer(
		const BlockContainerPath &path) const;
	[[nodiscard]] RichPage::Block *block(const BlockPath &path);
	[[nodiscard]] const RichPage::Block *block(const BlockPath &path) const;
	[[nodiscard]] RichPage::ListItem *listItem(
		const BlockPath &blockPath,
		int itemIndex);
	[[nodiscard]] const RichPage::ListItem *listItem(
		const BlockPath &blockPath,
		int itemIndex) const;
	[[nodiscard]] RichPage::TableCell *tableCell(
		const BlockPath &blockPath,
		int rowIndex,
		int cellIndex);
	[[nodiscard]] const RichPage::TableCell *tableCell(
		const BlockPath &blockPath,
		int rowIndex,
		int cellIndex) const;
	[[nodiscard]] RichPage::RichText *richText(const LeafPath &path);
	[[nodiscard]] const RichPage::RichText *richText(
		const LeafPath &path) const;
	[[nodiscard]] QString *rawText(const LeafPath &path);
	[[nodiscard]] const QString *rawText(const LeafPath &path) const;
	[[nodiscard]] const TextNodeDescriptor *textNode(int ordinal) const;
	[[nodiscard]] const TextNodeDescriptor *adjacentTextNode(
		int ordinal,
		bool forward) const;
	[[nodiscard]] int textNodeOrdinal(const LeafPath &path) const;
	[[nodiscard]] auto convertPreparedLeafSource(const LeafPath &path) const
	-> std::optional<Markdown::PreparedEditLeafSource>;
	[[nodiscard]] auto convertPreparedLeafSource(
		const TextNodeDescriptor &descriptor) const
	-> std::optional<Markdown::PreparedEditLeafSource>;
	[[nodiscard]] auto tableRenderLimits() const
	-> Markdown::MarkdownPrepareTableRenderLimits;

	void rebuild();
	void rebuildPrepared();
	void rebuildTextNodes();
	void rebuildTextNodes(
		const std::vector<RichPage::Block> &blocks,
		const BlockContainerPath &container);
	void clearTemporaryDownParagraph();
	void clearTemporaryDownParagraphIfInvalid();
	void commitCheckedMutation(State state);
	template <typename Result, typename Callback>
	[[nodiscard]] Result applyCheckedMutation(
		Result failure,
		Callback &&callback);
	[[nodiscard]] std::optional<int> activateRebuiltLeaf(
		const LeafPath &path);
	[[nodiscard]] InsertionAnchor resolveActiveInsertionTarget() const;
	[[nodiscard]] std::optional<int> normalizeTextOnlyListItemForInsertion(
		const BlockContainerPath &container);
	[[nodiscard]] std::optional<int> normalizeTextOnlyQuoteSurface(
		const BlockContainerPath &container,
		bool keepEmptyParagraph);
	[[nodiscard]] std::optional<int> normalizeTextOnlyQuoteForInsertion(
		const BlockContainerPath &container);
	[[nodiscard]] bool normalizeTextOnlyContainerForInsertion(
		const BlockContainerPath &container,
		int *insertAt);
	[[nodiscard]] bool shouldReplaceActiveTextOnlyBlock(
		const TextNodeDescriptor &descriptor,
		const std::vector<RichPage::Block> &blocks) const;
	[[nodiscard]] std::optional<ParagraphTarget> reuseOrInsertParagraph(
		const BlockContainerPath &container,
		int index);
	[[nodiscard]] auto resolveActiveTextInsertTarget()
	-> std::optional<ActiveTextInsertTarget>;
	[[nodiscard]] auto activeQuote(bool pullquote) const
	-> std::optional<ActiveQuote>;
	[[nodiscard]] auto activeListItemSurface() const
	-> std::optional<ActiveListItemSurface>;
	[[nodiscard]] std::optional<LeafPath> leafAfterUnwrappingBlockChildren(
		const LeafPath &leaf,
		const BlockPath &wrapper) const;
	[[nodiscard]] bool unwrapActiveCodeBlockUnchecked(
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target);
	[[nodiscard]] bool unwrapActiveQuoteUnchecked(
		bool pullquote,
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target);
	[[nodiscard]] bool convertActiveHeadingOrFooterUnchecked(
		InsertAction action,
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target);
	[[nodiscard]] bool joinActiveParagraphBoundaryUnchecked(
		bool forward,
		ActiveTextSelectionTarget *target);
	[[nodiscard]] bool canJoinActiveTextBlockBoundary(bool forward) const;
	[[nodiscard]] bool canJoinActiveListItemBoundary() const;
	[[nodiscard]] bool joinActiveListItemBoundaryUnchecked(
		ActiveTextSelectionTarget *target);
	[[nodiscard]] auto normalizeActiveListItemSurface()
	-> std::optional<ActiveListItemSurface>;
	[[nodiscard]] ApplyResult applyActiveTextUnchecked(TextWithEntities text);
	[[nodiscard]] ApplyResult applyActiveRawTextUnchecked(QString text);
	[[nodiscard]] ApplyResult applyActiveTextWithLocalLimit(
		TextWithEntities text);
	[[nodiscard]] ApplyResult applyActiveRawTextWithLocalLimit(QString text);
	[[nodiscard]] ApplyResult applySplitParagraphText(
		const TextNodeDescriptor &descriptor,
		std::vector<TextWithEntities> chunks);
	[[nodiscard]] bool leafMutationKeepsTextNodes(
		const TextNodeDescriptor &descriptor) const;
	[[nodiscard]] bool updatePreparedActiveLeaf(
		const TextNodeDescriptor &descriptor);
	[[nodiscard]] bool addTableRowUnchecked(
		const Markdown::PreparedEditTableCellRange &range,
		bool after);
	[[nodiscard]] bool addTableColumnUnchecked(
		const Markdown::PreparedEditTableCellRange &range,
		bool after);
	[[nodiscard]] std::optional<int> ensureTrailingParagraphActiveUnchecked();
	[[nodiscard]] std::optional<int> insertLeadingParagraphActiveUnchecked(
		bool focusInserted);
	[[nodiscard]] std::optional<int> moveActiveSpecialBlockDownUnchecked();
	[[nodiscard]] std::optional<int> submitActiveSingleLineFieldUnchecked(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> escapeActiveBlockBodyUnchecked();
	[[nodiscard]] std::optional<BlockPath> activeBlockBodyEscapeBlock() const;
	[[nodiscard]] BoundaryTarget boundaryTargetForLeaf(
		const LeafPath &leaf,
		const TextNodeDescriptor *descriptor,
		bool forward,
		bool allowRemoveDirectly) const;
	[[nodiscard]] auto captureRebuiltBoundaryTarget(
		const BoundaryTarget &target) const
	-> std::optional<RebuiltBoundaryTarget>;
	void shiftRebuiltBoundaryTargetAfterRemovedBlock(
		RebuiltBoundaryTarget &target,
		const BlockPath &removed) const;
	[[nodiscard]] BoundaryTarget materializeBoundaryTarget(
		const RebuiltBoundaryTarget &target) const;
	[[nodiscard]] BoundaryTarget removeTemporaryDownParagraphAndMoveUnchecked();
	[[nodiscard]] std::optional<int> handleActiveHeadingEnterUnchecked(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveFooterEnterUnchecked(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveListEnterUnchecked(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveParagraphEnterUnchecked(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveQuoteEnterUnchecked(
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleActiveBlockEnterUnchecked(
		RichPage::BlockKind kind,
		const ActiveEnterContext &context);
	[[nodiscard]] std::optional<int> handleEnterAtBlockUnchecked(
		const BlockContainerPath &container,
		int index,
		const ActiveEnterContext &context);
	[[nodiscard]] bool insertBlocksAfterActiveUnchecked(
		std::vector<RichPage::Block> blocks,
		std::optional<ActiveTextInsertContext> context = std::nullopt);
	[[nodiscard]] bool insertPreparedBlocksAtExplicitPosition(
		std::vector<RichPage::Block> blocks,
		const BlockContainerPath &container,
		int *insertAt);
	[[nodiscard]] bool insertPreparedBlocksAtRemovedBlockRange(
		std::vector<RichPage::Block> blocks,
		const StructuralBlockRange &range);
	[[nodiscard]] bool wrapStructuralBlockSelection(
		const Markdown::PreparedEditSelection &selection,
		InsertAction action,
		BoundaryTarget *destination = nullptr);
	[[nodiscard]] bool unwrapMatchingStructuralWrapper(
		const Markdown::PreparedEditSelection &selection,
		InsertBlockType type,
		BoundaryTarget *destination = nullptr);
	[[nodiscard]] bool unwrapMatchingListItemWrapper(
		const Markdown::PreparedEditSelection &selection,
		InsertBlockType type,
		BoundaryTarget *destination = nullptr);
	[[nodiscard]] bool unwrapListItemIntoParent(
		const BlockPath &listPath,
		int itemIndex,
		bool materializeEmptyItem,
		BoundaryTarget *destination = nullptr);
	[[nodiscard]] std::vector<RichPage::Block> takeListItemBlocksForUnwrap(
		RichPage::ListItem *item);
	void adoptLeadingParagraphListItemText(RichPage::ListItem *item) const;
	[[nodiscard]] bool insertPreparedListItemsAtExplicitPosition(
		std::vector<RichPage::ListItem> items,
		const BlockPath &path,
		int insertAt);
	[[nodiscard]] bool insertBlocksAfterActiveWithContextUnchecked(
		std::vector<RichPage::Block> &blocks,
		const ActiveTextInsertContext &context);
	[[nodiscard]] RichPage::RichText *seedInsertedBlocks(
		std::vector<RichPage::Block> &blocks,
		TextWithEntities text);
	[[nodiscard]] RichPage::RichText *seedInsertedBlock(
		RichPage::Block &block);
	[[nodiscard]] bool appendInsertedTrailingText(
		const BlockContainerPath &container,
		int insertAt,
		int count,
		TextWithEntities text);
	void normalizeInsertedBlockAnchors(std::vector<RichPage::Block> &blocks);
	void normalizeInsertedBlockAnchors(
		std::vector<RichPage::Block> &root,
		RichPage::Block &block);
	void normalizeInsertedRichTextAnchors(
		std::vector<RichPage::Block> &root,
		RichPage::RichText &text);
	void appendBlockTextNode(
		const BlockPath &path,
		LeafKind kind,
		FieldMode mode = FieldMode::Rich,
		std::optional<InsertionAnchor> insertionAnchor = std::nullopt);
	void appendListItemTextNode(const BlockPath &path, int itemIndex);
	void appendTableCellTextNode(
		const BlockPath &path,
		int rowIndex,
		int cellIndex);
	void ensureActiveTextOrdinal();
	void ensureEditableNodes();
	void focusInsertedBlocks(
		const BlockContainerPath &container,
		int from,
		int count);
	[[nodiscard]] BoundaryTarget destinationTargetForInsertedBlocks(
		const BlockContainerPath &container,
		int from,
		int count);
	[[nodiscard]] BoundaryTarget destinationTargetForInsertedListItems(
		const BlockPath &path,
		int from,
		int count);
	[[nodiscard]] std::optional<int> adjacentEditableOrdinal(
		bool forward) const;
	void collectBoundarySteps(
		const std::vector<RichPage::Block> &blocks,
		const BlockContainerPath &container,
		bool forward,
		std::vector<BoundaryTarget> *steps) const;
	void appendBoundaryTextStep(
		LeafPath leaf,
		std::vector<BoundaryTarget> *steps) const;
	void appendBoundaryBlockStep(
		const BlockPath &path,
		std::vector<BoundaryTarget> *steps) const;
	void appendBoundaryListItemStep(
		const BlockPath &path,
		int itemIndex,
		std::vector<BoundaryTarget> *steps) const;
	[[nodiscard]] Markdown::PreparedEditSelection preparedSelectionForListItem(
		const BlockPath &path,
		int itemIndex) const;
	[[nodiscard]] bool shouldRemoveActiveOwnerDirectly(
		const TextNodeDescriptor &descriptor) const;
	[[nodiscard]] bool descriptorBelongsToBlock(
		const TextNodeDescriptor &descriptor,
		const BlockPath &path) const;
	[[nodiscard]] bool removalTargetIsEmpty(
		const RemovalTarget &target) const;
	[[nodiscard]] bool removeTarget(const RemovalTarget &target);
	[[nodiscard]] bool anchorIdExists(const QString &id) const;
	[[nodiscard]] bool anchorIdExists(
		const std::vector<RichPage::Block> &blocks,
		const QString &id) const;
	[[nodiscard]] QString nextAnchorId() const;
	[[nodiscard]] RichPage::Block makeBlock(InsertAction action) const;

	[[nodiscard]] static TextWithEntities MakeText(QString text);
	[[nodiscard]] static RichPage::Block MakeParagraphBlock();
	[[nodiscard]] static RichPage::Block MakeFooterBlock();
	[[nodiscard]] static RichPage::Block MakeHeadingBlock(int level);
	[[nodiscard]] static RichPage::Block MakeQuoteBlock(bool pullquote);
	[[nodiscard]] static RichPage::Block MakeCodeBlock();
	[[nodiscard]] static RichPage::Block MakeMathBlock();
	[[nodiscard]] static RichPage::Block MakeDividerBlock();
	[[nodiscard]] static RichPage::Block MakeAnchorBlock(QString anchorId);
	[[nodiscard]] static RichPage::Block MakeListBlock(
		RichPage::ListKind kind,
		RichPage::TaskState taskState = RichPage::TaskState::None);
	[[nodiscard]] static RichPage::ListItem MakeParagraphListItem(
		RichPage::TaskState taskState);
	[[nodiscard]] static RichPage::Block MakeDetailsBlock();
	[[nodiscard]] static RichPage::Block MakeTableBlock();
	[[nodiscard]] static RichPage::Block MakeMediaBlock(
		RichPage::BlockKind kind);
	[[nodiscard]] static RichPage::Block MakeMapBlock(
		double latitude,
		double longitude);
	[[nodiscard]] static bool RichTextIsEmpty(const RichPage::RichText &text);
	[[nodiscard]] static bool ListItemIsEmpty(
		const RichPage::ListItem &item);
	[[nodiscard]] static bool BlockIsEmpty(const RichPage::Block &block);
	[[nodiscard]] static bool StripWrapperEntityInEditMode(EntityType type);
	[[nodiscard]] static TextWithEntities StripEditModeWrapperEntities(
		TextWithEntities text);
	static void StripEditModeWrapperEntities(RichPage::RichText &text);
	static void StripEditModeWrapperEntities(
		std::vector<RichPage::Block> &blocks);

	std::shared_ptr<RichPage> _richPage;
	std::shared_ptr<Markdown::MediaRuntime> _mediaRuntime;
	RichMessageLimits _limits;
	Markdown::MarkdownArticleContent _prepared;
	std::vector<TextNodeDescriptor> _textNodes;
	int _activeTextOrdinal = -1;
	PreparedMutationKind _lastPreparedMutationKind
		= PreparedMutationKind::None;
	std::optional<RichMessageLimitError> _lastLimitError;
	std::optional<LeafPath> _temporaryDownParagraph;

};

enum class RequestMediaType : uchar {
	PhotoVideo,
	Audio,
	PhotoVideoAudio,
};

struct MediaUploadState {
	bool uploading = false;
};

[[nodiscard]] bool CanEditRichPage(const RichPage &page);
[[nodiscard]] bool CanEditRichPage(
	const std::shared_ptr<const RichPage> &page);

} // namespace Iv::Editor
