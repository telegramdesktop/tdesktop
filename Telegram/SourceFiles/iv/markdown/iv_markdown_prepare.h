/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_document.h"
#include "iv/markdown/iv_markdown_math_renderer.h"

#include "ui/text/text_entity.h"

#include <array>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace style {
struct Markdown;
} // namespace style

namespace Iv {
struct RichPage;
struct RichMessageLimits;
} // namespace Iv

namespace Iv::Markdown {

enum class PreparedBlockKind {
	Paragraph,
	Thinking,
	Heading,
	CodeBlock,
	Rule,
	List,
	ListItem,
	Quote,
	DisplayMath,
	Table,
	Details,
	Photo,
	Video,
	Audio,
	Map,
	Channel,
	GroupedMedia,
	RelatedArticle,
	EmbedPost,
	Placeholder,
};

enum class PreparedLinkKind {
	External,
	InstantViewPage,
	Anchor,
	Footnote,
	FootnoteBacklink,
	LocalFile,
	RejectedRelative,
	ToggleDetails,
};

struct PreparedLink {
	uint16 index = 0;
	PreparedLinkKind kind = PreparedLinkKind::External;
	QString target;
	QString fragment;
	QString copyText;
	EntityType entityType = EntityType::Invalid;
	EntityLinkShown shown = EntityLinkShown::Full;
	uint64 webpageId = 0;
};

enum class InlineTextObjectKind {
	Formula,
	IvImage,
};

struct InlineTextObjectFormulaData {
	QString copySource;
	QString trimmedTex;
};

struct InlineTextObjectIvImageData {
	uint64 documentId = 0;
	int width = 0;
	int height = 0;
	QString replacementText;
};

struct InlineTextObjectEntity {
	InlineTextObjectKind kind = InlineTextObjectKind::Formula;
	std::variant<
		InlineTextObjectFormulaData,
		InlineTextObjectIvImageData> data = InlineTextObjectFormulaData();
};

enum class PreparedTableCellVerticalAlignment {
	Top,
	Middle,
	Bottom,
};

enum class PreparedOrderedListType {
	Decimal,
	LowerAlpha,
	UpperAlpha,
	LowerRoman,
	UpperRoman,
};

enum class PreparedEditBlockContainerKind {
	Root,
	BlockChildren,
	ListItemChildren,
};

struct PreparedEditBlockContainerStep {
	PreparedEditBlockContainerKind kind
		= PreparedEditBlockContainerKind::BlockChildren;
	int blockIndex = -1;
	int listItemIndex = -1;

	friend inline bool operator==(
			const PreparedEditBlockContainerStep &a,
			const PreparedEditBlockContainerStep &b) {
		return (a.kind == b.kind)
			&& (a.blockIndex == b.blockIndex)
			&& (a.listItemIndex == b.listItemIndex);
	}

	friend inline bool operator!=(
			const PreparedEditBlockContainerStep &a,
			const PreparedEditBlockContainerStep &b) {
		return !(a == b);
	}
};

struct PreparedEditBlockContainerPath {
	std::vector<PreparedEditBlockContainerStep> steps;

	friend inline bool operator==(
			const PreparedEditBlockContainerPath &a,
			const PreparedEditBlockContainerPath &b) {
		return (a.steps == b.steps);
	}

	friend inline bool operator!=(
			const PreparedEditBlockContainerPath &a,
			const PreparedEditBlockContainerPath &b) {
		return !(a == b);
	}
};

struct PreparedEditBlockPath {
	PreparedEditBlockContainerPath container;
	int index = -1;

	friend inline bool operator==(
			const PreparedEditBlockPath &a,
			const PreparedEditBlockPath &b) {
		return (a.container == b.container)
			&& (a.index == b.index);
	}

	friend inline bool operator!=(
			const PreparedEditBlockPath &a,
			const PreparedEditBlockPath &b) {
		return !(a == b);
	}
};

enum class PreparedEditLeafKind {
	BlockText,
	BlockCaption,
	ListItemText,
	TableCellText,
	MathFormula,
};

struct PreparedEditLeafSource {
	PreparedEditLeafKind kind = PreparedEditLeafKind::BlockText;
	PreparedEditBlockPath block;
	int listItemIndex = -1;
	int tableRowIndex = -1;
	int tableCellIndex = -1;

	friend inline bool operator==(
			const PreparedEditLeafSource &a,
			const PreparedEditLeafSource &b) {
		return (a.kind == b.kind)
			&& (a.block == b.block)
			&& (a.listItemIndex == b.listItemIndex)
			&& (a.tableRowIndex == b.tableRowIndex)
			&& (a.tableCellIndex == b.tableCellIndex);
	}

	friend inline bool operator!=(
			const PreparedEditLeafSource &a,
			const PreparedEditLeafSource &b) {
		return !(a == b);
	}
};

struct PreparedEditBlockSource {
	PreparedEditBlockPath path;

	friend inline bool operator==(
			const PreparedEditBlockSource &a,
			const PreparedEditBlockSource &b) {
		return (a.path == b.path);
	}

	friend inline bool operator!=(
			const PreparedEditBlockSource &a,
			const PreparedEditBlockSource &b) {
		return !(a == b);
	}
};

struct PreparedEditListItemSource {
	PreparedEditBlockPath block;
	int listItemIndex = -1;

	friend inline bool operator==(
			const PreparedEditListItemSource &a,
			const PreparedEditListItemSource &b) {
		return (a.block == b.block)
			&& (a.listItemIndex == b.listItemIndex);
	}

	friend inline bool operator!=(
			const PreparedEditListItemSource &a,
			const PreparedEditListItemSource &b) {
		return !(a == b);
	}
};

struct PreparedEditTableRowSource {
	PreparedEditBlockPath block;
	int tableRowIndex = -1;

	friend inline bool operator==(
			const PreparedEditTableRowSource &a,
			const PreparedEditTableRowSource &b) {
		return (a.block == b.block)
			&& (a.tableRowIndex == b.tableRowIndex);
	}

	friend inline bool operator!=(
			const PreparedEditTableRowSource &a,
			const PreparedEditTableRowSource &b) {
		return !(a == b);
	}
};

struct PreparedEditTableCellSource {
	PreparedEditBlockPath block;
	int tableRowIndex = -1;
	int tableCellIndex = -1;
	int column = -1;
	int colspan = 1;
	int rowspan = 1;

	friend inline bool operator==(
			const PreparedEditTableCellSource &a,
			const PreparedEditTableCellSource &b) {
		return (a.block == b.block)
			&& (a.tableRowIndex == b.tableRowIndex)
			&& (a.tableCellIndex == b.tableCellIndex)
			&& (a.column == b.column)
			&& (a.colspan == b.colspan)
			&& (a.rowspan == b.rowspan);
	}

	friend inline bool operator!=(
			const PreparedEditTableCellSource &a,
			const PreparedEditTableCellSource &b) {
		return !(a == b);
	}
};

struct PreparedEditBlockRange {
	PreparedEditBlockContainerPath container;
	int from = -1;
	int till = -1;

	[[nodiscard]] bool empty() const {
		return (from < 0) || (till <= from);
	}

	friend inline bool operator==(
			const PreparedEditBlockRange &a,
			const PreparedEditBlockRange &b) {
		return (a.container == b.container)
			&& (a.from == b.from)
			&& (a.till == b.till);
	}

	friend inline bool operator!=(
			const PreparedEditBlockRange &a,
			const PreparedEditBlockRange &b) {
		return !(a == b);
	}
};

struct PreparedEditListItemRange {
	PreparedEditBlockPath block;
	int from = -1;
	int till = -1;

	[[nodiscard]] bool empty() const {
		return (from < 0) || (till <= from);
	}

	friend inline bool operator==(
			const PreparedEditListItemRange &a,
			const PreparedEditListItemRange &b) {
		return (a.block == b.block)
			&& (a.from == b.from)
			&& (a.till == b.till);
	}

	friend inline bool operator!=(
			const PreparedEditListItemRange &a,
			const PreparedEditListItemRange &b) {
		return !(a == b);
	}
};

struct PreparedEditTableRowRange {
	PreparedEditBlockPath block;
	int from = -1;
	int till = -1;

	[[nodiscard]] bool empty() const {
		return (from < 0) || (till <= from);
	}

	friend inline bool operator==(
			const PreparedEditTableRowRange &a,
			const PreparedEditTableRowRange &b) {
		return (a.block == b.block)
			&& (a.from == b.from)
			&& (a.till == b.till);
	}

	friend inline bool operator!=(
			const PreparedEditTableRowRange &a,
			const PreparedEditTableRowRange &b) {
		return !(a == b);
	}
};

struct PreparedEditTableCellRange {
	PreparedEditBlockPath block;
	int rowFrom = -1;
	int rowTill = -1;
	int columnFrom = -1;
	int columnTill = -1;

	[[nodiscard]] bool empty() const {
		return (rowFrom < 0)
			|| (rowTill <= rowFrom)
			|| (columnFrom < 0)
			|| (columnTill <= columnFrom);
	}

	friend inline bool operator==(
			const PreparedEditTableCellRange &a,
			const PreparedEditTableCellRange &b) {
		return (a.block == b.block)
			&& (a.rowFrom == b.rowFrom)
			&& (a.rowTill == b.rowTill)
			&& (a.columnFrom == b.columnFrom)
			&& (a.columnTill == b.columnTill);
	}

	friend inline bool operator!=(
			const PreparedEditTableCellRange &a,
			const PreparedEditTableCellRange &b) {
		return !(a == b);
	}
};

enum class PreparedEditSelectionKind {
	None,
	Blocks,
	ListItems,
	TableRows,
	TableCells,
};

struct PreparedEditSelection {
	PreparedEditSelectionKind kind = PreparedEditSelectionKind::None;
	PreparedEditBlockRange blocks;
	PreparedEditListItemRange listItems;
	PreparedEditTableRowRange tableRows;
	PreparedEditTableCellRange tableCells;

	[[nodiscard]] bool empty() const {
		switch (kind) {
		case PreparedEditSelectionKind::Blocks:
			return blocks.empty();
		case PreparedEditSelectionKind::ListItems:
			return listItems.empty();
		case PreparedEditSelectionKind::TableRows:
			return tableRows.empty();
		case PreparedEditSelectionKind::TableCells:
			return tableCells.empty();
		case PreparedEditSelectionKind::None:
			return true;
		}
		return true;
	}

	friend inline bool operator==(
			const PreparedEditSelection &a,
			const PreparedEditSelection &b) {
		return (a.kind == b.kind)
			&& (a.blocks == b.blocks)
			&& (a.listItems == b.listItems)
			&& (a.tableRows == b.tableRows)
			&& (a.tableCells == b.tableCells);
	}

	friend inline bool operator!=(
			const PreparedEditSelection &a,
			const PreparedEditSelection &b) {
		return !(a == b);
	}
};

enum class PreparedEditHitKind {
	None,
	Block,
	ListItem,
	TableRow,
	TableCell,
	Leaf,
};

struct PreparedEditHit {
	PreparedEditHitKind kind = PreparedEditHitKind::None;
	std::optional<PreparedEditBlockSource> block;
	std::optional<PreparedEditListItemSource> listItem;
	std::optional<PreparedEditTableRowSource> tableRow;
	std::optional<PreparedEditTableCellSource> tableCell;
	std::optional<PreparedEditLeafSource> leaf;

	[[nodiscard]] bool valid() const {
		switch (kind) {
		case PreparedEditHitKind::Block:
			return block.has_value();
		case PreparedEditHitKind::ListItem:
			return listItem.has_value();
		case PreparedEditHitKind::TableRow:
			return tableRow.has_value();
		case PreparedEditHitKind::TableCell:
			return tableCell.has_value();
		case PreparedEditHitKind::Leaf:
			return leaf.has_value();
		case PreparedEditHitKind::None:
			return false;
		}
		return false;
	}

	friend inline bool operator==(
			const PreparedEditHit &a,
			const PreparedEditHit &b) {
		return (a.kind == b.kind)
			&& (a.block == b.block)
			&& (a.listItem == b.listItem)
			&& (a.tableRow == b.tableRow)
			&& (a.tableCell == b.tableCell)
			&& (a.leaf == b.leaf);
	}

	friend inline bool operator!=(
			const PreparedEditHit &a,
			const PreparedEditHit &b) {
		return !(a == b);
	}
};

struct PreparedEditTextDropTarget {
	PreparedEditLeafSource leaf;
	int offset = 0;

	friend inline bool operator==(
			const PreparedEditTextDropTarget &a,
			const PreparedEditTextDropTarget &b) {
		return (a.leaf == b.leaf)
			&& (a.offset == b.offset);
	}

	friend inline bool operator!=(
			const PreparedEditTextDropTarget &a,
			const PreparedEditTextDropTarget &b) {
		return !(a == b);
	}
};

struct PreparedEditBlockDropTarget {
	PreparedEditBlockContainerPath container;
	int insertIndex = -1;

	friend inline bool operator==(
			const PreparedEditBlockDropTarget &a,
			const PreparedEditBlockDropTarget &b) {
		return (a.container == b.container)
			&& (a.insertIndex == b.insertIndex);
	}

	friend inline bool operator!=(
			const PreparedEditBlockDropTarget &a,
			const PreparedEditBlockDropTarget &b) {
		return !(a == b);
	}
};

struct PreparedEditListItemDropTarget {
	PreparedEditBlockPath block;
	int insertIndex = -1;

	friend inline bool operator==(
			const PreparedEditListItemDropTarget &a,
			const PreparedEditListItemDropTarget &b) {
		return (a.block == b.block)
			&& (a.insertIndex == b.insertIndex);
	}

	friend inline bool operator!=(
			const PreparedEditListItemDropTarget &a,
			const PreparedEditListItemDropTarget &b) {
		return !(a == b);
	}
};

using PreparedEditDropTarget = std::variant<
	PreparedEditTextDropTarget,
	PreparedEditBlockDropTarget,
	PreparedEditListItemDropTarget>;

struct PreparedTableCell {
	TextWithEntities text;
	std::vector<PreparedLink> links;
	int column = 0;
	TableAlignment alignment = TableAlignment::None;
	bool header = false;
	PreparedTableCellVerticalAlignment verticalAlignment
		= PreparedTableCellVerticalAlignment::Top;
	int colspan = 1;
	int rowspan = 1;
	std::optional<PreparedEditTableCellSource> editCell;
	std::optional<PreparedEditLeafSource> editLeaf;
	QString editPlaceholderText;
};

struct PreparedTableRow {
	std::vector<PreparedTableCell> cells;
	bool header = false;
	std::optional<PreparedEditTableRowSource> editRow;
};

struct PreparedPhotoBlockData {
	PreparedMediaBlockId id;
	uint64 photoId = 0;
	int width = 0;
	int height = 0;
	QString urlOverride;
	TextWithEntities caption;
	bool spoiler = false;
	bool viewerOpen = false;
	bool editMode = false;
};

enum class PreparedMediaItemKind {
	Photo,
	Document,
};

struct PreparedMediaItemData {
	PreparedMediaItemKind kind = PreparedMediaItemKind::Photo;
	uint64 id = 0;
	int width = 0;
	int height = 0;
	bool spoiler = false;
};

struct PreparedVideoBlockData {
	PreparedMediaBlockId id;
	PreparedMediaItemData media;
	TextWithEntities caption;
	bool editMode = false;
};

struct PreparedAudioBlockData {
	PreparedMediaBlockId id;
	uint64 documentId = 0;
	QString title;
	QString performer;
	QString fileName;
	int duration = 0;
};

struct PreparedMapBlockData {
	PreparedMediaBlockId id;
	double latitude = 0.;
	double longitude = 0.;
	uint64 accessHash = 0;
	int width = 0;
	int height = 0;
	int zoom = 0;
	QString url;
};

struct PreparedChannelBlockData {
	PreparedMediaBlockId id;
	uint64 channelId = 0;
	QString title;
	QString username;
};

enum class PreparedGroupedMediaIntent {
	Collage,
	Slideshow,
};

struct PreparedGroupedMediaItemData {
	PreparedMediaBlockId id;
	PreparedMediaItemData media;
};

struct PreparedGroupedMediaBlockData {
	PreparedMediaBlockId id;
	PreparedGroupedMediaIntent intent = PreparedGroupedMediaIntent::Collage;
	std::vector<PreparedGroupedMediaItemData> items;
	TextWithEntities caption;
	bool editMode = false;
};

struct PreparedPlaceholderBlockData {
	PreparedPlaceholderBlockId id;
	QString label;
	QString copyText;
	std::optional<EmbedRequest> embed;
};

struct PreparedRelatedArticleBlockData {
	PreparedLink link;
	QString copyText;
	QString title;
	QString description;
	QString footer;
	uint64 photoId = 0;
};

struct PreparedEmbedPostBlockData {
	QString url;
	uint64 authorPhotoId = 0;
	QString author;
	QString dateText;
};

struct PreparedBlock {
	PreparedBlockKind kind = PreparedBlockKind::Paragraph;
	TextWithEntities text;
	std::vector<PreparedLink> links;
	std::vector<PreparedBlock> children;
	std::vector<PreparedTableRow> tableRows;
	std::vector<TableAlignment> tableAlignments;
	TableAlignment flowAlignment = TableAlignment::Left;
	QString codeLanguage;
	QString formulaTex;
	QString anchorId;
	std::vector<QString> anchorIds;
	PreparedPhotoBlockData photo;
	PreparedVideoBlockData video;
	PreparedAudioBlockData audio;
	PreparedMapBlockData map;
	PreparedChannelBlockData channel;
	PreparedGroupedMediaBlockData groupedMedia;
	PreparedEmbedPostBlockData embedPost;
	PreparedPlaceholderBlockData placeholder;
	PreparedRelatedArticleBlockData relatedArticle;
	ListKind listKind = ListKind::Bullet;
	ListDelimiter listDelimiter = ListDelimiter::None;
	MathKind mathKind = MathKind::Display;
	TaskState taskState = TaskState::None;
	PreparedOrderedListType orderedType = PreparedOrderedListType::Decimal;
	int headingLevel = 0;
	int formulaIndex = -1;
	int orderedNumber = 0;
	int startNumber = 1;
	int actualDepth = 0;
	int visualDepth = 0;
	int tableColumnCount = 0;
	bool tableBordered = true;
	bool tableStriped = false;
	bool collapsed = false;
	bool detailsOpen = false;
	bool depthClamped = false;
	bool tight = false;
	bool supplementary = false;
	bool pullquote = false;
	bool quoteAuthor = false;
	bool footer = false;
	bool forceTextSegment = false;
	bool orderedReversed = false;
	std::optional<PreparedEditBlockSource> editBlock;
	std::optional<PreparedEditListItemSource> editListItem;
	std::optional<PreparedEditLeafSource> editLeaf;
	QString articleOrderedMarkerText;
	QString orderedMarkerText;
	QString editPlaceholderText;
};

struct PreparedRenderDocument {
	std::vector<PreparedBlock> blocks;
};

struct PreparedFootnote {
	QString label;
	QString displayText;
	TextWithEntities text;
	std::vector<PreparedLink> links;
	std::vector<PreparedBlock> blocks;
};

struct MarkdownPrepareDimensions {
	int bodyTextSize = 0;
	std::array<int, 6> headingTextSizes = { 0, 0, 0, 0, 0, 0 };
	int tableHeaderTextSize = 0;
	int tableBodyTextSize = 0;
	int displayMathTextSize = 0;
	int displayMathMaxRenderWidth = 0;
	int displayMathMaxRenderHeight = 0;
};

struct MarkdownPrepareTableRenderLimits {
	int maxRows = 0;
	int maxColumns = 0;
	int maxCells = 0;
};

struct MarkdownPrepareLimits {
	MarkdownPrepareTableRenderLimits tableRender;
	MarkdownPrepareTableRenderLimits markdownTableRender;
	int visualListDepth = 0;
	int visualQuoteDepth = 0;
	int maxPreparedBlocks = 0;
};

enum class PrepareTerminalFailure {
	None,
	InvalidRequest,
	InvalidStyle,
	DocumentTooLarge,
	InternalError,
};

struct PrepareFailureStatus {
	PrepareTerminalFailure terminal = PrepareTerminalFailure::None;
	QString debugReason;

	[[nodiscard]] bool failed() const {
		return (terminal != PrepareTerminalFailure::None);
	}
};

struct PrepareDebugStats {
	int prepareMs = 0;
	int formulaMeasureMs = 0;
	int formulaRenderMs = 0;
	int sourceWarningCount = 0;
	int prepareWarningCount = 0;
	int formulaWarningCount = 0;
};

struct PreparedFormulaSlot {
	QString trimmedTex;
	MathKind kind = MathKind::Display;
	int textSize = 0;
	int renderWidthCap = 0;
	int renderHeightCap = 0;
	std::shared_ptr<const MeasuredFormula> measuredData;
	MeasuredFormula measured;
	bool present = false;
};

struct PrepareRequest {
	std::shared_ptr<const PreparedDocument> document;
	std::shared_ptr<MathRenderer> renderer;
	MarkdownPrepareDimensions dimensions;
	QString sourcePath;
};

struct NativeInstantViewPrepareRequest {
	std::shared_ptr<const Iv::RichPage> richPage;
	std::shared_ptr<MediaRuntime> mediaRuntime;
	std::optional<MarkdownPrepareDimensions> dimensionsOverride;
	std::optional<MarkdownPrepareTableRenderLimits> tableRenderLimits;
	bool editMode = false;
};

struct MarkdownArticleContent {
	PreparedRenderDocument blocks;
	std::vector<PreparedFootnote> footnotes;
	std::vector<PreparedFormulaSlot> formulas;
	std::shared_ptr<MediaRuntime> mediaRuntime;
	std::shared_ptr<const Iv::RichPage> richPage;
	bool editMode = false;
	PrepareFailureStatus failure;
	PrepareDebugStats debug;
};

enum class NativeInstantViewPrepareResultKind {
	Supported,
	Unsupported,
	Failure,
};

enum class NativeInstantViewLeafUpdateResult {
	Updated,
	Unsupported,
	Failed,
};

struct NativeInstantViewPrepareResult {
	NativeInstantViewPrepareResultKind kind
		= NativeInstantViewPrepareResultKind::Unsupported;
	MarkdownArticleContent content;
	QString debugReason;

	[[nodiscard]] bool supported() const {
		return (kind == NativeInstantViewPrepareResultKind::Supported);
	}

	[[nodiscard]] bool unsupported() const {
		return (kind == NativeInstantViewPrepareResultKind::Unsupported);
	}

	[[nodiscard]] bool failed() const {
		return (kind == NativeInstantViewPrepareResultKind::Failure);
	}
};

[[nodiscard]] const MarkdownPrepareTableRenderLimits &PrepareTableRenderLimitsForIv();
[[nodiscard]] MarkdownPrepareTableRenderLimits PrepareTableRenderLimitsForRichMessage(
	const RichMessageLimits &limits);
[[nodiscard]] auto PrepareMarkdownTableRenderLimitsForIv()
-> const MarkdownPrepareTableRenderLimits &;
[[nodiscard]] const MarkdownPrepareLimits &PrepareLimitsForIv();
[[nodiscard]] MarkdownPrepareDimensions CaptureMarkdownPrepareDimensions();
[[nodiscard]] MarkdownPrepareDimensions CaptureMarkdownPrepareDimensions(
	const style::Markdown &st);
[[nodiscard]] QString HeadingLevelLabel(int level);
[[nodiscard]] QString FormatPreparedOrderedRawMarkerText(
	const QString &raw,
	ListDelimiter delimiter);
[[nodiscard]] QString SerializeInlineTextObjectEntity(
	const InlineTextObjectEntity &object);
[[nodiscard]] QString InlineFormulaCopySource(const QString &source);
[[nodiscard]] MarkdownArticleContent PrepareSynchronously(PrepareRequest request);
[[nodiscard]] NativeInstantViewPrepareResult TryPrepareNativeInstantView(
	NativeInstantViewPrepareRequest request);
[[nodiscard]] NativeInstantViewLeafUpdateResult UpdatePreparedNativeInstantViewLeaf(
	MarkdownArticleContent *content,
	const RichPage &page,
	const PreparedEditLeafSource &source,
	std::optional<MarkdownPrepareTableRenderLimits> tableRenderLimits
		= std::nullopt);

} // namespace Iv::Markdown
