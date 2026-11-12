/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_layout_blocks.h"

#include "base/algorithm.h"
#include "ui/text/text_renderer.h"

namespace Iv::Markdown {

int ConsumeQuoteFoldLines(
		QuoteFold *fold,
		const Ui::Text::String &leaf,
		int width) {
	if (!fold
		|| (fold->state != QuoteFoldState::Counting)
		|| (fold->remainingLines <= 0)) {
		return 0;
	}
	const auto geometry = leaf.countLinesGeometry(width);
	const auto count = int(geometry.size());
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
	return geometry[allowed - 1].bottom;
}

bool QuoteFoldHidesBlock(QuoteFold *fold, const PreparedBlock &block) {
	if (!fold || block.quoteAuthor) {
		return false;
	}
	const auto anchorOnly = IsAnchorOnlyBlock(block);
	if (fold->state == QuoteFoldState::Counting) {
		if (anchorOnly || IsFlowKind(block.kind)) {
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
		const auto &children = block.children;
		const auto hidden = [&](int index) {
			return !children[index].quoteAuthor
				&& live.children[index].outer.isNull();
		};
		const auto size = int(children.size());
		auto from = 0;
		auto segmentIndex = -1;
		for (; from != size && !hidden(from); ++from) {
			if (live.children[from].segmentIndex >= 0) {
				segmentIndex = live.children[from].segmentIndex;
			}
		}
		auto till = from;
		while (till != size && hidden(till)) {
			++till;
		}
		if (segmentIndex >= 0 && till > from) {
			ranges->push_back({
				.blocks = std::span(children).subspan(from, till - from),
				.toggleId = block.collapseToggleId,
				.segmentIndex = segmentIndex,
			});
		}
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
