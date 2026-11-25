/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/algorithm.h"
#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "ui/text/text_renderer.h"

namespace Iv::Markdown {
namespace {

struct QuoteFoldScope {
	bool fold = false;
	bool listed = false;
	bool omitted = false;
};

[[nodiscard]] bool IsListKind(PreparedBlockKind kind) {
	return (kind == PreparedBlockKind::List)
		|| (kind == PreparedBlockKind::ListItem);
}

[[nodiscard]] bool IsQuoteFoldSlot(const LaidOutBlock &block) {
	return block.logicalGeometry.outer.isNull();
}

[[nodiscard]] bool QuoteFoldQualifies(const QuoteFoldScope &scope) {
	return scope.fold && scope.listed && scope.omitted;
}

[[nodiscard]] int AnalyzedRequiredWidth(const WidthAnalysisNode &analysis) {
	return std::max(
		analysis.outerMinimumWidth,
		analysis.subtreeNeedsScrollOwner
			? analysis.outerScrollViewportMinimumWidth
			: 1);
}

[[nodiscard]] int QuoteFrameFreeWidth(const LaidOutBlock &block) {
	const auto &geometry = block.logicalGeometry;
	return std::min(
		geometry.contentRect.width() - 1,
		geometry.outer.width() - geometry.collapseControlRect.width());
}

[[nodiscard]] int TrailingFreeWidth(
		const std::vector<PreparedBlock> &prepared,
		const std::vector<WidthAnalysisNode> &analysis,
		const std::vector<LaidOutBlock> &laid,
		int band,
		int result,
		QuoteFoldScope *scope,
		bool *qualified) {
	if ((prepared.size() != laid.size())
		|| (prepared.size() != analysis.size())) {
		return 0;
	}
	auto &&rows = ranges::views::zip(prepared, analysis, laid);
	for (const auto &[source, node, block] : rows) {
		const auto &geometry = block.logicalGeometry;
		if (result <= 0) {
			return 0;
		} else if (IsQuoteFoldSlot(block)) {
			if (!scope->fold || !scope->listed) {
				result = std::min(result, band - AnalyzedRequiredWidth(node));
			} else if (!scope->omitted) {
				scope->omitted = !source.quoteAuthor
					&& !IsAnchorOnlyBlock(source);
			}
		} else if (IsListKind(block.kind)) {
			const auto own = geometry.contentRect.width();
			scope->listed = true;
			result = TrailingFreeWidth(
				source.children,
				node.children,
				block.children,
				own,
				std::min(result, own - 1),
				scope,
				qualified);
		} else if ((block.kind == PreparedBlockKind::Quote)
			&& !block.pullquote) {
			auto inner = QuoteFoldScope{
				.fold = !block.collapseToggleId.isEmpty()
					&& block.collapsed
					&& block.collapsedLinesExceeded,
			};
			result = TrailingFreeWidth(
				source.children,
				node.children,
				block.children,
				geometry.contentRect.width(),
				std::min(result, QuoteFrameFreeWidth(block)),
				&inner,
				qualified);
			*qualified |= QuoteFoldQualifies(inner);
		} else if (block.kind == PreparedBlockKind::Rule) {
			result = std::min(result, geometry.outer.width() - 1);
		} else if (IsFlowKind(block.kind)
			&& (block.flowTextAlign == style::al_left)) {
			const auto &leaf = block.placeholderLeaf.isEmpty()
				? block.leaf
				: block.placeholderLeaf;
			result = std::min(
				result,
				block.textWidth - std::max(leaf.maxWidth(), 1));
		} else {
			return 0;
		}
	}
	return result;
}

void ShrinkQuoteFoldGeometry(
		LaidOutBlock *block,
		int delta,
		const style::QuoteStyle &quoteStyle,
		bool rtl) {
	if (IsQuoteFoldSlot(*block)) {
		return;
	}
	const auto control = QuoteHasCollapseControl(*block);
	const auto marker = rtl && (block->kind == PreparedBlockKind::ListItem);
	const auto shrink = [&](auto &geometry) {
		for (const auto rect : {
				&geometry.outer,
				&geometry.headerRect,
				&geometry.contentRect,
				&geometry.textRect }) {
			if (!rect->isNull()) {
				rect->setWidth(rect->width() - delta);
			}
		}
		if (control) {
			geometry.collapseControlRect = QuoteCollapseControlRect(
				geometry.outer,
				quoteStyle);
		}
		if (!marker) {
			return;
		} else if (geometry.markerRect.isNull()) {
			geometry.markerCenter.rx() -= delta;
		} else {
			geometry.markerRect.translate(-delta, 0);
		}
	};
	shrink(*block);
	shrink(block->logicalGeometry);
	if (IsFlowKind(block->kind) || (block->kind == PreparedBlockKind::Quote)) {
		block->textWidth -= delta;
	}
	for (auto &child : block->children) {
		ShrinkQuoteFoldGeometry(&child, delta, quoteStyle, rtl);
	}
}

void CollectQuoteFoldTail(
		const std::vector<PreparedBlock> &children,
		const std::vector<LaidOutBlock> &live,
		const QString &toggleId,
		int *segmentIndex,
		std::vector<QuoteFoldHiddenRange> *ranges) {
	const auto hidden = [&](int index) {
		return !children[index].quoteAuthor && IsQuoteFoldSlot(live[index]);
	};
	const auto size = int(children.size());
	auto from = 0;
	for (; from != size && !hidden(from); ++from) {
		const auto &child = children[from];
		const auto &laid = live[from];
		if (laid.segmentIndex >= 0) {
			*segmentIndex = laid.segmentIndex;
		} else if (IsListKind(child.kind)
			&& (laid.children.size() == child.children.size())) {
			CollectQuoteFoldTail(
				child.children,
				laid.children,
				toggleId,
				segmentIndex,
				ranges);
		}
	}
	auto till = from;
	while (till != size && hidden(till)) {
		++till;
	}
	if (*segmentIndex >= 0 && till > from) {
		ranges->push_back({
			.blocks = std::span(children).subspan(from, till - from),
			.toggleId = toggleId,
			.segmentIndex = *segmentIndex,
		});
	}
}

} // namespace

int ConsumeQuoteFoldLines(
		QuoteFold *fold,
		const std::vector<Ui::Text::LineLayoutInfo> &lines) {
	if (!fold
		|| (fold->state != QuoteFoldState::Counting)
		|| (fold->remainingLines <= 0)) {
		return 0;
	}
	const auto count = int(lines.size());
	if (count < fold->remainingLines) {
		fold->remainingLines -= count;
		return 0;
	}
	const auto allowed = base::take(fold->remainingLines);
	fold->state = QuoteFoldState::Ended;
	if (count == allowed) {
		return 0;
	}
	fold->exceeded = true;
	return lines[allowed - 1].bottom;
}

bool QuoteFoldHidesBlock(
		const LayoutContext &context,
		const PreparedBlock &block,
		bool anchorOnly,
		const WidthAnalysisNode &analysis) {
	const auto fold = context.quoteFold;
	if (!fold || block.quoteAuthor) {
		return false;
	} else if (fold->state == QuoteFoldState::Counting) {
		if (!analysis.foldBarrier) {
			return false;
		}
		fold->state = (fold->remainingLines < Ui::Text::kQuoteCollapsedLines)
			? QuoteFoldState::Ended
			: QuoteFoldState::Refused;
	}
	if (fold->state != QuoteFoldState::Ended) {
		return false;
	} else if (!anchorOnly) {
		fold->exceeded = true;
	}
	return fold->collapsed;
}

void SummarizeQuoteFoldOpening(
		const PreparedBlock &prepared,
		WidthAnalysisNode *analysis) {
	if (IsFlowKind(prepared.kind)) {
		return;
	} else if (!IsListKind(prepared.kind)) {
		analysis->foldBarrier = true;
		return;
	}
	const auto &children = analysis->children;
	const auto opening = ranges::find(
		children,
		false,
		&WidthAnalysisNode::foldZeroLines);
	analysis->foldZeroLines = (opening == end(children));
	analysis->foldBarrier = (prepared.kind == PreparedBlockKind::List)
		? (!children.empty() && children.front().foldBarrier)
		: ((opening != end(children)) && opening->foldBarrier);
}

int TightenQuoteFoldBand(
		const PreparedBlock &prepared,
		const WidthAnalysisNode &analysis,
		LaidOutBlock *block,
		const style::QuoteStyle &quoteStyle,
		int band,
		int excess,
		const LayoutContext &context) {
	if (excess <= 0) {
		return 0;
	}
	const auto fold = context.quoteFold;
	auto scope = QuoteFoldScope{
		.fold = fold && fold->collapsed && fold->exceeded,
		.listed = (prepared.kind == PreparedBlockKind::List),
	};
	auto qualified = false;
	const auto delta = TrailingFreeWidth(
		prepared.children,
		analysis.children,
		block->children,
		band,
		excess,
		&scope,
		&qualified);
	if (!(qualified || QuoteFoldQualifies(scope)) || (delta <= 0)) {
		return 0;
	}
	// Every surviving line fits the tighter band, so its lines stay valid.
	for (auto &child : block->children) {
		ShrinkQuoteFoldGeometry(&child, delta, quoteStyle, context.rtl);
	}
	return delta;
}

void AppendPreparedBlocksText(
		std::span<const PreparedBlock> blocks,
		QString *to) {
	const auto append = [&](const QString &text) {
		if (text.isEmpty()) {
			return;
		}
		if (!to->isEmpty()) {
			to->append('\n');
		}
		to->append(text);
	};
	for (const auto &block : blocks) {
		append(block.embedPost.author);
		append(block.embedPost.dateText);
		append(block.text.text);
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				append(cell.text.text);
			}
		}
		AppendPreparedBlocksText(block.children, to);
	}
}

void CollectQuoteFoldHiddenRanges(
		const std::vector<PreparedBlock> &prepared,
		const std::vector<LaidOutBlock> &laid,
		std::vector<QuoteFoldHiddenRange> *ranges) {
	const auto count = std::min(int(prepared.size()), int(laid.size()));
	for (auto i = 0; i != count; ++i) {
		const auto &block = prepared[i];
		const auto &live = laid[i];
		CollectQuoteFoldHiddenRanges(block.children, live.children, ranges);
		if (!live.collapsed
			|| !live.collapsedLinesExceeded
			|| block.collapseToggleId.isEmpty()
			|| (live.collapseToggleId != block.collapseToggleId)
			|| (live.children.size() != block.children.size())) {
			continue;
		}
		auto segmentIndex = -1;
		CollectQuoteFoldTail(
			block.children,
			live.children,
			block.collapseToggleId,
			&segmentIndex,
			ranges);
	}
}

MarkdownArticleAnchorExpansion ExpandQuoteFold(
		std::vector<PreparedBlock> *blocks,
		const QString &toggleId) {
	for (auto &block : *blocks) {
		auto result = MarkdownArticleAnchorExpansion();
		const auto fold = (block.kind == PreparedBlockKind::Quote)
			&& !block.collapseToggleId.isEmpty();
		if (fold && (block.collapseToggleId == toggleId)) {
			result.found = true;
		} else {
			result = ExpandQuoteFold(&block.children, toggleId);
		}
		if (result.found) {
			if ((fold || (block.kind == PreparedBlockKind::Details))
				&& block.collapsed) {
				block.collapsed = false;
				result.changed = true;
			}
			return result;
		}
	}
	return {};
}

} // namespace Iv::Markdown
