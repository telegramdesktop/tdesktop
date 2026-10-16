/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_rich_quote.h"

#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/history_item.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/iv_rich_page.h"
#include "iv/iv_rich_page_quote.h"

#include "styles/style_chat.h"

namespace HistoryView {
namespace {

using MarkdownArticleSearchMatch = Iv::Markdown::MarkdownArticleSearchMatch;
using MarkdownArticleTextRange = Iv::Markdown::MarkdownArticleTextRange;

[[nodiscard]] std::vector<MarkdownArticleSearchMatch> RichQuoteDisplayRanges(
		not_null<const HistoryMessageRichPage*> rich,
		not_null<const HistoryItem*> item,
		const Ui::ChatPaintRichQuote &quote) {
	auto result = std::vector<MarkdownArticleSearchMatch>();
	const auto model = quote ? quote.page->quoteModel.get() : nullptr;
	if (!model
		|| (quote.page != rich->page)
		|| (quote.page != item->richPage())
		|| (&item->translatedText() != &item->originalText())) {
		return result;
	}
	const auto pieces = model->pageRanges({ quote.from, quote.till });
	auto group = std::vector<MarkdownArticleTextRange>();
	for (auto i = begin(pieces); i != end(pieces);) {
		const auto &leaf = i->leaf;
		group.clear();
		do {
			group.push_back({ .from = i->from, .till = i->till });
		} while (++i != end(pieces)
			&& i->leaf == leaf
			&& i->from >= group.back().till);
		rich->article.appendDisplayRangesForLeafText(leaf, group, &result);
	}
	ranges::sort(result, ranges::less(), [](const auto &match) {
		return std::make_pair(match.segment, match.from);
	});
	return result;
}

} // namespace

RichQuotePaint::RichQuotePaint(
		not_null<const HistoryMessageRichPage*> rich,
		not_null<const HistoryItem*> item,
		not_null<Iv::Markdown::MarkdownArticlePaintContext*> context) {
	if (!context->highlightPathCache
		|| (context->highlight.collapsion <= 0.)) {
		return;
	}
	_ranges = RichQuoteDisplayRanges(
		rich,
		item,
		context->highlight.richQuote);
	if (!_ranges.empty()) {
		_target = context->highlightPathCache;
		context->highlightPathCache = &_path;
		context->quoteRanges = &_ranges;
	}
}

void RichQuotePaint::finish(QPoint origin) {
	if (_target && !_path.isEmpty()) {
		_path.translate(origin);
		_target->addPath(_path);
	}
}

HighlightYRange RichQuoteHighlightYRange(
		not_null<const HistoryMessageRichPage*> rich,
		not_null<const HistoryItem*> item,
		const Ui::ChatPaintRichQuote &quote,
		int articleTop) {
	const auto ranges = RichQuoteDisplayRanges(rich, item, quote);
	const auto rect = ranges.empty()
		? QRect()
		: rich->article.displayRangesRect(ranges);
	if (rect.isEmpty()) {
		return {};
	}
	const auto top = articleTop + rect.y();
	const auto single = st::messageTextStyle.font->height;
	return { top - single, top + rect.height() + single };
}

} // namespace HistoryView
