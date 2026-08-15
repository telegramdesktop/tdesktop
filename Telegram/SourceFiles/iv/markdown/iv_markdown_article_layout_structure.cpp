/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_layout_structure.h"
#include "iv/markdown/iv_markdown_article_text.h"

#include "lang/lang_keys.h"
#include "styles/style_iv.h"

#include <algorithm>

namespace Iv::Markdown {
namespace {

[[nodiscard]] int LeafFirstLineBaseline(
		const Ui::Text::String &leaf,
		const QRect &textRect,
		const style::TextStyle &style) {
	const auto lines = leaf.countLinesGeometry(textRect.width());
	return textRect.y() + (lines.empty()
		? TextLineBaseline(style)
		: lines.front().baseline);
}

[[nodiscard]] int MarkdownBodyBaseline(
		int top,
		const style::Markdown &st) {
	return TextLineBaseline(st.body, top);
}

[[nodiscard]] int BlockBottom(const LaidOutBlock &block) {
	return block.outer.y() + block.outer.height();
}

[[nodiscard]] bool UsesMediaBand(PreparedBlockKind kind) {
	switch (kind) {
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::Details:
		return true;
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::DisplayMath:
	case PreparedBlockKind::Table:
	case PreparedBlockKind::Placeholder:
	case PreparedBlockKind::EmbedPost:
		return false;
	}
	return false;
}

[[nodiscard]] QRect PaddedBand(
		int left,
		int width,
		QMargins padding) {
	return QRect(
		left + padding.left(),
		0,
		std::max(width - padding.left() - padding.right(), 1),
		0);
}

[[nodiscard]] int PaddedWidth(
		int width,
		QMargins padding) {
	return std::max(width - padding.left() - padding.right(), 1);
}

[[nodiscard]] int PullquoteIconReserveWidth(
		const style::QuoteStyle &style) {
	return style.icon.empty()
		? 0
		: (style.icon.width() + style.iconPosition.x());
}

void RefreshLogicalGeometry(LaidOutBlock *block);

[[nodiscard]] int FlowBlockVisibleTextWidth(
		const LaidOutBlock &block,
		const EditableMaxLineWidthOverride *override) {
	if (override
		&& block.editLeaf
		&& (*block.editLeaf == override->leaf)) {
		return std::min(
			std::max(override->width, 1),
			std::max(block.textRect.width(), 1));
	}
	const auto &leaf = block.placeholderLeaf.isEmpty()
		? block.leaf
		: block.placeholderLeaf;
	return (leaf.isEmpty() || block.textRect.isEmpty())
		? 0
		: std::min(leaf.maxWidth(), block.textRect.width());
}

[[nodiscard]] bool CanTightenPullquoteChild(const LaidOutBlock &block) {
	return (block.kind == PreparedBlockKind::Paragraph)
		&& !block.insideHorizontalScroll
		&& (block.horizontalScrollMax <= 0)
		&& block.scrollViewportRect.isEmpty()
		&& block.scrollLogicalContentRect.isEmpty();
}

[[nodiscard]] int TightPullquoteTextWidth(
		const std::vector<LaidOutBlock> &children,
		const EditableMaxLineWidthOverride *override) {
	auto result = 0;
	for (const auto &child : children) {
		if (!CanTightenPullquoteChild(child)) {
			return 0;
		}
		result = std::max(result, FlowBlockVisibleTextWidth(child, override));
	}
	return result;
}

void RecenterPullquoteChild(
		LaidOutBlock *block,
		int left,
		int width) {
	if (!block) {
		return;
	}
	block->textWidth = width;
	block->textRect.setX(left);
	block->textRect.setWidth(width);
	block->contentRect.setX(left);
	block->contentRect.setWidth(width);
	block->outer.setX(left);
	block->outer.setWidth(width);
	block->overflowed = false;
	block->horizontalScrollMax = 0;
	block->scrollViewportRect = QRect();
	block->scrollLogicalContentRect = QRect();
	block->scrollScrollbarTrackRect = QRect();
	block->scrollScrollbarThumbRect = QRect();
	RefreshLogicalGeometry(block);
}

[[nodiscard]] QRect BlockBand(
		PreparedBlockKind kind,
		const style::Markdown &st,
		int left,
		int width,
		LayoutContext context) {
	if (!context.useArticleBands) {
		return QRect(left, 0, std::max(width, 1), 0);
	}
	return UsesMediaBand(kind)
		? PaddedBand(left, width, st.mediaPadding)
		: PaddedBand(left, width, st.textPadding);
}

[[nodiscard]] int BlockBandWidth(
		PreparedBlockKind kind,
		const style::Markdown &st,
		int width,
		LayoutContext context) {
	if (!context.useArticleBands) {
		return std::max(width, 1);
	}
	return UsesMediaBand(kind)
		? PaddedWidth(width, st.mediaPadding)
		: PaddedWidth(width, st.textPadding);
}

[[nodiscard]] bool IsRelatedArticlesHeader(
		const PreparedBlock &block,
		const PreparedBlock *next) {
	return next
		&& (block.kind == PreparedBlockKind::Heading)
		&& (next->kind == PreparedBlockKind::RelatedArticle);
}

[[nodiscard]] const PreparedBlock *NextVisibleBlock(
		const std::vector<PreparedBlock> &blocks,
		int index) {
	for (auto i = index + 1, count = int(blocks.size()); i != count; ++i) {
		if (!IsAnchorOnlyBlock(blocks[i])) {
			return &blocks[i];
		}
	}
	return nullptr;
}

void PrepareNestedContext(
		LayoutContext *context,
		int left,
		int width) {
	context->useArticleBands = false;
	context->articleLeft = left;
	context->articleWidth = std::max(width, 1);
	context->listItemContentShift = 0;
}

[[nodiscard]] bool TextEmptyForLayout(
		const PreparedBlock &block,
		LayoutContext context) {
	return (block.editLeaf
		&& context.editableTextEmptyOverride
		&& (*block.editLeaf == context.editableTextEmptyOverride->leaf))
		? context.editableTextEmptyOverride->empty
		: block.text.text.isEmpty();
}

[[nodiscard]] bool QuoteBodyEmptyForLayout(
		const PreparedBlock &block,
		LayoutContext context) {
	for (const auto &child : block.children) {
		if (child.quoteAuthor || IsAnchorOnlyBlock(child)) {
			continue;
		} else if (IsFlowKind(child.kind)
			&& TextEmptyForLayout(child, context)
			&& child.children.empty()) {
			continue;
		}
		return false;
	}
	return true;
}

[[nodiscard]] bool HideEmptyQuoteAuthorBlock(
		const PreparedBlock &block,
		LayoutContext context) {
	return context.hideEmptyQuoteAuthor
		&& block.quoteAuthor
		&& TextEmptyForLayout(block, context);
}

[[nodiscard]] style::align FlowTextAlign(TableAlignment alignment) {
	switch (alignment) {
	case TableAlignment::Center:
		return style::al_center;
	case TableAlignment::Right:
		return style::al_right;
	case TableAlignment::None:
	case TableAlignment::Left:
		return style::al_left;
	}
	return style::al_left;
}

[[nodiscard]] LaidOutBlock HiddenQuoteAuthorBlock(
		const PreparedBlock &prepared) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = prepared.kind;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.headingLevel = prepared.headingLevel;
	block.supplementary = prepared.supplementary;
	block.pullquote = prepared.pullquote;
	block.quoteAuthor = prepared.quoteAuthor;
	block.footer = prepared.footer;
	block.flowTextAlign = FlowTextAlign(prepared.flowAlignment);
	return block;
}

[[nodiscard]] bool FirstLineComesFromChildren(const LaidOutBlock &block) {
	switch (block.kind) {
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::EmbedPost:
		return true;
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::DisplayMath:
	case PreparedBlockKind::Table:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Placeholder:
	case PreparedBlockKind::Details:
		return false;
	}
	return false;
}

[[nodiscard]] int ResolveFirstDisplayedLineBaseline(
		const LaidOutBlock &block,
		const style::Markdown &st) {
	if (block.firstLineBaseline >= 0) {
		return block.firstLineBaseline;
	}
	if (FirstLineComesFromChildren(block)) {
		for (const auto &child : block.children) {
			if (child.outer.height() <= 0) {
				continue;
			}
			return ResolveFirstDisplayedLineBaseline(child, st);
		}
	}
	return MarkdownBodyBaseline(block.outer.y(), st);
}

void RefreshLogicalGeometry(LaidOutBlock *block) {
	block->logicalGeometry = {
		.outer = block->outer,
		.headerRect = block->headerRect,
		.bodyRect = block->bodyRect,
		.iconRect = block->iconRect,
		.textRect = block->textRect,
		.labelRect = block->labelRect,
		.subtitleRect = block->subtitleRect,
		.actionRect = block->actionRect,
		.markerRect = block->markerRect,
		.contentRect = block->contentRect,
		.formulaRect = block->formulaRect,
		.tableRect = block->tableRect,
		.mediaRect = block->mediaRect,
		.thumbnailRect = block->thumbnailRect,
		.markerCenter = block->markerCenter,
	};
}

void ClearBlockGeometry(LaidOutBlock *block) {
	if (!block) {
		return;
	}
	block->outer = QRect();
	block->headerRect = QRect();
	block->bodyRect = QRect();
	block->iconRect = QRect();
	block->textRect = QRect();
	block->labelRect = QRect();
	block->subtitleRect = QRect();
	block->actionRect = QRect();
	block->markerRect = QRect();
	block->contentRect = QRect();
	block->formulaRect = QRect();
	block->tableRect = QRect();
	block->mediaRect = QRect();
	block->thumbnailRect = QRect();
	block->visibleFormulaRect = QRect();
	block->scrollViewportRect = QRect();
	block->scrollLogicalContentRect = QRect();
	block->scrollScrollbarTrackRect = QRect();
	block->scrollScrollbarThumbRect = QRect();
	block->visibleTableRect = QRect();
	block->tableScrollbarTrackRect = QRect();
	block->tableScrollbarThumbRect = QRect();
	block->visibleMediaRect = QRect();
	block->markerCenter = QPoint();
	block->logicalGeometry = {};
	block->textWidth = 0;
	block->labelWidth = 0;
	block->subtitleWidth = 0;
	block->actionWidth = 0;
	block->markerWidth = 0;
	block->firstLineBaseline = -1;
	block->overflowed = false;
	block->insideHorizontalScroll = false;
	block->horizontalScrollMax = 0;
	block->horizontalScrollAncestorShift = 0;
}

[[nodiscard]] int RetainedLeafHeight(
		const Ui::Text::String &leaf,
		const style::TextStyle &textStyle,
		int width) {
	return std::max(
		leaf.countHeight(width),
		TextLineHeight(textStyle));
}

struct WidthAnalysisNode {
	std::vector<WidthAnalysisNode> children;
	int contentMinimumWidth = 1;
	int contentPreferredWidth = 1;
	int outerMinimumWidth = 1;
	int outerPreferredWidth = 1;
	int scrollOwnerMinimumWidth = 1;
	int scrollOwnerOverflowWidth = 1;
	int scrollViewportMinimumWidth = 1;
	int outerScrollViewportMinimumWidth = 1;
	bool ownerEligible = false;
	bool chosenScrollOwner = false;
	bool subtreeNeedsScrollOwner = false;
	bool subtreeHasChildMovingScrollOwner = false;
	bool subtreeHasScrollOwner = false;
};

[[nodiscard]] int MaxChildOuterMinimumWidth(
		const std::vector<WidthAnalysisNode> &children) {
	auto result = 1;
	for (const auto &child : children) {
		result = std::max(result, child.outerMinimumWidth);
	}
	return result;
}

[[nodiscard]] int MaxChildOuterPreferredWidth(
		const std::vector<WidthAnalysisNode> &children) {
	auto result = 1;
	for (const auto &child : children) {
		result = std::max(result, child.outerPreferredWidth);
	}
	return result;
}

[[nodiscard]] int MaxUnresolvedChildOuterScrollViewportMinimumWidth(
		const std::vector<WidthAnalysisNode> &children) {
	auto result = 1;
	for (const auto &child : children) {
		if (child.subtreeNeedsScrollOwner) {
			result = std::max(result, child.outerScrollViewportMinimumWidth);
		}
	}
	return result;
}

void FinalizeOwnerSelection(
		WidthAnalysisNode *analysis,
		int availableWidth,
		int visibleViewportWidth,
		bool ownerMovesChildren) {
	auto childNeedsScrollOwner = false;
	auto childHasChildMovingScrollOwner = false;
	auto childHasScrollOwner = false;
	for (const auto &child : analysis->children) {
		childNeedsScrollOwner |= child.subtreeNeedsScrollOwner;
		childHasChildMovingScrollOwner
			|= child.subtreeHasChildMovingScrollOwner;
		childHasScrollOwner |= child.subtreeHasScrollOwner;
	}
	const auto nodeOverflows = analysis->scrollOwnerOverflowWidth
		> std::max(availableWidth, 1);
	const auto subtreeNeedsScrollOwner = childNeedsScrollOwner
		|| (!childHasChildMovingScrollOwner && nodeOverflows);
	const auto viewportIsReadable = std::max(visibleViewportWidth, 1)
		>= std::max(analysis->scrollViewportMinimumWidth, 1);
	analysis->chosenScrollOwner = subtreeNeedsScrollOwner
		&& analysis->ownerEligible
		&& viewportIsReadable;
	analysis->subtreeNeedsScrollOwner = subtreeNeedsScrollOwner
		&& !analysis->chosenScrollOwner;
	analysis->subtreeHasChildMovingScrollOwner
		= childHasChildMovingScrollOwner
		|| (analysis->chosenScrollOwner && ownerMovesChildren);
	analysis->subtreeHasScrollOwner = childHasScrollOwner
		|| analysis->chosenScrollOwner;
}

[[nodiscard]] bool AnalysisOwnerMovesChildren(PreparedBlockKind kind) {
	switch (kind) {
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::EmbedPost:
	case PreparedBlockKind::Details:
		return true;
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::DisplayMath:
	case PreparedBlockKind::Table:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Placeholder:
		return false;
	}
	return false;
}

[[nodiscard]] int ReadableScrollViewportMinimumWidth(
		int contentMaxWidth,
		const style::Markdown &st) {
	return std::min(
		std::max(contentMaxWidth, 1),
		std::max(st.quoteReadableMinWidth, 1));
}

[[nodiscard]] int NestedScrollViewportMinimumWidth(
		LayoutContext context,
		int contentMaxWidth,
		const style::Markdown &st) {
	return (context.quoteDepth > 0 || context.listItemDepth > 0)
		? ReadableScrollViewportMinimumWidth(contentMaxWidth, st)
		: 1;
}

[[nodiscard]] int OrderedMarkerMinimumWidth(
		const PreparedBlock &prepared,
		const style::Markdown &st) {
	return std::max(st.body.font->width(ListMarkerText(prepared)), 1);
}

[[nodiscard]] int ListItemMarkerMinimumWidth(
		const PreparedBlock &prepared,
		const style::Markdown &st) {
	if (prepared.taskState != TaskState::None) {
		return std::max(st.list.taskCheck.diameter, 1);
	} else if (prepared.listKind == ListKind::Ordered) {
		return OrderedMarkerMinimumWidth(prepared, st);
	}
	return std::max(st.list.markerWidth, 1);
}

[[nodiscard]] QMargins DetailsHeaderPadding(
		LayoutContext context,
		const style::Markdown &st) {
	const auto &details = st.details;
	return context.useArticleBands
		? QMargins(
			st.textPadding.left(),
			details.headerPadding.top(),
			st.textPadding.right(),
			details.headerPadding.bottom())
		: details.headerPadding;
}

[[nodiscard]] QString DetailsStateText(bool open) {
	return open
		? tr::lng_iv_details_state_expanded(tr::now)
		: tr::lng_iv_details_state_collapsed(tr::now);
}

[[nodiscard]] int DetailsStateReserveWidth(const style::Markdown &st) {
	return std::max({
		st.details.summaryStyle.font->width(DetailsStateText(false)),
		st.details.summaryStyle.font->width(DetailsStateText(true)),
		1,
	});
}

[[nodiscard]] QMargins DetailsBodyPadding(
		LayoutContext context,
		const style::Markdown &st) {
	const auto &details = st.details;
	return context.useArticleBands
		? QMargins(
			st.textPadding.left(),
			details.bodyPadding.top(),
			st.textPadding.right(),
			details.bodyPadding.bottom())
		: details.bodyPadding;
}

[[nodiscard]] const WidthAnalysisNode *NextActiveScrollOwner(
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner) {
	return activeScrollOwner
		? activeScrollOwner
		: analysis.chosenScrollOwner
		? &analysis
		: nullptr;
}

[[nodiscard]] bool IsActiveScrollOwner(
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner) {
	return (activeScrollOwner == &analysis);
}

[[nodiscard]] int LogicalOuterWidth(
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		int logicalWidth) {
	const auto result = std::max(logicalWidth, 1);
	return IsActiveScrollOwner(analysis, activeScrollOwner)
		? std::max(result, analysis.scrollOwnerMinimumWidth)
		: result;
}

[[nodiscard]] int ChildLayoutWidth(
		const WidthAnalysisNode *activeScrollOwner,
		int visibleWidth,
		int logicalWidth) {
	return activeScrollOwner
		? std::max(logicalWidth, 1)
		: std::max(visibleWidth, 1);
}

[[nodiscard]] int ScrollbarReserveHeight(
		bool scrollOwner,
		int horizontalScrollMax,
		const style::Markdown &st) {
	return (scrollOwner && (horizontalScrollMax > 0))
		? (st.table.scrollbarSkip + st.table.scrollbarHeight)
		: 0;
}

[[nodiscard]] WidthAnalysisNode AnalyzeBlock(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st,
	int width,
	LayoutContext context);

[[nodiscard]] std::vector<WidthAnalysisNode> AnalyzeBlocks(
		const std::vector<PreparedBlock> &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int width,
		LayoutContext context) {
	auto result = std::vector<WidthAnalysisNode>();
	result.reserve(prepared.size());
	for (auto i = 0, count = int(prepared.size()); i != count; ++i) {
		const auto &block = prepared[i];
		const auto next = NextVisibleBlock(prepared, i);
		auto blockContext = context;
		blockContext.preparedPath.push_back(i);
		auto blockWidth = BlockBandWidth(block.kind, st, width, context);
		if (IsRelatedArticlesHeader(block, next)) {
			blockWidth = std::max(
				width
					- st.relatedArticle.headerPadding.left()
					- st.relatedArticle.headerPadding.right(),
				1);
		}
		result.push_back(AnalyzeBlock(
			block,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			blockWidth,
			blockContext));
	}
	return result;
}

[[nodiscard]] WidthAnalysisNode AnalyzeBlock(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int width,
		LayoutContext context) {
	auto analysis = WidthAnalysisNode();
	if (HideEmptyQuoteAuthorBlock(prepared, context)) {
		return analysis;
	}
	const auto availableWidth = std::max(width, 1);
	auto visibleScrollViewportWidth = availableWidth;
	switch (prepared.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
		analysis.contentMinimumWidth = FlowBlockContentMinimumWidth(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			context);
		analysis.contentPreferredWidth = FlowBlockPreferredWidth(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			context);
		analysis.outerMinimumWidth = analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		analysis.scrollViewportMinimumWidth
			= NestedScrollViewportMinimumWidth(
				context,
				analysis.contentPreferredWidth,
				st);
		analysis.outerScrollViewportMinimumWidth = std::max(
			analysis.outerMinimumWidth,
			analysis.scrollViewportMinimumWidth);
		analysis.scrollOwnerOverflowWidth
			= (analysis.scrollViewportMinimumWidth > 1)
			? analysis.outerScrollViewportMinimumWidth
			: analysis.outerMinimumWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerMinimumWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = !IsAnchorOnlyBlock(prepared);
		break;
	case PreparedBlockKind::CodeBlock: {
		const auto padding = BlockquotePadding(st.code.pre);
		visibleScrollViewportWidth = availableWidth
			- HorizontalMarginsWidth(padding);
		analysis.contentMinimumWidth = CodeBlockMinimumWidth(st);
		analysis.contentPreferredWidth = CodeBlockPreferredWidth(
			prepared,
			st,
			context);
		analysis.outerMinimumWidth = analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		const auto contentMaxWidth = std::max(
			analysis.contentPreferredWidth - HorizontalMarginsWidth(padding),
			1);
		analysis.scrollViewportMinimumWidth
			= NestedScrollViewportMinimumWidth(
				context,
				contentMaxWidth,
				st);
		analysis.outerScrollViewportMinimumWidth
			= std::max(
				HorizontalMarginsWidth(padding)
					+ analysis.scrollViewportMinimumWidth,
				analysis.outerPreferredWidth);
		analysis.scrollOwnerOverflowWidth
			= (analysis.scrollViewportMinimumWidth > 1)
			? std::max(
				analysis.outerMinimumWidth,
				analysis.outerPreferredWidth)
			: analysis.outerMinimumWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerMinimumWidth,
			analysis.outerPreferredWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = true;
	} break;
	case PreparedBlockKind::DisplayMath: {
		const auto &padding = st.displayMath.padding;
		visibleScrollViewportWidth = availableWidth
			- HorizontalMarginsWidth(padding);
		analysis.contentMinimumWidth = DisplayMathMinimumWidth(
			prepared,
			formulas,
			st);
		analysis.contentPreferredWidth = DisplayMathPreferredWidth(
			prepared,
			formulas,
			st,
			context);
		analysis.outerMinimumWidth = analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		const auto contentMaxWidth = std::max(
			analysis.contentPreferredWidth - HorizontalMarginsWidth(padding),
			1);
		analysis.scrollViewportMinimumWidth
			= NestedScrollViewportMinimumWidth(
				context,
				contentMaxWidth,
				st);
		analysis.outerScrollViewportMinimumWidth
			= std::max(
				HorizontalMarginsWidth(padding)
					+ analysis.scrollViewportMinimumWidth,
				analysis.outerPreferredWidth);
		analysis.scrollOwnerOverflowWidth
			= (analysis.scrollViewportMinimumWidth > 1)
			? std::max(
				analysis.outerMinimumWidth,
				analysis.outerPreferredWidth)
			: analysis.outerMinimumWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerMinimumWidth,
			analysis.outerPreferredWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = true;
	} break;
	case PreparedBlockKind::Table:
		analysis.contentMinimumWidth = TableBlockContentMinimumWidth(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			context);
		analysis.contentPreferredWidth = analysis.contentMinimumWidth;
		analysis.outerMinimumWidth = analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		analysis.scrollViewportMinimumWidth
			= NestedScrollViewportMinimumWidth(
				context,
				analysis.contentPreferredWidth,
				st);
		analysis.outerScrollViewportMinimumWidth = std::max(
			analysis.outerMinimumWidth,
			analysis.scrollViewportMinimumWidth);
		analysis.scrollOwnerOverflowWidth
			= (analysis.scrollViewportMinimumWidth > 1)
			? analysis.outerScrollViewportMinimumWidth
			: analysis.outerMinimumWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerMinimumWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = true;
		break;
	case PreparedBlockKind::List: {
		const auto depthDelta = std::max(
			prepared.visualDepth - context.listDepth,
			0);
		const auto markerColumn = context.listItemContentShift;
		const auto step = std::max(st.textPadding.left(), markerColumn);
		const auto overhead = depthDelta * step - markerColumn;
		const auto childWidth = std::max(availableWidth - overhead, 1);
		visibleScrollViewportWidth = childWidth;
		auto childContext = context;
		childContext.listDepth = prepared.visualDepth;
		childContext.tightList = false;
		PrepareNestedContext(&childContext, 0, childWidth);
		analysis.children = AnalyzeBlocks(
			prepared.children,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			childWidth,
			childContext);
		const auto childRequiredWidth
			= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
				analysis.children);
		const auto childNeedsScrollOwner = (childRequiredWidth > 1);
		analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(analysis.children);
		analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
			analysis.children);
		analysis.outerMinimumWidth = overhead + analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = overhead + analysis.contentPreferredWidth;
		const auto naturalOverflowWidth = overhead
			+ analysis.contentMinimumWidth;
		const auto requiredOverflowWidth = overhead
			+ std::max(analysis.contentMinimumWidth, childRequiredWidth);
		analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
			? ReadableScrollViewportMinimumWidth(
				analysis.contentPreferredWidth,
				st)
			: 1;
		analysis.outerScrollViewportMinimumWidth = overhead
			+ (childNeedsScrollOwner ? childRequiredWidth : 1);
		analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
			? requiredOverflowWidth
			: naturalOverflowWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerOverflowWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = !prepared.children.empty();
	} break;
	case PreparedBlockKind::ListItem: {
		const auto markerWidth = std::max(
			st.list.markerWidth,
			ListItemMarkerMinimumWidth(prepared, st));
		const auto overhead = markerWidth + st.list.markerSkip;
		const auto childWidth = std::max(availableWidth - overhead, 1);
		visibleScrollViewportWidth = childWidth;
		auto childContext = context;
		childContext.tightList = false;
		PrepareNestedContext(&childContext, 0, childWidth);
		childContext.listItemDepth = context.listItemDepth + 1;
		childContext.listItemContentShift = overhead;
		analysis.children = AnalyzeBlocks(
			prepared.children,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			childWidth,
			childContext);
		const auto childRequiredWidth
			= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
				analysis.children);
		const auto childNeedsScrollOwner = (childRequiredWidth > 1);
		analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(analysis.children);
		analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
			analysis.children);
		analysis.outerMinimumWidth = overhead + analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = overhead + analysis.contentPreferredWidth;
		const auto naturalOverflowWidth = overhead
			+ analysis.contentMinimumWidth;
		const auto requiredOverflowWidth = overhead
			+ std::max(analysis.contentMinimumWidth, childRequiredWidth);
		analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
			? ReadableScrollViewportMinimumWidth(
				analysis.contentPreferredWidth,
				st)
			: 1;
		analysis.outerScrollViewportMinimumWidth = overhead
			+ (childNeedsScrollOwner ? childRequiredWidth : 1);
		analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
			? requiredOverflowWidth
			: naturalOverflowWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerOverflowWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = !prepared.children.empty();
	} break;
	case PreparedBlockKind::Quote: {
		const auto depthDelta = std::max(
			prepared.visualDepth - context.quoteDepth,
			0);
		const auto overhead = depthDelta * st.quoteIndent;
		const auto padding = prepared.pullquote
			? st.pullquote.padding
			: BlockquotePadding(st.body.blockquote);
		const auto paddingWidth = HorizontalMarginsWidth(padding);
		const auto pullquoteReserveWidth = prepared.pullquote
			? (2 * PullquoteIconReserveWidth(st.body.blockquote))
			: 0;
		auto childWidth = std::max(
			availableWidth - overhead - paddingWidth - pullquoteReserveWidth,
			1);
		if (prepared.pullquote) {
			childWidth = std::min(childWidth, std::max(st.pullquote.maxWidth, 1));
		}
		visibleScrollViewportWidth = childWidth;
		auto childContext = context;
		childContext.quoteDepth = prepared.visualDepth + 1;
		childContext.tightList = false;
		PrepareNestedContext(&childContext, 0, childWidth);
		childContext.hideEmptyQuoteAuthor = QuoteBodyEmptyForLayout(
			prepared,
			childContext);
		analysis.children = AnalyzeBlocks(
			prepared.children,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			childWidth,
			childContext);
		const auto childRequiredWidth
			= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
				analysis.children);
		const auto childNeedsScrollOwner = (childRequiredWidth > 1);
		analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(analysis.children);
		analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
			analysis.children);
		const auto visibleContentWidth = prepared.pullquote
			? std::min(
				analysis.contentMinimumWidth,
				std::max(st.pullquote.maxWidth, 1))
			: analysis.contentMinimumWidth;
		const auto visiblePreferredContentWidth = prepared.pullquote
			? std::min(
				analysis.contentPreferredWidth,
				std::max(st.pullquote.maxWidth, 1))
			: analysis.contentPreferredWidth;
		analysis.outerMinimumWidth = overhead
			+ paddingWidth
			+ pullquoteReserveWidth
			+ visibleContentWidth;
		analysis.outerPreferredWidth = overhead
			+ paddingWidth
			+ pullquoteReserveWidth
			+ visiblePreferredContentWidth;
		if (prepared.pullquote) {
			const auto requiredContentWidth = std::max(
				analysis.contentMinimumWidth,
				childRequiredWidth);
			analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
				? overhead
					+ paddingWidth
					+ pullquoteReserveWidth
					+ requiredContentWidth
				: analysis.outerMinimumWidth;
			analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
				? ReadableScrollViewportMinimumWidth(
					analysis.contentPreferredWidth,
					st)
				: 1;
			analysis.outerScrollViewportMinimumWidth = overhead
				+ paddingWidth
				+ pullquoteReserveWidth
				+ (childNeedsScrollOwner ? childRequiredWidth : 1);
			analysis.scrollOwnerMinimumWidth
				= analysis.scrollOwnerOverflowWidth;
			if (childNeedsScrollOwner) {
				analysis.scrollOwnerMinimumWidth = std::max({
					analysis.scrollOwnerMinimumWidth,
					analysis.outerScrollViewportMinimumWidth,
				});
			}
		} else {
			const auto requiredContentWidth = std::max(
				analysis.contentMinimumWidth,
				childRequiredWidth);
			analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
				? overhead + paddingWidth + requiredContentWidth
				: analysis.outerMinimumWidth;
			analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
				? ReadableScrollViewportMinimumWidth(
					analysis.contentPreferredWidth,
					st)
				: 1;
			analysis.outerScrollViewportMinimumWidth = overhead
				+ paddingWidth
				+ (childNeedsScrollOwner ? childRequiredWidth : 1);
			analysis.scrollOwnerMinimumWidth = std::max({
				analysis.scrollOwnerOverflowWidth,
				analysis.outerScrollViewportMinimumWidth,
			});
		}
		analysis.ownerEligible = !prepared.children.empty();
	} break;
	case PreparedBlockKind::Details: {
		const auto headerPadding = DetailsHeaderPadding(context, st);
		const auto bodyPadding = DetailsBodyPadding(context, st);
		const auto iconWidth = st.details.icon.empty()
			? 0
			: TextLineHeight(st.details.summaryStyle);
		const auto iconSkip = iconWidth ? st.details.iconSkip : 0;
		const auto actionWidth = context.editMode
			? DetailsStateReserveWidth(st)
			: 0;
		const auto actionSkip = actionWidth ? st.details.stateSkip : 0;
		const auto headerMinimumWidth = HorizontalMarginsWidth(headerPadding)
			+ iconWidth
			+ iconSkip
			+ actionSkip
			+ actionWidth
			+ DetailsSummaryContentMinimumWidth(
				prepared,
				formulas,
				inlineFormulaObjects,
				mediaRuntime,
				st,
				context);
		const auto bodyPaddingWidth = HorizontalMarginsWidth(bodyPadding);
		auto bodyMinimumWidth = 1;
		auto bodyPreferredWidth = 1;
		auto childRequiredWidth = 1;
		auto childNeedsScrollOwner = false;
		if (!prepared.collapsed) {
			const auto childWidth = std::max(
				availableWidth - bodyPaddingWidth,
				1);
			visibleScrollViewportWidth = childWidth;
			auto childContext = context;
			PrepareNestedContext(&childContext, 0, childWidth);
			analysis.children = AnalyzeBlocks(
				prepared.children,
				formulas,
				inlineFormulaObjects,
				mediaRuntime,
				st,
				childWidth,
				childContext);
			childRequiredWidth
				= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
					analysis.children);
			childNeedsScrollOwner = (childRequiredWidth > 1);
			analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(
				analysis.children);
			analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
				analysis.children);
			bodyMinimumWidth = ContainerMinimumWidth(
				analysis.contentMinimumWidth,
				bodyPadding);
			bodyPreferredWidth = std::max(analysis.contentPreferredWidth, 1)
				+ bodyPaddingWidth;
			analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
				? ReadableScrollViewportMinimumWidth(
					analysis.contentPreferredWidth,
					st)
				: 1;
			analysis.outerScrollViewportMinimumWidth = bodyPaddingWidth
				+ (childNeedsScrollOwner ? childRequiredWidth : 1);
			analysis.scrollOwnerMinimumWidth = std::max({
				bodyMinimumWidth,
				analysis.outerScrollViewportMinimumWidth,
			});
		}
		analysis.outerMinimumWidth = std::max(
			headerMinimumWidth,
			bodyMinimumWidth);
		analysis.outerPreferredWidth = std::max(
			headerMinimumWidth,
			bodyPreferredWidth);
		analysis.scrollOwnerOverflowWidth
			= childNeedsScrollOwner
			? bodyPaddingWidth
				+ std::max(analysis.contentMinimumWidth, childRequiredWidth)
			: bodyMinimumWidth;
		analysis.ownerEligible = !prepared.collapsed
			&& !prepared.children.empty();
	} break;
	case PreparedBlockKind::EmbedPost: {
		const auto &style = st.embedPost;
		const auto contentOverhead = style.accentWidth
			+ style.accentSkip
			+ style.padding.left()
			+ style.padding.right();
		const auto avatarWidth = prepared.embedPost.authorPhotoId
			? std::max(style.avatarSize, 1)
			: 0;
		const auto headerGap = avatarWidth ? style.headerGap : 0;
		auto headerTextWidth = 1;
		auto headerTextPreferredWidth = 1;
		if (!prepared.embedPost.author.isEmpty()) {
			headerTextWidth = std::max(
				headerTextWidth,
				ReadableTextMinWidth(style.authorStyle));
			headerTextPreferredWidth = std::max(
				headerTextPreferredWidth,
				style.authorStyle.font->width(prepared.embedPost.author));
		}
		if (!prepared.embedPost.dateText.isEmpty()) {
			headerTextWidth = std::max(
				headerTextWidth,
				ReadableTextMinWidth(style.dateStyle));
			headerTextPreferredWidth = std::max(
				headerTextPreferredWidth,
				style.dateStyle.font->width(prepared.embedPost.dateText));
		}
		const auto childWidth = std::max(availableWidth - contentOverhead, 1);
		visibleScrollViewportWidth = childWidth;
		auto childContext = context;
		PrepareNestedContext(&childContext, 0, childWidth);
		analysis.children = AnalyzeBlocks(
			prepared.children,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			childWidth,
			childContext);
		const auto childRequiredWidth
			= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
				analysis.children);
		const auto childNeedsScrollOwner = (childRequiredWidth > 1);
		analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(analysis.children);
		analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
			analysis.children);
		const auto naturalOverflowWidth = contentOverhead
			+ analysis.contentMinimumWidth;
		const auto requiredOverflowWidth = contentOverhead
			+ std::max(analysis.contentMinimumWidth, childRequiredWidth);
		analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
			? ReadableScrollViewportMinimumWidth(
				analysis.contentPreferredWidth,
				st)
			: 1;
		analysis.outerScrollViewportMinimumWidth = contentOverhead
			+ (childNeedsScrollOwner ? childRequiredWidth : 1);
		analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
			? requiredOverflowWidth
			: naturalOverflowWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerOverflowWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.outerMinimumWidth = std::max(
			contentOverhead + avatarWidth + headerGap + headerTextWidth,
			analysis.scrollOwnerOverflowWidth);
		analysis.outerPreferredWidth = std::max(
			contentOverhead + avatarWidth + headerGap + headerTextPreferredWidth,
			contentOverhead + analysis.contentPreferredWidth);
		analysis.ownerEligible = !prepared.children.empty();
	} break;
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Placeholder:
		analysis.contentMinimumWidth = 1;
		analysis.contentPreferredWidth = analysis.contentMinimumWidth;
		analysis.outerMinimumWidth = 1;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		analysis.ownerEligible = false;
		break;
	}
	FinalizeOwnerSelection(
		&analysis,
		availableWidth,
		visibleScrollViewportWidth,
		AnalysisOwnerMovesChildren(prepared.kind));
	return analysis;
}

[[nodiscard]] const Ui::Text::String &DisplayedLeaf(
		const LaidOutBlock &block) {
	return block.placeholderLeaf.isEmpty()
		? block.leaf
		: block.placeholderLeaf;
}

[[nodiscard]] int RetainedLeafMaxWidth(
		const Ui::Text::String &leaf,
		int minimum = 1) {
	return std::max(leaf.maxWidth(), std::max(minimum, 1));
}

[[nodiscard]] std::optional<WidthAnalysisNode> AnalyzeRetainedBlock(
	const PreparedBlock &prepared,
	const LaidOutBlock &block,
	const std::vector<PreparedFormulaSlot> &formulas,
	const style::Markdown &st,
	int width,
	LayoutContext context);

[[nodiscard]] std::optional<std::vector<WidthAnalysisNode>> AnalyzeRetainedBlocks(
		const std::vector<PreparedBlock> &prepared,
		const std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedFormulaSlot> &formulas,
		const style::Markdown &st,
		int width,
		LayoutContext context) {
	if (prepared.size() != blocks.size()) {
		return std::nullopt;
	}
	auto result = std::vector<WidthAnalysisNode>();
	result.reserve(prepared.size());
	for (auto i = 0, count = int(prepared.size()); i != count; ++i) {
		const auto &preparedBlock = prepared[i];
		const auto &block = blocks[i];
		if (preparedBlock.kind != block.kind) {
			return std::nullopt;
		}
		const auto next = NextVisibleBlock(prepared, i);
		auto blockContext = context;
		blockContext.preparedPath.push_back(i);
		auto blockWidth = BlockBandWidth(preparedBlock.kind, st, width, context);
		if (IsRelatedArticlesHeader(preparedBlock, next)) {
			blockWidth = std::max(
				width
					- st.relatedArticle.headerPadding.left()
					- st.relatedArticle.headerPadding.right(),
				1);
		}
		auto analysis = AnalyzeRetainedBlock(
			preparedBlock,
			block,
			formulas,
			st,
			blockWidth,
			blockContext);
		if (!analysis) {
			return std::nullopt;
		}
		result.push_back(std::move(*analysis));
	}
	return result;
}

[[nodiscard]] std::optional<WidthAnalysisNode> AnalyzeRetainedBlock(
		const PreparedBlock &prepared,
		const LaidOutBlock &block,
		const std::vector<PreparedFormulaSlot> &formulas,
		const style::Markdown &st,
		int width,
		LayoutContext context) {
	if (prepared.kind != block.kind) {
		return std::nullopt;
	}
	auto analysis = WidthAnalysisNode();
	if (HideEmptyQuoteAuthorBlock(prepared, context)) {
		return analysis;
	}
	const auto availableWidth = std::max(width, 1);
	auto visibleScrollViewportWidth = availableWidth;
	switch (prepared.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading: {
		const auto &displayLeaf = DisplayedLeaf(block);
		analysis.contentMinimumWidth = std::max(
			FlowBlockMinimumWidth(prepared, st),
			IsAnchorOnlyBlock(prepared)
				? 0
				: LeafMinimumWidth(displayLeaf));
		analysis.contentPreferredWidth = IsAnchorOnlyBlock(prepared)
			? 1
			: RetainedLeafMaxWidth(displayLeaf);
		analysis.outerMinimumWidth = analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		analysis.scrollViewportMinimumWidth
			= NestedScrollViewportMinimumWidth(
				context,
				analysis.contentPreferredWidth,
				st);
		analysis.outerScrollViewportMinimumWidth = std::max(
			analysis.outerMinimumWidth,
			analysis.scrollViewportMinimumWidth);
		analysis.scrollOwnerOverflowWidth
			= (analysis.scrollViewportMinimumWidth > 1)
			? analysis.outerScrollViewportMinimumWidth
			: analysis.outerMinimumWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerMinimumWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = !IsAnchorOnlyBlock(prepared);
	} break;
	case PreparedBlockKind::CodeBlock: {
		const auto padding = BlockquotePadding(st.code.pre);
		visibleScrollViewportWidth = availableWidth
			- HorizontalMarginsWidth(padding);
		analysis.contentMinimumWidth = CodeBlockMinimumWidth(st);
		analysis.contentPreferredWidth = HorizontalMarginsWidth(padding)
			+ RetainedLeafMaxWidth(DisplayedLeaf(block));
		analysis.outerMinimumWidth = analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		const auto contentMaxWidth = std::max(
			analysis.contentPreferredWidth - HorizontalMarginsWidth(padding),
			1);
		analysis.scrollViewportMinimumWidth
			= NestedScrollViewportMinimumWidth(
				context,
				contentMaxWidth,
				st);
		analysis.outerScrollViewportMinimumWidth
			= std::max(
				HorizontalMarginsWidth(padding)
					+ analysis.scrollViewportMinimumWidth,
				analysis.outerPreferredWidth);
		analysis.scrollOwnerOverflowWidth
			= (analysis.scrollViewportMinimumWidth > 1)
			? std::max(
				analysis.outerMinimumWidth,
				analysis.outerPreferredWidth)
			: analysis.outerMinimumWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerMinimumWidth,
			analysis.outerPreferredWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = true;
	} break;
	case PreparedBlockKind::DisplayMath: {
		const auto &padding = st.displayMath.padding;
		visibleScrollViewportWidth = availableWidth
			- HorizontalMarginsWidth(padding);
		if (const auto formula = PreparedFormulaFor(formulas, prepared.formulaIndex);
			formula && formula->measured.success) {
			analysis.contentMinimumWidth = HorizontalMarginsWidth(padding)
				+ std::max(formula->measured.logicalSize.width(), 1);
			analysis.contentPreferredWidth = analysis.contentMinimumWidth;
		} else {
			const auto &fallbackPadding = st.displayMath.fallbackPadding;
			const auto &displayLeaf = block.placeholderLeaf.isEmpty()
				? block.fallbackLeaf
				: block.placeholderLeaf;
			const auto fallbackWidth = RetainedLeafMaxWidth(
				displayLeaf,
				ReadableTextMinWidth(st.displayMath.fallbackStyle));
			analysis.contentMinimumWidth = HorizontalMarginsWidth(padding)
				+ HorizontalMarginsWidth(fallbackPadding)
				+ fallbackWidth;
			analysis.contentPreferredWidth = analysis.contentMinimumWidth;
		}
		analysis.outerMinimumWidth = analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		const auto contentMaxWidth = std::max(
			analysis.contentPreferredWidth - HorizontalMarginsWidth(padding),
			1);
		analysis.scrollViewportMinimumWidth
			= NestedScrollViewportMinimumWidth(
				context,
				contentMaxWidth,
				st);
		analysis.outerScrollViewportMinimumWidth
			= std::max(
				HorizontalMarginsWidth(padding)
					+ analysis.scrollViewportMinimumWidth,
				analysis.outerPreferredWidth);
		analysis.scrollOwnerOverflowWidth
			= (analysis.scrollViewportMinimumWidth > 1)
			? std::max(
				analysis.outerMinimumWidth,
				analysis.outerPreferredWidth)
			: analysis.outerMinimumWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerMinimumWidth,
			analysis.outerPreferredWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = true;
	} break;
	case PreparedBlockKind::Table:
		if (prepared.tableRows.size() != block.tableRows.size()) {
			return std::nullopt;
		}
		for (auto rowIndex = 0, rowCount = int(prepared.tableRows.size());
				rowIndex != rowCount;
				++rowIndex) {
			if (prepared.tableRows[rowIndex].cells.size()
				!= block.tableRows[rowIndex].cells.size()) {
				return std::nullopt;
			}
		}
		analysis.contentMinimumWidth = RetainedTableBlockMinimumWidth(
			prepared,
			block,
			st);
		analysis.contentPreferredWidth = analysis.contentMinimumWidth;
		analysis.outerMinimumWidth = analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		analysis.scrollViewportMinimumWidth
			= NestedScrollViewportMinimumWidth(
				context,
				analysis.contentPreferredWidth,
				st);
		analysis.outerScrollViewportMinimumWidth = std::max(
			analysis.scrollViewportMinimumWidth,
			analysis.outerPreferredWidth);
		analysis.scrollOwnerOverflowWidth
			= (analysis.scrollViewportMinimumWidth > 1)
			? std::max(
				analysis.outerMinimumWidth,
				analysis.outerPreferredWidth)
			: analysis.outerMinimumWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerMinimumWidth,
			analysis.outerPreferredWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = true;
		break;
	case PreparedBlockKind::List: {
		auto childContext = context;
		const auto depthDelta = std::max(
			prepared.visualDepth - context.listDepth,
			0);
		const auto markerColumn = context.listItemContentShift;
		const auto step = std::max(st.textPadding.left(), markerColumn);
		const auto overhead = depthDelta * step - markerColumn;
		const auto childWidth = std::max(availableWidth - overhead, 1);
		visibleScrollViewportWidth = childWidth;
		childContext.listDepth = prepared.visualDepth;
		childContext.tightList = false;
		PrepareNestedContext(&childContext, 0, childWidth);
		auto children = AnalyzeRetainedBlocks(
			prepared.children,
			block.children,
			formulas,
			st,
			childWidth,
			childContext);
		if (!children) {
			return std::nullopt;
		}
		analysis.children = std::move(*children);
		const auto childRequiredWidth
			= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
				analysis.children);
		const auto childNeedsScrollOwner = (childRequiredWidth > 1);
		analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(analysis.children);
		analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
			analysis.children);
		analysis.outerMinimumWidth = overhead + analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = overhead + analysis.contentPreferredWidth;
		const auto naturalOverflowWidth = overhead
			+ analysis.contentMinimumWidth;
		const auto requiredOverflowWidth = overhead
			+ std::max(analysis.contentMinimumWidth, childRequiredWidth);
		analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
			? ReadableScrollViewportMinimumWidth(
				analysis.contentPreferredWidth,
				st)
			: 1;
		analysis.outerScrollViewportMinimumWidth = overhead
			+ (childNeedsScrollOwner ? childRequiredWidth : 1);
		analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
			? requiredOverflowWidth
			: naturalOverflowWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerOverflowWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = !prepared.children.empty();
	} break;
	case PreparedBlockKind::ListItem: {
		if (prepared.children.size() != block.children.size()) {
			return std::nullopt;
		}
		const auto markerWidth = std::max(
			st.list.markerWidth,
			(prepared.taskState != TaskState::None)
				? st.list.taskCheck.diameter
				: (!block.marker.isEmpty()
					? RetainedLeafMaxWidth(block.marker)
					: ListItemMarkerMinimumWidth(prepared, st)));
		const auto overhead = markerWidth + st.list.markerSkip;
		const auto childWidth = std::max(availableWidth - overhead, 1);
		visibleScrollViewportWidth = childWidth;
		auto childContext = context;
		childContext.tightList = false;
		PrepareNestedContext(&childContext, 0, childWidth);
		childContext.listItemDepth = context.listItemDepth + 1;
		childContext.listItemContentShift = overhead;
		auto children = AnalyzeRetainedBlocks(
			prepared.children,
			block.children,
			formulas,
			st,
			childWidth,
			childContext);
		if (!children) {
			return std::nullopt;
		}
		analysis.children = std::move(*children);
		const auto childRequiredWidth
			= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
				analysis.children);
		const auto childNeedsScrollOwner = (childRequiredWidth > 1);
		analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(analysis.children);
		analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
			analysis.children);
		analysis.outerMinimumWidth = overhead + analysis.contentMinimumWidth;
		analysis.outerPreferredWidth = overhead + analysis.contentPreferredWidth;
		const auto naturalOverflowWidth = overhead
			+ analysis.contentMinimumWidth;
		const auto requiredOverflowWidth = overhead
			+ std::max(analysis.contentMinimumWidth, childRequiredWidth);
		analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
			? ReadableScrollViewportMinimumWidth(
				analysis.contentPreferredWidth,
				st)
			: 1;
		analysis.outerScrollViewportMinimumWidth = overhead
			+ (childNeedsScrollOwner ? childRequiredWidth : 1);
		analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
			? requiredOverflowWidth
			: naturalOverflowWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerOverflowWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.ownerEligible = !prepared.children.empty();
	} break;
	case PreparedBlockKind::Quote: {
		const auto depthDelta = std::max(
			prepared.visualDepth - context.quoteDepth,
			0);
		const auto overhead = depthDelta * st.quoteIndent;
		const auto padding = prepared.pullquote
			? st.pullquote.padding
			: BlockquotePadding(st.body.blockquote);
		const auto paddingWidth = HorizontalMarginsWidth(padding);
		const auto pullquoteReserveWidth = prepared.pullquote
			? (2 * PullquoteIconReserveWidth(st.body.blockquote))
			: 0;
		auto childWidth = std::max(
			availableWidth - overhead - paddingWidth - pullquoteReserveWidth,
			1);
		if (prepared.pullquote) {
			childWidth = std::min(childWidth, std::max(st.pullquote.maxWidth, 1));
		}
		visibleScrollViewportWidth = childWidth;
		auto childContext = context;
		childContext.quoteDepth = prepared.visualDepth + 1;
		childContext.tightList = false;
		PrepareNestedContext(&childContext, 0, childWidth);
		childContext.hideEmptyQuoteAuthor = QuoteBodyEmptyForLayout(
			prepared,
			childContext);
		auto children = AnalyzeRetainedBlocks(
			prepared.children,
			block.children,
			formulas,
			st,
			childWidth,
			childContext);
		if (!children) {
			return std::nullopt;
		}
		analysis.children = std::move(*children);
		const auto childRequiredWidth
			= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
				analysis.children);
		const auto childNeedsScrollOwner = (childRequiredWidth > 1);
		analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(analysis.children);
		analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
			analysis.children);
		const auto visibleContentWidth = prepared.pullquote
			? std::min(
				analysis.contentMinimumWidth,
				std::max(st.pullquote.maxWidth, 1))
			: analysis.contentMinimumWidth;
		const auto visiblePreferredContentWidth = prepared.pullquote
			? std::min(
				analysis.contentPreferredWidth,
				std::max(st.pullquote.maxWidth, 1))
			: analysis.contentPreferredWidth;
		analysis.outerMinimumWidth = overhead
			+ paddingWidth
			+ pullquoteReserveWidth
			+ visibleContentWidth;
		analysis.outerPreferredWidth = overhead
			+ paddingWidth
			+ pullquoteReserveWidth
			+ visiblePreferredContentWidth;
		if (prepared.pullquote) {
			const auto requiredContentWidth = std::max(
				analysis.contentMinimumWidth,
				childRequiredWidth);
			analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
				? overhead
					+ paddingWidth
					+ pullquoteReserveWidth
					+ requiredContentWidth
				: analysis.outerMinimumWidth;
			analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
				? ReadableScrollViewportMinimumWidth(
					analysis.contentPreferredWidth,
					st)
				: 1;
			analysis.outerScrollViewportMinimumWidth = overhead
				+ paddingWidth
				+ pullquoteReserveWidth
				+ (childNeedsScrollOwner ? childRequiredWidth : 1);
			analysis.scrollOwnerMinimumWidth
				= analysis.scrollOwnerOverflowWidth;
			if (childNeedsScrollOwner) {
				analysis.scrollOwnerMinimumWidth = std::max({
					analysis.scrollOwnerMinimumWidth,
					analysis.outerScrollViewportMinimumWidth,
				});
			}
		} else {
			const auto requiredContentWidth = std::max(
				analysis.contentMinimumWidth,
				childRequiredWidth);
			analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
				? overhead + paddingWidth + requiredContentWidth
				: analysis.outerMinimumWidth;
			analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
				? ReadableScrollViewportMinimumWidth(
					analysis.contentPreferredWidth,
					st)
				: 1;
			analysis.outerScrollViewportMinimumWidth = overhead
				+ paddingWidth
				+ (childNeedsScrollOwner ? childRequiredWidth : 1);
			analysis.scrollOwnerMinimumWidth = std::max({
				analysis.scrollOwnerOverflowWidth,
				analysis.outerScrollViewportMinimumWidth,
			});
		}
		analysis.ownerEligible = !prepared.children.empty();
	} break;
	case PreparedBlockKind::Details: {
		const auto headerPadding = DetailsHeaderPadding(context, st);
		const auto bodyPadding = DetailsBodyPadding(context, st);
		const auto iconWidth = st.details.icon.empty()
			? 0
			: TextLineHeight(st.details.summaryStyle);
		const auto iconSkip = iconWidth ? st.details.iconSkip : 0;
		const auto actionWidth = context.editMode
			? DetailsStateReserveWidth(st)
			: 0;
		const auto actionSkip = actionWidth ? st.details.stateSkip : 0;
		const auto summaryMinimumWidth = std::max(
			ReadableTextMinWidth(st.details.summaryStyle),
			LeafMinimumWidth(DisplayedLeaf(block)));
		const auto headerMinimumWidth = HorizontalMarginsWidth(headerPadding)
			+ iconWidth
			+ iconSkip
			+ actionSkip
			+ actionWidth
			+ summaryMinimumWidth;
		const auto bodyPaddingWidth = HorizontalMarginsWidth(bodyPadding);
		auto bodyMinimumWidth = 1;
		auto bodyPreferredWidth = 1;
		auto childRequiredWidth = 1;
		auto childNeedsScrollOwner = false;
		if (prepared.collapsed) {
			if (!prepared.children.empty() && !block.children.empty()) {
				return std::nullopt;
			}
		} else {
			const auto childWidth = std::max(
				availableWidth - bodyPaddingWidth,
				1);
			visibleScrollViewportWidth = childWidth;
			auto childContext = context;
			PrepareNestedContext(&childContext, 0, childWidth);
			auto children = AnalyzeRetainedBlocks(
				prepared.children,
				block.children,
				formulas,
				st,
				childWidth,
				childContext);
			if (!children) {
				return std::nullopt;
			}
			analysis.children = std::move(*children);
			childRequiredWidth
				= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
					analysis.children);
			childNeedsScrollOwner = (childRequiredWidth > 1);
			analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(
				analysis.children);
			analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
				analysis.children);
			bodyMinimumWidth = ContainerMinimumWidth(
				analysis.contentMinimumWidth,
				bodyPadding);
			bodyPreferredWidth = std::max(analysis.contentPreferredWidth, 1)
				+ bodyPaddingWidth;
			analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
				? ReadableScrollViewportMinimumWidth(
					analysis.contentPreferredWidth,
					st)
				: 1;
			analysis.outerScrollViewportMinimumWidth = bodyPaddingWidth
				+ (childNeedsScrollOwner ? childRequiredWidth : 1);
			analysis.scrollOwnerMinimumWidth = std::max({
				bodyMinimumWidth,
				analysis.outerScrollViewportMinimumWidth,
			});
		}
		analysis.outerMinimumWidth = std::max(
			headerMinimumWidth,
			bodyMinimumWidth);
		analysis.outerPreferredWidth = std::max(
			headerMinimumWidth,
			bodyPreferredWidth);
		analysis.scrollOwnerOverflowWidth
			= childNeedsScrollOwner
			? bodyPaddingWidth
				+ std::max(analysis.contentMinimumWidth, childRequiredWidth)
			: bodyMinimumWidth;
		analysis.ownerEligible = !prepared.collapsed
			&& !prepared.children.empty();
	} break;
	case PreparedBlockKind::EmbedPost: {
		const auto &style = st.embedPost;
		const auto contentOverhead = style.accentWidth
			+ style.accentSkip
			+ style.padding.left()
			+ style.padding.right();
		const auto avatarWidth = prepared.embedPost.authorPhotoId
			? std::max(style.avatarSize, 1)
			: 0;
		const auto headerGap = avatarWidth ? style.headerGap : 0;
		auto headerTextWidth = 1;
		auto headerTextPreferredWidth = 1;
		if (!block.labelLeaf.isEmpty()) {
			headerTextWidth = std::max(
				headerTextWidth,
				ReadableTextMinWidth(style.authorStyle));
			headerTextPreferredWidth = std::max(
				headerTextPreferredWidth,
				RetainedLeafMaxWidth(block.labelLeaf));
		}
		if (!block.subtitleLeaf.isEmpty()) {
			headerTextWidth = std::max(
				headerTextWidth,
				ReadableTextMinWidth(style.dateStyle));
			headerTextPreferredWidth = std::max(
				headerTextPreferredWidth,
				RetainedLeafMaxWidth(block.subtitleLeaf));
		}
		const auto childWidth = std::max(availableWidth - contentOverhead, 1);
		visibleScrollViewportWidth = childWidth;
		auto childContext = context;
		PrepareNestedContext(&childContext, 0, childWidth);
		auto children = AnalyzeRetainedBlocks(
			prepared.children,
			block.children,
			formulas,
			st,
			childWidth,
			childContext);
		if (!children) {
			return std::nullopt;
		}
		analysis.children = std::move(*children);
		const auto childRequiredWidth
			= MaxUnresolvedChildOuterScrollViewportMinimumWidth(
				analysis.children);
		const auto childNeedsScrollOwner = (childRequiredWidth > 1);
		analysis.contentMinimumWidth = MaxChildOuterMinimumWidth(analysis.children);
		analysis.contentPreferredWidth = MaxChildOuterPreferredWidth(
			analysis.children);
		const auto naturalOverflowWidth = contentOverhead
			+ analysis.contentMinimumWidth;
		const auto requiredOverflowWidth = contentOverhead
			+ std::max(analysis.contentMinimumWidth, childRequiredWidth);
		analysis.scrollViewportMinimumWidth = childNeedsScrollOwner
			? ReadableScrollViewportMinimumWidth(
				analysis.contentPreferredWidth,
				st)
			: 1;
		analysis.outerScrollViewportMinimumWidth = contentOverhead
			+ (childNeedsScrollOwner ? childRequiredWidth : 1);
		analysis.scrollOwnerOverflowWidth = childNeedsScrollOwner
			? requiredOverflowWidth
			: naturalOverflowWidth;
		analysis.scrollOwnerMinimumWidth = std::max({
			analysis.scrollOwnerOverflowWidth,
			analysis.outerScrollViewportMinimumWidth,
		});
		analysis.outerMinimumWidth = std::max(
			contentOverhead + avatarWidth + headerGap + headerTextWidth,
			analysis.scrollOwnerOverflowWidth);
		analysis.outerPreferredWidth = std::max(
			contentOverhead + avatarWidth + headerGap + headerTextPreferredWidth,
			contentOverhead + analysis.contentPreferredWidth);
		analysis.ownerEligible = !prepared.children.empty();
	} break;
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Placeholder:
		analysis.contentMinimumWidth = 1;
		analysis.contentPreferredWidth = analysis.contentMinimumWidth;
		analysis.outerMinimumWidth = 1;
		analysis.outerPreferredWidth = analysis.contentPreferredWidth;
		analysis.scrollOwnerMinimumWidth = analysis.outerMinimumWidth;
		analysis.ownerEligible = false;
		break;
	}
	FinalizeOwnerSelection(
		&analysis,
		availableWidth,
		visibleScrollViewportWidth,
		AnalysisOwnerMovesChildren(prepared.kind));
	return analysis;
}

[[nodiscard]] LaidOutBlock LayoutBlock(
	const PreparedBlock &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	std::vector<RenderedFormula> *renderedFormulas,
	MathRenderer *renderer,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const WidthAnalysisNode &analysis,
	const WidthAnalysisNode *activeScrollOwner,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context);

[[nodiscard]] int LayoutBlocks(
		const std::vector<PreparedBlock> &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		std::vector<LaidOutBlock> *blocks,
		const std::vector<WidthAnalysisNode> &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context);

using LayoutNestedBlocksCallback = std::function<std::optional<int>(
	const std::vector<PreparedBlock> &prepared,
	std::vector<LaidOutBlock> *blocks,
	const std::vector<WidthAnalysisNode> &analysis,
	const WidthAnalysisNode *activeScrollOwner,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context)>;
using LayoutListChildCallback = std::function<std::optional<int>(
	int index,
	const WidthAnalysisNode *activeScrollOwner,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context,
	bool tight)>;

[[nodiscard]] LayoutNestedBlocksCallback FreshNestedBlocksCallback(
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st) {
	return [
		formulas,
		renderedFormulas,
		renderer,
		inlineFormulaObjects,
		mediaRuntime,
		&st
	](const std::vector<PreparedBlock> &prepared,
			std::vector<LaidOutBlock> *blocks,
			const std::vector<WidthAnalysisNode> &analysis,
			const WidthAnalysisNode *activeScrollOwner,
			int left,
			int top,
			int width,
			int logicalWidth,
			LayoutContext context) -> std::optional<int> {
		if (!blocks) {
			return std::nullopt;
		}
		blocks->clear();
		return LayoutBlocks(
			prepared,
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			blocks,
			analysis,
			activeScrollOwner,
			st,
			left,
			top,
			width,
			logicalWidth,
			context);
	};
}

[[nodiscard]] std::optional<int> LayoutListItemBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const WidthAnalysisNode &analysis,
	const WidthAnalysisNode *activeScrollOwner,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context,
	bool tight,
	const LayoutNestedBlocksCallback &layoutNestedBlocks);
[[nodiscard]] std::optional<int> LayoutListBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const WidthAnalysisNode &analysis,
	const WidthAnalysisNode *activeScrollOwner,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context,
	const LayoutListChildCallback &layoutChild);
[[nodiscard]] std::optional<int> LayoutQuoteBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const WidthAnalysisNode &analysis,
	const WidthAnalysisNode *activeScrollOwner,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context,
	const LayoutNestedBlocksCallback &layoutNestedBlocks);
[[nodiscard]] std::optional<int> LayoutDetailsBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const WidthAnalysisNode &analysis,
	const WidthAnalysisNode *activeScrollOwner,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context,
	const LayoutNestedBlocksCallback &layoutNestedBlocks);
[[nodiscard]] std::optional<int> LayoutEmbedPostBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const WidthAnalysisNode &analysis,
	const WidthAnalysisNode *activeScrollOwner,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context,
	const LayoutNestedBlocksCallback &layoutNestedBlocks);

[[nodiscard]] LaidOutBlock LayoutListItemBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context,
		bool tight) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::ListItem;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.supplementary = prepared.supplementary;
	block.listKind = prepared.listKind;
	block.listDelimiter = prepared.listDelimiter;
	block.taskState = prepared.taskState;
	block.orderedNumber = prepared.orderedNumber;

	const auto task = (prepared.taskState != TaskState::None);
	const auto ordered = !task && (prepared.listKind == ListKind::Ordered);
	const auto markerText = ordered ? ListMarkerText(prepared) : QString();
	if (task
		&& prepared.editListItem
		&& context.taskMarkerRippleRuntimeFactory) {
		block.taskMarkerRippleRuntime
			= context.taskMarkerRippleRuntimeFactory(*prepared.editListItem);
	}
	if (ordered) {
		auto markerContext = context;
		markerContext.rtl = false;
		BuildOrReusePlainTextLeaf(
			&block.marker,
			CachedTextLeafSlot::Marker,
			prepared,
			st.body,
			markerText,
			PlainTextMinResizeWidth(st.body),
			markerContext);
	}
	const auto bottom = LayoutListItemBlockGeometry(
		prepared,
		&block,
		analysis,
		activeScrollOwner,
		st,
		left,
		top,
		width,
		logicalWidth,
		context,
		tight,
		FreshNestedBlocksCallback(
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			st));
	Expects(bottom.has_value());
	return block;
}

[[nodiscard]] LaidOutBlock LayoutListBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::List;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.listKind = prepared.listKind;
	block.listDelimiter = prepared.listDelimiter;
	const auto layoutChild = [&](int index,
			const WidthAnalysisNode *childActiveScrollOwner,
			int childLeft,
			int childTop,
			int childWidth,
			int childLogicalWidth,
			LayoutContext childContext,
			bool tight) -> std::optional<int> {
		const auto &child = prepared.children[index];
		auto laidOut = (child.kind == PreparedBlockKind::ListItem)
			? LayoutListItemBlock(
				child,
				formulas,
				renderedFormulas,
				renderer,
				inlineFormulaObjects,
				mediaRuntime,
				analysis.children[index],
				childActiveScrollOwner,
				st,
				childLeft,
				childTop,
				childWidth,
				childLogicalWidth,
				childContext,
				tight)
			: LayoutBlock(
				child,
				formulas,
				renderedFormulas,
				renderer,
				inlineFormulaObjects,
				mediaRuntime,
				analysis.children[index],
				childActiveScrollOwner,
				st,
				childLeft,
				childTop,
				childWidth,
				childLogicalWidth,
				childContext);
		const auto bottom = BlockBottom(laidOut);
		block.children.push_back(std::move(laidOut));
		return bottom;
	};
	const auto bottom = LayoutListBlockGeometry(
		prepared,
		&block,
		analysis,
		activeScrollOwner,
		st,
		left,
		top,
		width,
		logicalWidth,
		context,
		layoutChild);
	Expects(bottom.has_value());
	return block;
}


[[nodiscard]] LaidOutBlock LayoutQuoteBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Quote;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.pullquote = prepared.pullquote;
	const auto bottom = LayoutQuoteBlockGeometry(
		prepared,
		&block,
		analysis,
		activeScrollOwner,
		st,
		left,
		top,
		width,
		logicalWidth,
		context,
		FreshNestedBlocksCallback(
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			st));
	Expects(bottom.has_value());
	return block;
}

[[nodiscard]] LaidOutBlock LayoutDetailsBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Details;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.collapsed = prepared.collapsed;
	block.detailsOpen = prepared.detailsOpen;
	block.supplementary = prepared.supplementary;
	const auto &details = st.details;
	BuildOrReuseMarkedTextLeaf(
		&block.leaf,
		CachedTextLeafSlot::Leaf,
		prepared,
		details.summaryStyle,
		st,
		prepared.text,
		prepared.links,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		FlowTextMinResizeWidth(details.summaryStyle),
		context);
	const auto usePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (usePlaceholder) {
		BuildOrReuseEditPlaceholderLeaf(
			&block.placeholderText,
			&block.placeholderLeaf,
			prepared,
			prepared.editPlaceholderText,
			details.summaryStyle,
			PlainTextMinResizeWidth(details.summaryStyle),
			context);
	}
	if (context.editMode) {
		auto actionContext = context;
		actionContext.rtl = false;
		BuildOrReusePlainTextLeaf(
			&block.actionLeaf,
			CachedTextLeafSlot::Action,
			prepared,
			details.summaryStyle,
			DetailsStateText(prepared.detailsOpen),
			PlainTextMinResizeWidth(details.summaryStyle),
			actionContext);
	}
	const auto bottom = LayoutDetailsBlockGeometry(
		prepared,
		&block,
		analysis,
		activeScrollOwner,
		st,
		left,
		top,
		width,
		logicalWidth,
		context,
		FreshNestedBlocksCallback(
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			st));
	Expects(bottom.has_value());
	return block;
}

[[nodiscard]] LaidOutBlock LayoutEmbedPostBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::EmbedPost;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.supplementary = prepared.supplementary;
	block.thumbnailPhotoId = prepared.embedPost.authorPhotoId;
	if (prepared.embedPost.authorPhotoId && mediaRuntime) {
		block.photoRuntime = mediaRuntime->resolvePhoto(
			prepared.embedPost.authorPhotoId);
	}

	const auto &style = st.embedPost;
	if (!prepared.embedPost.author.isEmpty()) {
		BuildOrReusePlainTextLeaf(
			&block.labelLeaf,
			CachedTextLeafSlot::Label,
			prepared,
			style.authorStyle,
			prepared.embedPost.author,
			PlainTextMinResizeWidth(style.authorStyle),
			context);
	}
	if (!prepared.embedPost.dateText.isEmpty()) {
		BuildOrReusePlainTextLeaf(
			&block.subtitleLeaf,
			CachedTextLeafSlot::Subtitle,
			prepared,
			style.dateStyle,
			prepared.embedPost.dateText,
			PlainTextMinResizeWidth(style.dateStyle),
			context);
	}
	FillMediaCaptionContent(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto bottom = LayoutEmbedPostBlockGeometry(
		prepared,
		&block,
		analysis,
		activeScrollOwner,
		st,
		left,
		top,
		width,
		logicalWidth,
		context,
		FreshNestedBlocksCallback(
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			st));
	Expects(bottom.has_value());
	return block;
}

[[nodiscard]] LaidOutBlock LayoutBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context) {
	const auto currentScrollOwner = NextActiveScrollOwner(
		analysis,
		activeScrollOwner);
	const auto scrollOwner = IsActiveScrollOwner(analysis, currentScrollOwner);
	const auto effectiveLogicalWidth = LogicalOuterWidth(
		analysis,
		currentScrollOwner,
		logicalWidth);
	switch (prepared.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
		return LayoutFlowBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			scrollOwner,
			context);
	case PreparedBlockKind::CodeBlock:
		return LayoutCodeBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			scrollOwner,
			context.allowAsyncSyntaxHighlighting,
			context.syntaxHighlightTracker,
			context);
	case PreparedBlockKind::Rule:
		return LayoutRuleBlock(prepared, st, left, top, width);
	case PreparedBlockKind::List:
		return LayoutListBlock(
			prepared,
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context);
	case PreparedBlockKind::ListItem:
		return LayoutListItemBlock(
			prepared,
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context,
			false);
	case PreparedBlockKind::Quote:
		return LayoutQuoteBlock(
			prepared,
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context);
	case PreparedBlockKind::DisplayMath:
		return LayoutDisplayMathBlock(
			prepared,
			*formulas,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			scrollOwner,
			context);
	case PreparedBlockKind::Table:
		return LayoutTableBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			scrollOwner,
			context);
	case PreparedBlockKind::Photo:
		return LayoutPhotoBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::Video:
		return LayoutVideoBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::Audio:
		return LayoutAudioBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::Map:
		return LayoutMapBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::Channel:
		return LayoutChannelBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::GroupedMedia:
		return LayoutGroupedMediaBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::RelatedArticle:
		return LayoutRelatedArticleBlock(
			prepared,
			st,
			left,
			top,
			width,
			mediaRuntime,
			context);
	case PreparedBlockKind::Placeholder:
		return LayoutPlaceholderBlock(
			prepared,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::Details:
		return LayoutDetailsBlock(
			prepared,
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context);
	case PreparedBlockKind::EmbedPost:
		return LayoutEmbedPostBlock(
			prepared,
			formulas,
			renderedFormulas,
			renderer,
			inlineFormulaObjects,
			mediaRuntime,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context);
	}
	Unexpected("Unknown markdown article block kind.");
}

int LayoutBlocks(
		const std::vector<PreparedBlock> &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		std::vector<LaidOutBlock> *blocks,
		const std::vector<WidthAnalysisNode> &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context) {
	auto y = top;
	auto previous = static_cast<const PreparedBlock*>(nullptr);
	for (auto i = 0, count = int(prepared.size()); i != count; ++i) {
		const auto &block = prepared[i];
		const auto anchorOnly = IsAnchorOnlyBlock(block);
		const auto next = NextVisibleBlock(prepared, i);
		auto blockContext = context;
		blockContext.preparedPath.push_back(i);
		if (HideEmptyQuoteAuthorBlock(block, blockContext)) {
			blocks->push_back(HiddenQuoteAuthorBlock(block));
			continue;
		}
		if (previous && !anchorOnly) {
			y += BlockSkip(*previous, block, context, st);
		}
		const auto band = BlockBand(
			block.kind,
			st,
			left,
			std::max(width, 1),
			context);
		const auto logicalBandWidth = BlockBandWidth(
			block.kind,
			st,
			logicalWidth,
			context);
		auto laidOut = IsRelatedArticlesHeader(block, next)
			? LayoutFlowBlock(
				block,
				formulas,
				inlineFormulaObjects,
				mediaRuntime,
				st,
				left + st.relatedArticle.headerPadding.left(),
				y + st.relatedArticle.headerPadding.top(),
				std::max(
					width
						- st.relatedArticle.headerPadding.left()
						- st.relatedArticle.headerPadding.right(),
					1),
				std::max(
					logicalWidth
						- st.relatedArticle.headerPadding.left()
						- st.relatedArticle.headerPadding.right(),
					1),
				false,
				blockContext)
			: LayoutBlock(
				block,
				formulas,
				renderedFormulas,
				renderer,
				inlineFormulaObjects,
				mediaRuntime,
				analysis[i],
				activeScrollOwner,
				st,
				band.x(),
				y,
				band.width(),
				logicalBandWidth,
				blockContext);
		if (IsRelatedArticlesHeader(block, next)) {
			laidOut.headerRect = QRect(
				left,
				y,
				std::max(width, 1),
				laidOut.outer.height()
					+ st.relatedArticle.headerPadding.top()
					+ st.relatedArticle.headerPadding.bottom());
			laidOut.outer = laidOut.headerRect;
			laidOut.contentRect = laidOut.headerRect;
			RefreshLogicalGeometry(&laidOut);
		}
		y = BlockBottom(laidOut);
		blocks->push_back(std::move(laidOut));
		if (!anchorOnly) {
			previous = &block;
		}
	}
	return y;
}

[[nodiscard]] std::optional<int> RecountBlockInPlace(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	LaidOutBlock *block,
	const WidthAnalysisNode &analysis,
	const WidthAnalysisNode *activeScrollOwner,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	LayoutContext context);

[[nodiscard]] std::optional<int> RecountBlocksInPlace(
		const std::vector<PreparedBlock> &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		std::vector<LaidOutBlock> *blocks,
		const std::vector<WidthAnalysisNode> &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context);

[[nodiscard]] LayoutNestedBlocksCallback RetainedNestedBlocksCallback(
		const std::vector<PreparedFormulaSlot> &formulas,
		const style::Markdown &st) {
	return [
		&formulas,
		&st
	](const std::vector<PreparedBlock> &prepared,
			std::vector<LaidOutBlock> *blocks,
			const std::vector<WidthAnalysisNode> &analysis,
			const WidthAnalysisNode *activeScrollOwner,
			int left,
			int top,
			int width,
			int logicalWidth,
			LayoutContext context) -> std::optional<int> {
		return RecountBlocksInPlace(
			prepared,
			formulas,
			blocks,
			analysis,
			activeScrollOwner,
			st,
			left,
			top,
			width,
			logicalWidth,
			context);
	};
}

[[nodiscard]] std::optional<int> LayoutListItemBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context,
		bool tight,
		const LayoutNestedBlocksCallback &layoutNestedBlocks) {
	if (!block) {
		return std::nullopt;
	}
	const auto &list = st.list;
	const auto bodyLineHeight = TextLineHeight(st.body);
	const auto task = (prepared.taskState != TaskState::None);
	const auto ordered = !task && (prepared.listKind == ListKind::Ordered);
	auto markerTextWidth = 0;
	auto markerTextHeight = bodyLineHeight;
	if (ordered && block->marker.isEmpty()) {
		return std::nullopt;
	}
	if (task) {
		markerTextWidth = list.taskCheck.diameter;
		markerTextHeight = list.taskCheck.diameter;
	} else if (ordered) {
		markerTextWidth = std::max(block->marker.maxWidth(), 1);
		markerTextHeight = std::max(
			block->marker.countHeight(markerTextWidth),
			bodyLineHeight);
	}
	ClearBlockGeometry(block);
	const auto currentScrollOwner = NextActiveScrollOwner(
		analysis,
		activeScrollOwner);
	const auto scrollOwner = IsActiveScrollOwner(analysis, currentScrollOwner);
	const auto outerLogicalWidth = LogicalOuterWidth(
		analysis,
		currentScrollOwner,
		logicalWidth);
	block->markerWidth = std::max(list.markerWidth, markerTextWidth);
	const auto bodyLeft = context.rtl
		? left
		: (left + block->markerWidth + list.markerSkip);
	const auto bodyWidth = std::max(
		width - block->markerWidth - list.markerSkip,
		1);
	const auto bodyLogicalWidth = std::max(
		outerLogicalWidth - block->markerWidth - list.markerSkip,
		1);
	const auto childActiveScrollOwner = currentScrollOwner;
	const auto bodyLayoutWidth = ChildLayoutWidth(
		childActiveScrollOwner,
		bodyWidth,
		bodyLogicalWidth);
	auto childContext = context;
	childContext.tightList = tight;
	PrepareNestedContext(&childContext, bodyLeft, bodyLayoutWidth);
	childContext.listItemDepth = context.listItemDepth + 1;
	childContext.listItemContentShift = block->markerWidth + list.markerSkip;
	const auto childBottom = layoutNestedBlocks(
		prepared.children,
		&block->children,
		analysis.children,
		childActiveScrollOwner,
		bodyLeft,
		top,
		bodyLayoutWidth,
		bodyLogicalWidth,
		childContext);
	if (!childBottom) {
		return std::nullopt;
	}
	const auto markerBaseline = [&] {
		for (const auto &child : block->children) {
			if (child.outer.height() <= 0) {
				continue;
			}
			return ResolveFirstDisplayedLineBaseline(child, st);
		}
		return MarkdownBodyBaseline(top, st);
	}();
	const auto contentHeight = *childBottom - top;
	const auto rowHeight = std::max({
		contentHeight,
		markerTextHeight,
		bodyLineHeight,
	});
	const auto markerTop = top + std::max(
		(bodyLineHeight - markerTextHeight) / 2,
		0);
	if (task) {
		const auto markerLeft = context.rtl
			? (left + width - list.taskCheck.diameter)
			: left;
		block->markerRect = QRect(
			markerLeft,
			markerTop,
			list.taskCheck.diameter,
			list.taskCheck.diameter);
	} else if (ordered) {
		const auto markerLeft = context.rtl
			? (left + width - block->markerWidth)
			: (left + block->markerWidth - markerTextWidth);
		const auto markerLeafBaseline = LeafFirstLineBaseline(
			block->marker,
			QRect(0, 0, markerTextWidth, markerTextHeight),
			st.body);
		block->markerRect = QRect(
			markerLeft,
			markerBaseline - markerLeafBaseline,
			markerTextWidth,
			markerTextHeight);
	} else {
		const auto center = BulletMarkerCenter(left, markerBaseline, st);
		block->markerCenter = context.rtl
			? QPoint(left + width - (center.x() - left), center.y())
			: center;
	}
	block->contentRect = QRect(bodyLeft, top, bodyWidth, rowHeight);
	block->horizontalScrollMax = scrollOwner
		? std::max(bodyLogicalWidth - bodyWidth, 0)
		: 0;
	if (scrollOwner) {
		block->scrollViewportRect = block->contentRect;
		block->scrollLogicalContentRect = QRect(
			bodyLeft,
			top,
			bodyLogicalWidth,
			rowHeight);
		if (block->horizontalScrollMax > 0) {
			block->scrollScrollbarTrackRect = QRect(
				bodyLeft,
				top + rowHeight + st.table.scrollbarSkip,
				bodyWidth,
				st.table.scrollbarHeight);
		}
	}
	block->outer = QRect(
		left,
		top,
		std::max(width, 1),
		rowHeight + ScrollbarReserveHeight(
			scrollOwner,
			block->horizontalScrollMax,
			st));
	block->firstLineBaseline = markerBaseline;
	RefreshLogicalGeometry(block);
	return BlockBottom(*block);
}

[[nodiscard]] std::optional<int> LayoutListBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context,
		const LayoutListChildCallback &layoutChild) {
	if (!block) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto depthDelta = std::max(prepared.visualDepth - context.listDepth, 0);
	const auto markerColumn = context.listItemContentShift;
	const auto step = std::max(st.textPadding.left(), markerColumn);
	const auto indent = depthDelta * step - markerColumn;
	const auto listLeft = context.rtl ? left : (left + indent);
	const auto listWidth = std::max(width - indent, 1);
	const auto currentScrollOwner = NextActiveScrollOwner(
		analysis,
		activeScrollOwner);
	const auto scrollOwner = IsActiveScrollOwner(analysis, currentScrollOwner);
	const auto outerLogicalWidth = LogicalOuterWidth(
		analysis,
		currentScrollOwner,
		logicalWidth);
	const auto listLogicalWidth = std::max(outerLogicalWidth - indent, 1);
	const auto childActiveScrollOwner = currentScrollOwner;
	const auto listLayoutWidth = ChildLayoutWidth(
		childActiveScrollOwner,
		listWidth,
		listLogicalWidth);
	auto childContext = context;
	childContext.listDepth = prepared.visualDepth;
	childContext.tightList = false;
	PrepareNestedContext(&childContext, listLeft, listLayoutWidth);
	auto y = top;
	auto previous = static_cast<const PreparedBlock*>(nullptr);
	for (auto i = 0, count = int(prepared.children.size()); i != count; ++i) {
		const auto &child = prepared.children[i];
		const auto anchorOnly = IsAnchorOnlyBlock(child);
		if (previous && !anchorOnly) {
			y += prepared.tight ? 0 : BlockSkip(child, st);
		}
		const auto childBottom = layoutChild(
			i,
			childActiveScrollOwner,
			listLeft,
			y,
			listLayoutWidth,
			listLogicalWidth,
			childContext,
			prepared.tight);
		if (!childBottom) {
			return std::nullopt;
		}
		y = *childBottom;
		if (!anchorOnly) {
			previous = &child;
		}
	}
	block->outer = QRect(
		listLeft,
		top,
		listWidth,
		std::max(y - top, 0));
	block->contentRect = block->outer;
	block->horizontalScrollMax = scrollOwner
		? std::max(listLogicalWidth - listWidth, 0)
		: 0;
	if (scrollOwner) {
		block->scrollViewportRect = block->contentRect;
		block->scrollLogicalContentRect = QRect(
			listLeft,
			top,
			listLogicalWidth,
			block->contentRect.height());
		if (block->horizontalScrollMax > 0) {
			block->scrollScrollbarTrackRect = QRect(
				listLeft,
				block->contentRect.y()
					+ block->contentRect.height()
					+ st.table.scrollbarSkip,
				listWidth,
				st.table.scrollbarHeight);
			block->outer.setHeight(
				block->outer.height()
					+ ScrollbarReserveHeight(
						scrollOwner,
						block->horizontalScrollMax,
						st));
		}
	}
	block->firstLineBaseline = ResolveFirstDisplayedLineBaseline(*block, st);
	RefreshLogicalGeometry(block);
	return BlockBottom(*block);
}

[[nodiscard]] std::optional<int> LayoutQuoteBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context,
		const LayoutNestedBlocksCallback &layoutNestedBlocks) {
	if (!block) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto depthDelta = std::max(
		prepared.visualDepth - context.quoteDepth,
		0);
	const auto quoteLeft = left + depthDelta * st.quoteIndent;
	const auto quoteWidth = std::max(
		width - depthDelta * st.quoteIndent,
		1);
	const auto scrollOwner = IsActiveScrollOwner(analysis, activeScrollOwner);
	const auto outerLogicalWidth = LogicalOuterWidth(
		analysis,
		activeScrollOwner,
		logicalWidth);
	const auto quoteLogicalWidth = std::max(
		outerLogicalWidth - depthDelta * st.quoteIndent,
		1);
	const auto padding = prepared.pullquote
		? st.pullquote.padding
		: BlockquotePadding(st.body.blockquote);
	const auto pullquoteReserveWidth = prepared.pullquote
		? PullquoteIconReserveWidth(st.body.blockquote)
		: 0;
	const auto availableWidth = std::max(
		quoteWidth
			- padding.left()
			- padding.right()
			- 2 * pullquoteReserveWidth,
		1);
	const auto contentWidth = prepared.pullquote
		? std::min(availableWidth, std::max(st.pullquote.maxWidth, 1))
		: availableWidth;
	const auto logicalAvailableWidth = std::max(
		quoteLogicalWidth
			- padding.left()
			- padding.right()
			- 2 * pullquoteReserveWidth,
		1);
	const auto contentLogicalWidth = prepared.pullquote
		? std::min(logicalAvailableWidth, std::max(st.pullquote.maxWidth, 1))
		: logicalAvailableWidth;
	const auto contentLeft = prepared.pullquote
		? (quoteLeft
			+ padding.left()
			+ pullquoteReserveWidth
			+ ((availableWidth - contentWidth) / 2))
		: (quoteLeft + padding.left());
	const auto contentTop = top + padding.top();
	const auto childActiveScrollOwner = NextActiveScrollOwner(
		analysis,
		activeScrollOwner);
	const auto contentLayoutWidth = ChildLayoutWidth(
		childActiveScrollOwner,
		contentWidth,
		contentLogicalWidth);
	auto childContext = context;
	childContext.quoteDepth = prepared.visualDepth + 1;
	childContext.tightList = false;
	PrepareNestedContext(&childContext, contentLeft, contentLayoutWidth);
	childContext.hideEmptyQuoteAuthor = QuoteBodyEmptyForLayout(
		prepared,
		childContext);
	const auto childBottom = layoutNestedBlocks(
		prepared.children,
		&block->children,
		analysis.children,
		childActiveScrollOwner,
		contentLeft,
		contentTop,
		contentLayoutWidth,
		contentLogicalWidth,
		childContext);
	if (!childBottom) {
		return std::nullopt;
	}
	const auto contentHeight = std::max(
		*childBottom - contentTop,
		prepared.children.empty()
			? TextLineHeight(st.body)
			: 0);
	block->textWidth = contentWidth;
	block->horizontalScrollMax = scrollOwner
		? std::max(contentLogicalWidth - contentWidth, 0)
		: 0;
	if (scrollOwner) {
		block->scrollViewportRect = QRect(
			contentLeft,
			contentTop,
			contentWidth,
			contentHeight);
		block->scrollLogicalContentRect = QRect(
			contentLeft,
			contentTop,
			contentLogicalWidth,
			contentHeight);
		if (block->horizontalScrollMax > 0) {
			block->scrollScrollbarTrackRect = QRect(
				contentLeft,
				contentTop + contentHeight + st.table.scrollbarSkip,
				contentWidth,
				st.table.scrollbarHeight);
		}
	}
	const auto quoteHeight = padding.top()
		+ contentHeight
		+ padding.bottom()
		+ ScrollbarReserveHeight(
			scrollOwner,
			block->horizontalScrollMax,
			st);
	auto outerLeft = quoteLeft;
	auto outerWidth = quoteWidth;
	auto finalContentLeft = contentLeft;
	auto finalContentWidth = contentWidth;
	if (prepared.pullquote) {
		outerWidth = padding.left()
			+ 2 * pullquoteReserveWidth
			+ contentWidth
			+ padding.right();
		outerLeft = quoteLeft + ((quoteWidth - outerWidth) / 2);
		const auto tightTextWidth = TightPullquoteTextWidth(
			block->children,
			context.editableMaxLineWidthOverride.get());
		if ((tightTextWidth > 0) && (tightTextWidth < contentWidth)) {
			finalContentWidth = tightTextWidth;
			finalContentLeft = quoteLeft
				+ padding.left()
				+ pullquoteReserveWidth
				+ ((availableWidth - finalContentWidth) / 2);
			for (auto &child : block->children) {
				RecenterPullquoteChild(
					&child,
					finalContentLeft,
					finalContentWidth);
			}
			outerWidth = padding.left()
				+ 2 * pullquoteReserveWidth
				+ finalContentWidth
				+ padding.right();
			outerLeft = quoteLeft + ((quoteWidth - outerWidth) / 2);
		}
	}
	block->outer = QRect(outerLeft, top, outerWidth, quoteHeight);
	block->contentRect = QRect(
		finalContentLeft,
		contentTop,
		finalContentWidth,
		contentHeight);
	block->firstLineBaseline = ResolveFirstDisplayedLineBaseline(*block, st);
	RefreshLogicalGeometry(block);
	return BlockBottom(*block);
}

[[nodiscard]] std::optional<int> LayoutDetailsBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context,
		const LayoutNestedBlocksCallback &layoutNestedBlocks) {
	if (!block) {
		return std::nullopt;
	}
	const auto usePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (MissingRetainedLeaf(prepared.text.text, block->leaf)
		|| (usePlaceholder && block->placeholderLeaf.isEmpty())) {
		return std::nullopt;
	}
	const auto actionWidth = context.editMode
		? DetailsStateReserveWidth(st)
		: 0;
	if (actionWidth > 0 && block->actionLeaf.isEmpty()) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	block->rtl = context.rtl;
	const auto &details = st.details;
	const auto headerPadding = DetailsHeaderPadding(context, st);
	const auto bodyPadding = DetailsBodyPadding(context, st);
	const auto headerWidth = std::max(width, 1);
	const auto scrollOwner = IsActiveScrollOwner(analysis, activeScrollOwner);
	const auto outerLogicalWidth = LogicalOuterWidth(
		analysis,
		activeScrollOwner,
		logicalWidth);
	const auto iconWidth = details.icon.empty()
		? 0
		: TextLineHeight(details.summaryStyle);
	const auto iconHeight = iconWidth;
	const auto iconSkip = iconWidth ? details.iconSkip : 0;
	const auto actionSkip = actionWidth ? details.stateSkip : 0;
	const auto actionZoneWidth = actionSkip + actionWidth;
	const auto textLeft = context.rtl
		? (left + headerPadding.right() + actionZoneWidth)
		: (left
			+ headerPadding.left()
			+ iconWidth
			+ iconSkip);
	block->textWidth = std::max(
		headerWidth
			- headerPadding.left()
			- headerPadding.right()
			- iconWidth
			- iconSkip
			- actionZoneWidth,
		1);
	const auto &displayLeaf = usePlaceholder
		? block->placeholderLeaf
		: block->leaf;
	const auto summaryHeight = ResolveEditableHeight(
		RetainedLeafHeight(
			displayLeaf,
			details.summaryStyle,
			block->textWidth),
		context);
	auto actionHeight = 0;
	if (actionWidth > 0) {
		block->actionWidth = actionWidth;
		actionHeight = RetainedLeafHeight(
			block->actionLeaf,
			details.summaryStyle,
			block->actionWidth);
	}
	const auto headerContentHeight = std::max(summaryHeight, iconHeight);
	const auto headerHeight = headerPadding.top()
		+ headerContentHeight
		+ headerPadding.bottom();
	block->headerRect = QRect(left, top, headerWidth, headerHeight);
	if (iconWidth > 0 && iconHeight > 0) {
		const auto iconLeft = context.rtl
			? (left + headerWidth - headerPadding.left() - iconWidth)
			: (left + headerPadding.left());
		block->iconRect = QRect(
			iconLeft,
			top + (headerHeight - iconHeight) / 2,
			iconWidth,
			iconHeight);
	}
	block->textRect = QRect(
		textLeft,
		top + headerPadding.top()
			+ std::max((headerContentHeight - summaryHeight) / 2, 0),
		block->textWidth,
		summaryHeight);
	if (actionZoneWidth > 0 && actionHeight > 0) {
		const auto actionLeft = context.rtl
			? (left + headerPadding.right())
			: (left + headerWidth - headerPadding.right() - actionZoneWidth);
		block->actionRect = QRect(
			actionLeft,
			top + headerPadding.top()
				+ std::max((headerContentHeight - actionHeight) / 2, 0),
			actionZoneWidth,
			actionHeight);
	}
	block->firstLineBaseline = LeafFirstLineBaseline(
		displayLeaf,
		block->textRect,
		details.summaryStyle);
	const auto childActiveScrollOwner = NextActiveScrollOwner(
		analysis,
		activeScrollOwner);
	auto bottom = top + headerHeight;
	if (!prepared.collapsed) {
		const auto childLeft = context.rtl
			? (left + bodyPadding.right())
			: (left + bodyPadding.left());
		const auto childTop = bottom + bodyPadding.top();
		const auto childWidth = std::max(
			headerWidth
				- bodyPadding.left()
				- bodyPadding.right(),
			1);
		const auto childLogicalWidth = std::max(
			outerLogicalWidth
				- bodyPadding.left()
				- bodyPadding.right(),
			1);
		const auto childLayoutWidth = ChildLayoutWidth(
			childActiveScrollOwner,
			childWidth,
			childLogicalWidth);
		auto childContext = context;
		PrepareNestedContext(&childContext, childLeft, childLayoutWidth);
		const auto childBottom = layoutNestedBlocks(
			prepared.children,
			&block->children,
			analysis.children,
			childActiveScrollOwner,
			childLeft,
			childTop,
			childLayoutWidth,
			childLogicalWidth,
			childContext);
		if (!childBottom) {
			return std::nullopt;
		}
		const auto contentHeight = std::max(*childBottom - childTop, 0);
		const auto bodyHeight = bodyPadding.top()
			+ contentHeight
			+ bodyPadding.bottom();
		block->bodyRect = QRect(left, bottom, headerWidth, bodyHeight);
		block->contentRect = QRect(
			childLeft,
			childTop,
			childWidth,
			contentHeight);
		block->horizontalScrollMax = scrollOwner
			? std::max(childLogicalWidth - childWidth, 0)
			: 0;
		if (scrollOwner) {
			block->scrollViewportRect = block->contentRect;
			block->scrollLogicalContentRect = QRect(
				childLeft,
				childTop,
				childLogicalWidth,
				contentHeight);
			if (block->horizontalScrollMax > 0) {
				block->scrollScrollbarTrackRect = QRect(
					childLeft,
					childTop + contentHeight + st.table.scrollbarSkip,
					childWidth,
					st.table.scrollbarHeight);
			}
		}
		bottom += bodyHeight
			+ ScrollbarReserveHeight(
				scrollOwner,
				block->horizontalScrollMax,
				st);
	}
	block->outer = QRect(
		left,
		top,
		headerWidth,
		std::max(bottom - top, headerHeight));
	RefreshLogicalGeometry(block);
	return BlockBottom(*block);
}

[[nodiscard]] std::optional<int> LayoutEmbedPostBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context,
		const LayoutNestedBlocksCallback &layoutNestedBlocks) {
	if (!block
		|| MissingRetainedLeaf(prepared.embedPost.author, block->labelLeaf)
		|| MissingRetainedLeaf(prepared.embedPost.dateText, block->subtitleLeaf)
		|| (prepared.embedPost.authorPhotoId && !block->photoRuntime)) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto &style = st.embedPost;
	const auto blockWidth = std::max(width, 1);
	const auto scrollOwner = IsActiveScrollOwner(analysis, activeScrollOwner);
	const auto outerLogicalWidth = LogicalOuterWidth(
		analysis,
		activeScrollOwner,
		logicalWidth);
	const auto contentLeft = left
		+ style.accentWidth
		+ style.accentSkip
		+ style.padding.left();
	const auto contentTop = top + style.padding.top();
	const auto contentWidth = std::max(
		blockWidth
			- style.accentWidth
			- style.accentSkip
			- style.padding.left()
			- style.padding.right(),
		1);
	const auto contentLogicalWidth = std::max(
		outerLogicalWidth
			- style.accentWidth
			- style.accentSkip
			- style.padding.left()
			- style.padding.right(),
		1);
	const auto hasAvatar = (block->photoRuntime != nullptr);
	const auto avatarSize = hasAvatar ? std::max(style.avatarSize, 1) : 0;
	const auto headerGap = hasAvatar ? style.headerGap : 0;
	const auto textLeft = contentLeft + avatarSize + headerGap;
	const auto textWidth = std::max(contentWidth - avatarSize - headerGap, 1);
	const auto childActiveScrollOwner = NextActiveScrollOwner(
		analysis,
		activeScrollOwner);
	auto authorHeight = 0;
	if (!prepared.embedPost.author.isEmpty()) {
		block->labelWidth = textWidth;
		authorHeight = ResolveEditableHeight(
			RetainedLeafHeight(
				block->labelLeaf,
				style.authorStyle,
				textWidth),
			context);
	}
	auto dateHeight = 0;
	if (!prepared.embedPost.dateText.isEmpty()) {
		block->subtitleWidth = textWidth;
		dateHeight = ResolveEditableHeight(
			RetainedLeafHeight(
				block->subtitleLeaf,
				style.dateStyle,
				textWidth),
			context);
	}
	const auto textHeight = authorHeight + dateHeight;
	const auto headerHeight = std::max(textHeight, avatarSize);
	if (headerHeight > 0) {
		block->headerRect = QRect(contentLeft, contentTop, contentWidth, headerHeight);
	}
	if (avatarSize > 0) {
		block->thumbnailRect = QRect(
			contentLeft,
			contentTop + std::max((headerHeight - avatarSize) / 2, 0),
			avatarSize,
			avatarSize);
	}
	auto textTop = contentTop + std::max((headerHeight - textHeight) / 2, 0);
	if (authorHeight > 0) {
		block->labelRect = QRect(textLeft, textTop, textWidth, authorHeight);
		textTop += authorHeight;
	}
	if (dateHeight > 0) {
		block->subtitleRect = QRect(textLeft, textTop, textWidth, dateHeight);
	}
	auto wrapperBottom = contentTop + headerHeight;
	if (!prepared.children.empty()) {
		const auto bodyTop = wrapperBottom + ((headerHeight > 0) ? style.bodySkip : 0);
		const auto contentLayoutWidth = ChildLayoutWidth(
			childActiveScrollOwner,
			contentWidth,
			contentLogicalWidth);
		auto childContext = context;
		PrepareNestedContext(&childContext, contentLeft, contentLayoutWidth);
		const auto childBottom = layoutNestedBlocks(
			prepared.children,
			&block->children,
			analysis.children,
			childActiveScrollOwner,
			contentLeft,
			bodyTop,
			contentLayoutWidth,
			contentLogicalWidth,
			childContext);
		if (!childBottom) {
			return std::nullopt;
		}
		const auto bodyHeight = std::max(*childBottom - bodyTop, 0);
		block->bodyRect = QRect(contentLeft, bodyTop, contentWidth, bodyHeight);
		wrapperBottom = std::max(wrapperBottom, *childBottom);
		block->horizontalScrollMax = scrollOwner
			? std::max(contentLogicalWidth - contentWidth, 0)
			: 0;
		if (scrollOwner) {
			block->scrollViewportRect = block->bodyRect;
			block->scrollLogicalContentRect = QRect(
				contentLeft,
				bodyTop,
				contentLogicalWidth,
				bodyHeight);
			if (block->horizontalScrollMax > 0) {
				block->scrollScrollbarTrackRect = QRect(
					contentLeft,
					bodyTop + bodyHeight + st.table.scrollbarSkip,
					contentWidth,
					st.table.scrollbarHeight);
				wrapperBottom = std::max(
					wrapperBottom,
					block->scrollScrollbarTrackRect.y()
						+ block->scrollScrollbarTrackRect.height());
			}
		}
	}
	block->contentRect = QRect(
		contentLeft,
		contentTop,
		contentWidth,
		std::max(wrapperBottom - contentTop, 0));
	block->mediaRect = QRect(
		left,
		top,
		blockWidth,
		style.padding.top()
			+ block->contentRect.height()
			+ style.padding.bottom());
	auto bottom = block->mediaRect.y() + block->mediaRect.height();
	if (!LayoutMediaCaptionGeometry(
			block,
			prepared,
			st,
			left,
			bottom,
			blockWidth,
			style.captionSkip,
			&bottom,
			context)) {
		return std::nullopt;
	}
	block->outer = QRect(
		left,
		top,
		blockWidth,
		std::max(bottom - top, block->mediaRect.height()));
	if (!block->labelRect.isEmpty() && !block->labelLeaf.isEmpty()) {
		block->firstLineBaseline = LeafFirstLineBaseline(
			block->labelLeaf,
			block->labelRect,
			style.authorStyle);
	} else if (!block->subtitleRect.isEmpty() && !block->subtitleLeaf.isEmpty()) {
		block->firstLineBaseline = LeafFirstLineBaseline(
			block->subtitleLeaf,
			block->subtitleRect,
			style.dateStyle);
	} else {
		for (const auto &child : block->children) {
			if (child.outer.height() <= 0) {
				continue;
			}
			block->firstLineBaseline = ResolveFirstDisplayedLineBaseline(
				child,
				st);
			break;
		}
		if (block->firstLineBaseline < 0) {
			block->firstLineBaseline = MarkdownBodyBaseline(top, st);
		}
	}
	RefreshLogicalGeometry(block);
	return BlockBottom(*block);
}

[[nodiscard]] std::optional<int> RecountBlockInPlace(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		LaidOutBlock *block,
		const WidthAnalysisNode &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context) {
	const auto currentScrollOwner = NextActiveScrollOwner(
		analysis,
		activeScrollOwner);
	const auto scrollOwner = IsActiveScrollOwner(analysis, currentScrollOwner);
	const auto effectiveLogicalWidth = LogicalOuterWidth(
		analysis,
		currentScrollOwner,
		logicalWidth);
	switch (prepared.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::DisplayMath:
	case PreparedBlockKind::Table:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Placeholder:
		return RecountSimpleLaidOutBlock(
			prepared,
			formulas,
			block,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			scrollOwner,
			context);
	case PreparedBlockKind::List:
		return LayoutListBlockGeometry(
			prepared,
			block,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context,
			[&](int index,
					const WidthAnalysisNode *childActiveScrollOwner,
					int childLeft,
					int childTop,
					int childWidth,
					int childLogicalWidth,
					LayoutContext childContext,
					bool tight) -> std::optional<int> {
				const auto &child = prepared.children[index];
				return (child.kind == PreparedBlockKind::ListItem)
					? LayoutListItemBlockGeometry(
						child,
						&block->children[index],
						analysis.children[index],
						childActiveScrollOwner,
						st,
						childLeft,
						childTop,
						childWidth,
						childLogicalWidth,
						childContext,
						tight,
						RetainedNestedBlocksCallback(formulas, st))
					: RecountBlockInPlace(
						child,
						formulas,
						&block->children[index],
						analysis.children[index],
						childActiveScrollOwner,
						st,
						childLeft,
						childTop,
						childWidth,
						childLogicalWidth,
						childContext);
			});
	case PreparedBlockKind::ListItem:
		return LayoutListItemBlockGeometry(
			prepared,
			block,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context,
			false,
			RetainedNestedBlocksCallback(formulas, st));
	case PreparedBlockKind::Quote:
		return LayoutQuoteBlockGeometry(
			prepared,
			block,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context,
			RetainedNestedBlocksCallback(formulas, st));
	case PreparedBlockKind::Details:
		return LayoutDetailsBlockGeometry(
			prepared,
			block,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context,
			RetainedNestedBlocksCallback(formulas, st));
	case PreparedBlockKind::EmbedPost:
		return LayoutEmbedPostBlockGeometry(
			prepared,
			block,
			analysis,
			currentScrollOwner,
			st,
			left,
			top,
			width,
			effectiveLogicalWidth,
			context,
			RetainedNestedBlocksCallback(formulas, st));
	}
	return std::nullopt;
}

[[nodiscard]] const style::TextStyle &LaidOutFlowTextStyle(
		const LaidOutBlock &block,
		const style::Markdown &st) {
	if (block.footer) {
		return st.footer;
	} else if (block.kind != PreparedBlockKind::Heading) {
		return st.body;
	}
	switch (std::clamp(block.headingLevel, 1, 6)) {
	case 1: return st.heading1;
	case 2: return st.heading2;
	case 3: return st.heading3;
	case 4: return st.heading4;
	case 5: return st.heading5;
	case 6: return st.heading6;
	}
	return st.heading6;
}

[[nodiscard]] int BlockContentMaxRight(
		const LaidOutBlock &block,
		const style::Markdown &st,
		bool rtl) {
	const auto outerRight = block.outer.x() + block.outer.width();
	const auto textRight = [&] {
		const auto &leaf = block.placeholderLeaf.isEmpty()
			? block.leaf
			: block.placeholderLeaf;
		return (leaf.isEmpty() || block.textRect.isEmpty())
			? 0
			: (block.textRect.x()
				+ std::min(leaf.maxWidth(), block.textRect.width()));
	};
	const auto childrenRight = [&] {
		auto result = 0;
		for (const auto &child : block.children) {
			result = std::max(result, BlockContentMaxRight(child, st, rtl));
		}
		return result;
	};
	if (block.insideHorizontalScroll
		|| (!block.scrollViewportRect.isEmpty()
			&& block.horizontalScrollMax > 0)) {
		return outerRight;
	}
	switch (block.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading: {
		if (block.flowTextAlign != style::al_left) {
			return outerRight;
		}
		const auto &leaf = block.placeholderLeaf.isEmpty()
			? block.leaf
			: block.placeholderLeaf;
		if (leaf.isEmpty() || block.textRect.isEmpty()) {
			return 0;
		}
		const auto width = std::min(
			std::max(
				leaf.maxWidth(),
				ReadableTextMinWidth(LaidOutFlowTextStyle(block, st))),
			block.textRect.width());
		return std::min(block.textRect.x() + width, outerRight);
	}
	case PreparedBlockKind::CodeBlock:
		return std::min(
			std::max(
				textRight() + BlockquotePadding(st.code.pre).right(),
				block.outer.x() + CodeBlockMinimumWidth(st)),
			outerRight);
	case PreparedBlockKind::Rule:
		return block.outer.x();
	case PreparedBlockKind::DisplayMath: {
		if (!block.textRect.isEmpty()) {
			return outerRight;
		}
		const auto &padding = st.displayMath.padding;
		const auto formulaWidth = block.formulaRect.isEmpty()
			? 1
			: block.formulaRect.width();
		return std::min(
			block.outer.x()
				+ padding.left()
				+ formulaWidth
				+ padding.right(),
			outerRight);
	}
	case PreparedBlockKind::Table: {
		const auto tableRight = block.tableRect.isEmpty()
			? 0
			: (block.tableRect.x() + block.tableRect.width());
		return std::min(std::max(textRight(), tableRight), outerRight);
	}
	case PreparedBlockKind::List:
		return std::min(childrenRight(), outerRight);
	case PreparedBlockKind::ListItem:
		return rtl
			? outerRight
			: std::min(
				std::max(
					block.outer.x() + block.markerWidth,
					childrenRight()),
				outerRight);
	case PreparedBlockKind::Quote:
		return block.pullquote
			? outerRight
			: std::min(
				childrenRight()
					+ BlockquotePadding(st.body.blockquote).right(),
				outerRight);
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Placeholder:
	case PreparedBlockKind::Details:
	case PreparedBlockKind::EmbedPost:
		return outerRight;
	}
	return outerRight;
}

[[nodiscard]] std::optional<int> RecountBlocksInPlace(
		const std::vector<PreparedBlock> &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		std::vector<LaidOutBlock> *blocks,
		const std::vector<WidthAnalysisNode> &analysis,
		const WidthAnalysisNode *activeScrollOwner,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		LayoutContext context) {
	if (!blocks
		|| prepared.size() != blocks->size()
		|| prepared.size() != analysis.size()) {
		return std::nullopt;
	}
	auto y = top;
	auto previous = static_cast<const PreparedBlock*>(nullptr);
	for (auto i = 0, count = int(prepared.size()); i != count; ++i) {
		const auto &preparedBlock = prepared[i];
		auto &live = (*blocks)[i];
		const auto anchorOnly = IsAnchorOnlyBlock(preparedBlock);
		const auto next = NextVisibleBlock(prepared, i);
		auto blockContext = context;
		blockContext.preparedPath.push_back(i);
		if (HideEmptyQuoteAuthorBlock(preparedBlock, blockContext)) {
			live = HiddenQuoteAuthorBlock(preparedBlock);
			continue;
		}
		if (previous && !anchorOnly) {
			y += BlockSkip(*previous, preparedBlock, context, st);
		}
		const auto band = BlockBand(
			preparedBlock.kind,
			st,
			left,
			std::max(width, 1),
			context);
		const auto logicalBandWidth = BlockBandWidth(
			preparedBlock.kind,
			st,
			logicalWidth,
			context);
		const auto bottom = IsRelatedArticlesHeader(preparedBlock, next)
			? RecountSimpleLaidOutBlock(
				preparedBlock,
				formulas,
				&live,
				st,
				left + st.relatedArticle.headerPadding.left(),
				y + st.relatedArticle.headerPadding.top(),
				std::max(
					width
						- st.relatedArticle.headerPadding.left()
						- st.relatedArticle.headerPadding.right(),
					1),
				std::max(
					logicalWidth
						- st.relatedArticle.headerPadding.left()
						- st.relatedArticle.headerPadding.right(),
					1),
				false,
				blockContext)
			: RecountBlockInPlace(
				preparedBlock,
				formulas,
				&live,
				analysis[i],
				activeScrollOwner,
				st,
				band.x(),
				y,
				band.width(),
				logicalBandWidth,
				blockContext);
		if (!bottom) {
			return std::nullopt;
		}
		if (IsRelatedArticlesHeader(preparedBlock, next)) {
			live.headerRect = QRect(
				left,
				y,
				std::max(width, 1),
				live.outer.height()
					+ st.relatedArticle.headerPadding.top()
					+ st.relatedArticle.headerPadding.bottom());
			live.outer = live.headerRect;
			live.contentRect = live.headerRect;
			RefreshLogicalGeometry(&live);
		}
		y = BlockBottom(live);
		if (!anchorOnly) {
			previous = &preparedBlock;
		}
	}
	return y;
}

} // namespace

int LayoutBlocks(
		const std::vector<PreparedBlock> &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		std::vector<LaidOutBlock> *blocks,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	const auto analysis = AnalyzeBlocks(
		prepared,
		*formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		width,
		context);
	return LayoutBlocks(
		prepared,
		formulas,
		renderedFormulas,
		renderer,
		inlineFormulaObjects,
		mediaRuntime,
		blocks,
		analysis,
		nullptr,
		st,
		left,
		top,
		width,
		width,
		context);
}

std::optional<int> RecountLaidOutBlocks(
		const std::vector<PreparedBlock> &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		std::vector<LaidOutBlock> *blocks,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	if (!blocks) {
		return std::nullopt;
	}
	auto analysis = AnalyzeRetainedBlocks(
		prepared,
		*blocks,
		formulas,
		st,
		width,
		context);
	if (!analysis) {
		return std::nullopt;
	}
	return RecountBlocksInPlace(
		prepared,
		formulas,
		blocks,
		*analysis,
		nullptr,
		st,
		left,
		top,
		width,
		width,
		context);
}

int ArticleContentMaxRight(
		const std::vector<LaidOutBlock> &blocks,
		const style::Markdown &st,
		bool rtl) {
	auto result = 0;
	for (const auto &block : blocks) {
		const auto bandPadding = UsesMediaBand(block.kind)
			? st.mediaPadding
			: st.textPadding;
		result = std::max(
			result,
			BlockContentMaxRight(block, st, rtl) - bandPadding.left());
	}
	return result;
}

} // namespace Iv::Markdown
