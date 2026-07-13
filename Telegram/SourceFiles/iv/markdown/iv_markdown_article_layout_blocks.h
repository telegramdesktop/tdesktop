/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_article.h"
#include "spellcheck/spellcheck_highlight_syntax.h"

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Ui {
class DynamicImage;
} // namespace Ui

namespace style {
struct st;
struct QuoteStyle;
struct TextStyle;
} // namespace style

namespace Iv::Markdown {

class InlineFormulaObjectCache;
class CodeBlockSyntaxHighlightTracker {
public:
	virtual ~CodeBlockSyntaxHighlightTracker() = default;

	[[nodiscard]] virtual Spellchecker::HighlightProcessId tryHighlightSyntax(
		const QString &displayText,
		const QString &language,
		TextWithEntities &marked) = 0;
};

struct LaidOutTableCell {
	Ui::Text::String leaf;
	QRect logicalOuter;
	QRect logicalTextRect;
	QString placeholderText;
	Ui::Text::String placeholderLeaf;
	QRect outer;
	QRect textRect;
	int textWidth = 0;

	// Content-dependent, survives geometry resets, recomputed when
	// the displayed leaf max width changes (so on content changes).
	int cachedPreferredWidth = -1;
	int cachedPreferredHeight = 0;
	bool header = false;
	PreparedTableCellVerticalAlignment verticalAlignment
		= PreparedTableCellVerticalAlignment::Top;
	style::align align = style::al_left;
	int column = 0;
	int colspan = 1;
	int rowspan = 1;
	int segmentIndex = -1;
	int tableSegmentIndex = -1;
	std::optional<PreparedEditTableCellSource> editCell;
	std::optional<PreparedEditLeafSource> editLeaf;
};

struct LaidOutTableRow {
	std::vector<LaidOutTableCell> cells;
	QRect logicalOuter;
	QRect outer;
	bool header = false;
	std::optional<PreparedEditTableRowSource> editRow;
};

struct LaidOutBlockLogicalGeometry {
	QRect outer;
	QRect headerRect;
	QRect bodyRect;
	QRect iconRect;
	QRect textRect;
	QRect labelRect;
	QRect subtitleRect;
	QRect actionRect;
	QRect markerRect;
	QRect contentRect;
	QRect formulaRect;
	QRect tableRect;
	QRect mediaRect;
	QRect thumbnailRect;
	QPoint markerCenter;
};

struct LaidOutBlock {
	PreparedBlockKind kind = PreparedBlockKind::Paragraph;
	Ui::Text::String leaf;
	QString placeholderText;
	Ui::Text::String placeholderLeaf;
	Ui::Text::String labelLeaf;
	Ui::Text::String subtitleLeaf;
	Ui::Text::String actionLeaf;
	Ui::Text::String marker;
	Ui::Text::String fallbackLeaf;
	QString copyText;
	TextWithEntities codeText;
	std::vector<PreparedLink> codeLinks;
	QString labelText;
	QString codeLanguage;
	std::optional<PreparedLink> preparedLink;
	ClickHandlerPtr preparedLinkHandler;
	PreparedPlaceholderBlockId placeholderId;
	Spellchecker::HighlightProcessId syntaxHighlightProcessId = 0;
	std::vector<LaidOutBlock> children;
	std::vector<LaidOutTableRow> tableRows;
	std::vector<int> tableColumnWidths;
	QRect outer;
	QRect headerRect;
	QRect bodyRect;
	QRect iconRect;
	QRect textRect;
	QRect labelRect;
	QRect subtitleRect;
	QRect actionRect;
	QRect markerRect;
	QRect contentRect;
	QRect formulaRect;
	QRect tableRect;
	QRect mediaRect;
	QRect thumbnailRect;
	QRect visibleFormulaRect;
	QRect scrollViewportRect;
	QRect scrollLogicalContentRect;
	QRect scrollScrollbarTrackRect;
	QRect scrollScrollbarThumbRect;
	QRect visibleTableRect;
	QRect tableScrollbarTrackRect;
	QRect tableScrollbarThumbRect;
	QRect visibleMediaRect;
	QPoint markerCenter;
	LaidOutBlockLogicalGeometry logicalGeometry;
	QString anchorId;
	std::vector<QString> anchorIds;
	int textWidth = 0;
	int labelWidth = 0;
	int subtitleWidth = 0;
	int actionWidth = 0;
	int markerWidth = 0;
	int firstLineBaseline = -1;
	int headingLevel = 0;
	ListKind listKind = ListKind::Bullet;
	ListDelimiter listDelimiter = ListDelimiter::None;
	TaskState taskState = TaskState::None;
	int formulaIndex = -1;
	int orderedNumber = 0;
	style::align flowTextAlign = style::al_left;
	style::align formulaAlign = style::al_left;
	bool collapsed = false;
	bool detailsOpen = false;
	bool overflowed = false;
	bool tableBordered = true;
	bool tableStriped = false;
	bool supplementary = false;
	bool pullquote = false;
	bool quoteAuthor = false;
	bool insideHorizontalScroll = false;
	int horizontalScrollLeft = 0;
	int horizontalScrollMax = 0;
	int horizontalScrollAncestorShift = 0;
	int segmentIndex = -1;
	int secondarySegmentIndex = -1;
	int tertiarySegmentIndex = -1;
	std::optional<PreparedEditBlockSource> editBlock;
	std::optional<PreparedEditListItemSource> editListItem;
	std::optional<PreparedEditLeafSource> editLeaf;
	std::shared_ptr<MediaBlock> mediaBlock;
	std::shared_ptr<PlaceholderBlockRuntime> placeholderRuntime;
	std::shared_ptr<TaskMarkerRippleRuntime> taskMarkerRippleRuntime;
	std::shared_ptr<PhotoRuntime> photoRuntime;
	MediaActivation activation;
	uint64 thumbnailPhotoId = 0;
	mutable std::shared_ptr<Ui::DynamicImage> thumbnailImage;
	mutable std::shared_ptr<Ui::DynamicImage> previousThumbnailImage;
	mutable std::shared_ptr<Ui::DynamicImage> subscribedThumbnailImage;
	mutable QSize thumbnailRequestSize;
	mutable std::shared_ptr<Ui::DynamicImage> fullImage;
	mutable std::shared_ptr<Ui::DynamicImage> previousFullImage;
	mutable std::shared_ptr<Ui::DynamicImage> subscribedFullImage;
	mutable QSize fullRequestSize;
	mutable QImage colorizedFormulaImage;
	mutable QColor colorizedFormulaColor;
	mutable QSize colorizedFormulaSize;
};

struct EditableHeightOverride {
	int editableIndex = -1;
	int height = 0;
	int nextEditableIndex = 0;
};

struct EditableMaxLineWidthOverride {
	PreparedEditLeafSource leaf;
	int width = 0;
};

struct EditableTextEmptyOverride {
	PreparedEditLeafSource leaf;
	bool empty = true;
};

enum class CachedTextLeafSlot {
	Leaf,
	Placeholder,
	Label,
	Subtitle,
	Action,
	Marker,
	Fallback,
	TableCellText,
	TableCellPlaceholder,
};

enum class CachedTextLeafIdentityKind {
	PreparedPath,
	EditBlock,
	EditListItem,
	EditTableCell,
	EditLeaf,
};

struct CachedTextLeafKey {
	CachedTextLeafSlot slot = CachedTextLeafSlot::Leaf;
	CachedTextLeafIdentityKind identityKind
		= CachedTextLeafIdentityKind::PreparedPath;
	PreparedEditBlockSource editBlock;
	PreparedEditListItemSource editListItem;
	PreparedEditTableCellSource editTableCell;
	PreparedEditLeafSource editLeaf;
	std::vector<int> preparedPath;
	int tableRowIndex = -1;
	int tableCellIndex = -1;

	friend inline bool operator==(
			const CachedTextLeafKey &a,
			const CachedTextLeafKey &b) {
		if (a.slot != b.slot || a.identityKind != b.identityKind) {
			return false;
		}
		switch (a.identityKind) {
		case CachedTextLeafIdentityKind::PreparedPath:
			return (a.preparedPath == b.preparedPath)
				&& (a.tableRowIndex == b.tableRowIndex)
				&& (a.tableCellIndex == b.tableCellIndex);
		case CachedTextLeafIdentityKind::EditBlock:
			return (a.editBlock == b.editBlock);
		case CachedTextLeafIdentityKind::EditListItem:
			return (a.editListItem == b.editListItem);
		case CachedTextLeafIdentityKind::EditTableCell:
			return (a.editTableCell == b.editTableCell);
		case CachedTextLeafIdentityKind::EditLeaf:
			return (a.editLeaf == b.editLeaf);
		}
		return false;
	}

	friend inline bool operator!=(
			const CachedTextLeafKey &a,
			const CachedTextLeafKey &b) {
		return !(a == b);
	}
};

struct CachedTextLeafKeyHasher {
	[[nodiscard]] size_t operator()(
		const CachedTextLeafKey &value) const noexcept;
};

struct CachedTextLeafSourceSignature {
	TextWithEntities text;
	QString codeLanguage;
	int minResizeWidth = 1;
	size_t styleKey = 0;
	bool dependsOnMediaRuntime = false;

	friend inline bool operator==(
		const CachedTextLeafSourceSignature &a,
		const CachedTextLeafSourceSignature &b) = default;
	friend inline bool operator!=(
		const CachedTextLeafSourceSignature &a,
		const CachedTextLeafSourceSignature &b) {
		return !(a == b);
	}
};

struct CachedTextLeafEntry {
	Ui::Text::String leaf;
	CachedTextLeafSourceSignature source;
	Spellchecker::HighlightProcessId syntaxHighlightProcessId = 0;
};

struct CachedTextLeafPool {
	std::unordered_map<
		CachedTextLeafKey,
		CachedTextLeafEntry,
		CachedTextLeafKeyHasher> entries;
};

struct LayoutContext {
	int listDepth = 0;
	int listItemDepth = 0;
	int listItemContentShift = 0;
	int quoteDepth = 0;
	int articleLeft = 0;
	int articleWidth = 0;
	double mediaPixelScale = 1.;
	bool tightList = false;
	bool useArticleBands = false;
	bool editMode = false;
	bool hideEmptyQuoteAuthor = false;
	bool allowAsyncSyntaxHighlighting = true;
	CodeBlockSyntaxHighlightTracker *syntaxHighlightTracker = nullptr;
	CachedTextLeafPool *cachedTextLeafs = nullptr;
	Fn<void()> repaint;
	Fn<void(QRect)> repaintRect;
	Fn<bool(const ClickContext&)> spoilerLinkFilter;
	std::vector<int> preparedPath;
	std::shared_ptr<EditableHeightOverride> editableHeightOverride;
	std::shared_ptr<EditableMaxLineWidthOverride>
		editableMaxLineWidthOverride;
	std::shared_ptr<EditableTextEmptyOverride> editableTextEmptyOverride;
	std::function<std::shared_ptr<MediaBlock>(const PreparedBlock&)> mediaBlockFactory;
	std::function<std::shared_ptr<PlaceholderBlockRuntime>(
		PreparedPlaceholderBlockId)> placeholderRuntimeFactory;
	std::function<std::shared_ptr<TaskMarkerRippleRuntime>(
		const PreparedEditListItemSource&)> taskMarkerRippleRuntimeFactory;
};

class LayoutContextScope {
public:
	explicit LayoutContextScope(const LayoutContext &context);
	~LayoutContextScope();

	LayoutContextScope(const LayoutContextScope &) = delete;
	LayoutContextScope &operator=(const LayoutContextScope &) = delete;

private:
	const LayoutContext *_previous = nullptr;
};

[[nodiscard]] bool IsAnchorOnlyBlock(const PreparedBlock &block);
[[nodiscard]] bool IsFlowKind(PreparedBlockKind kind);
[[nodiscard]] QString ListMarkerText(const PreparedBlock &block);
[[nodiscard]] int TextLineHeight(const style::TextStyle &style);
[[nodiscard]] int TextLineAscent(const style::TextStyle &style);
[[nodiscard]] int TextLineBaseline(
	const style::TextStyle &style,
	int top = 0);
[[nodiscard]] int ResolveEditableHeight(
	int naturalHeight,
	LayoutContext context);
[[nodiscard]] QPoint BulletMarkerCenter(
	int left,
	int baseline,
	const style::Markdown &st);
[[nodiscard]] QMargins BlockquotePadding(const style::QuoteStyle &style);
[[nodiscard]] int HorizontalMarginsWidth(QMargins margins);
[[nodiscard]] Ui::Text::GeometryDescriptor TextGeometry(int width);
[[nodiscard]] int TextMinResizeWidth(int width);
[[nodiscard]] int ReadableTextMinWidth(const style::TextStyle &style);
[[nodiscard]] int FlowTextMinResizeWidth(const style::TextStyle &style);
[[nodiscard]] int FlowBlockMinimumWidth(
	const PreparedBlock &prepared,
	const style::Markdown &st);
[[nodiscard]] int FlowBlockPreferredWidth(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	LayoutContext context = {});
[[nodiscard]] int CodeBlockMinimumWidth(const style::Markdown &st);
[[nodiscard]] int CodeTextMinResizeWidth(const style::Markdown &st);
[[nodiscard]] int CodeBlockPreferredWidth(
	const PreparedBlock &prepared,
	const style::Markdown &st,
	LayoutContext context = {});
[[nodiscard]] int DisplayMathMinimumWidth(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	const style::Markdown &st);
[[nodiscard]] int DisplayMathPreferredWidth(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	const style::Markdown &st,
	LayoutContext context = {});
[[nodiscard]] int PlainTextMinResizeWidth(const style::TextStyle &style);
[[nodiscard]] int DisplayMathFallbackTextMinResizeWidth(
	const style::Markdown &st);
[[nodiscard]] int ContainerMinimumWidth(
	int contentMinimumWidth,
	QMargins padding);
[[nodiscard]] int TableMinimumGridWidth(
	int columnCount,
	const style::Markdown &st,
	bool bordered);
[[nodiscard]] int TableCellTextMinResizeWidth(
	const style::TextStyle &textStyle,
	const style::Markdown &st);

// Minimal width a text leaf can be laid out in without overflowing,
// counting wide inline objects (custom emoji / inline formulas).
[[nodiscard]] int LeafMinimumWidth(const Ui::Text::String &leaf);

struct TableCellMinimumWidthConstraint {
	int column = 0;
	int colspan = 1;
	int minimumWidth = 0; // Outer width, including cell padding.
};
[[nodiscard]] std::vector<int> ComputeTableColumnMinimumWidths(
	std::vector<TableCellMinimumWidthConstraint> constraints,
	int columnCount,
	const style::Markdown &st,
	bool bordered);
[[nodiscard]] int TableGridWidth(
	const std::vector<int> &columnWidths,
	const style::Markdown &st,
	bool bordered);
[[nodiscard]] int FlowBlockContentMinimumWidth(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	LayoutContext context = {});
[[nodiscard]] int DetailsSummaryContentMinimumWidth(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	LayoutContext context = {});
[[nodiscard]] int TableBlockContentMinimumWidth(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	LayoutContext context = {});
[[nodiscard]] int RetainedTableBlockMinimumWidth(
	const PreparedBlock &prepared,
	const LaidOutBlock &block,
	const style::Markdown &st);
[[nodiscard]] QString CodeBlockDisplayText(const QString &text);
[[nodiscard]] TextWithEntities CodeBlockDisplayText(TextWithEntities text);
[[nodiscard]] int BlockSkip(
	const PreparedBlock &block,
	const style::Markdown &st);
[[nodiscard]] int BlockSkip(
	const PreparedBlock &previous,
	const PreparedBlock &block,
	LayoutContext context,
	const style::Markdown &st);
[[nodiscard]] const style::TextStyle &TextStyleFor(
	const PreparedBlock &block,
	const style::Markdown &st);
[[nodiscard]] const style::TextStyle &EditPlaceholderTextStyleFor(
	const PreparedBlock &block,
	const style::Markdown &st);
void CopyCachedTextLeafs(
	const std::vector<PreparedBlock> &preparedBlocks,
	std::vector<LaidOutBlock> *blocks,
	const style::Markdown &st,
	CachedTextLeafPool *pool);
void BuildOrReuseMarkedTextLeaf(
	Ui::Text::String *leaf,
	CachedTextLeafSlot slot,
	const PreparedBlock &prepared,
	const style::TextStyle &textStyle,
	const style::Markdown &st,
	const TextWithEntities &text,
	const std::vector<PreparedLink> &links,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	int minResizeWidth,
	LayoutContext context = {});
void BuildOrReusePlainTextLeaf(
	Ui::Text::String *leaf,
	CachedTextLeafSlot slot,
	const PreparedBlock &prepared,
	const style::TextStyle &textStyle,
	const QString &text,
	int minResizeWidth,
	LayoutContext context = {});
void BuildOrReuseEditPlaceholderLeaf(
	QString *placeholderText,
	Ui::Text::String *placeholderLeaf,
	const PreparedBlock &prepared,
	const QString &text,
	const style::TextStyle &textStyle,
	int minResizeWidth,
	LayoutContext context = {});
void ApplyPreparedEditSources(
	LaidOutBlock *block,
	const PreparedBlock &prepared);
void LayoutMediaCaption(
	LaidOutBlock *block,
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int skip,
	int *bottom,
	LayoutContext context = {});
void FillMediaCaptionContent(
	LaidOutBlock *block,
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	LayoutContext context = {});
[[nodiscard]] bool LayoutMediaCaptionGeometry(
	LaidOutBlock *block,
	const PreparedBlock &prepared,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int skip,
	int *bottom,
	LayoutContext context = {});
void RepopulateCodeBlockLeaf(
	LaidOutBlock &block,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	bool allowAsyncSyntaxHighlighting,
	CodeBlockSyntaxHighlightTracker *syntaxHighlightTracker = nullptr,
	Fn<void()> repaint = nullptr,
	Fn<void(QRect)> repaintRect = nullptr,
	Fn<bool(const ClickContext&)> spoilerLinkFilter = nullptr);
void UpdateLaidOutLeafContent(
	LaidOutBlock *block,
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	LayoutContext context = {});
void UpdateLaidOutLeafContent(
	LaidOutTableCell *cell,
	const PreparedTableCell &prepared,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int tableRowIndex,
	int tableCellIndex,
	LayoutContext context = {});

[[nodiscard]] LaidOutBlock LayoutFlowBlock(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutCodeBlock(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	bool allowAsyncSyntaxHighlighting,
	CodeBlockSyntaxHighlightTracker *syntaxHighlightTracker = nullptr,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutRuleBlock(
	const PreparedBlock &prepared,
	const style::Markdown &st,
	int left,
	int top,
	int width);
[[nodiscard]] LaidOutBlock LayoutDisplayMathBlock(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutTableBlock(
	const PreparedBlock &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutPlaceholderBlock(
	const PreparedBlock &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutRelatedArticleBlock(
	const PreparedBlock &prepared,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutPhotoBlock(
	const PreparedBlock &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutVideoBlock(
	const PreparedBlock &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutAudioBlock(
	const PreparedBlock &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutMapBlock(
	const PreparedBlock &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutChannelBlock(
	const PreparedBlock &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context = {});
[[nodiscard]] LaidOutBlock LayoutGroupedMediaBlock(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> *formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context = {});
[[nodiscard]] std::optional<int> RecountSimpleLaidOutBlock(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	LaidOutBlock *block,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	LayoutContext context = {});

} // namespace Iv::Markdown
