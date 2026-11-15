/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_article_selection.h"

namespace Iv::Markdown {

struct QuoteLeafLookup {
	const MarkdownArticleContent *content = nullptr;
	const SelectableSegment *segment = nullptr;
	const PreparedBlock *block = nullptr;
	const PreparedTableCell *cell = nullptr;
};

struct QuoteSegmentGeometry {
	QRect logical;
	const LaidOutBlock *scrollOwner = nullptr;
};

void AppendDisplayRangesForLeafText(
	const QuoteLeafLookup &leaf,
	const PreparedEditLeafSource &source,
	std::span<const MarkdownArticleTextRange> ranges,
	not_null<std::vector<MarkdownArticleSearchMatch>*> result);
[[nodiscard]] std::optional<MarkdownArticleLeafPoint> LeafPointForDisplay(
	const QuoteLeafLookup &leaf,
	const PreparedEditLeafSource &source,
	int offset,
	MarkdownArticleTextEdge edge);
[[nodiscard]] int GroupedCaptionSegment(
	const std::vector<SelectableSegment> &segments,
	int segmentIndex);

void AppendQuoteTextHighlight(
	QPainter &p,
	const Ui::Text::String &leaf,
	const Ui::Text::PaintContext &leafContext,
	const MarkdownArticlePaintContext &context,
	int segmentIndex);
void AppendQuoteFormulaHighlight(
	QRect formula,
	const MarkdownArticlePaintContext &context,
	int segmentIndex);
[[nodiscard]] QRect DisplayRangesRect(
	const std::vector<SelectableSegment> &segments,
	const std::vector<MarkdownArticleSearchMatch> &ranges,
	const style::Markdown &st,
	float64 ratio,
	Fn<QuoteSegmentGeometry(int segmentIndex)> geometry);

} // namespace Iv::Markdown
