/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_rich_quote.h"

#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message_selection.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/iv_rich_page.h"
#include "iv/iv_rich_page_quote.h"
#include "iv/iv_rich_page_quote_builder.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

#include "styles/style_chat.h"

namespace HistoryView {
namespace {

using MarkdownArticle = Iv::Markdown::MarkdownArticle;
using MarkdownArticleSearchMatch = Iv::Markdown::MarkdownArticleSearchMatch;
using MarkdownArticleSelection = Iv::Markdown::MarkdownArticleSelection;
using MarkdownArticleTextEdge = Iv::Markdown::MarkdownArticleTextEdge;
using MarkdownArticleTextRange = Iv::Markdown::MarkdownArticleTextRange;

void AppendDisplayRanges(
		const MarkdownArticle &article,
		const std::vector<Iv::RichQuotePageRange> &pieces,
		not_null<std::vector<MarkdownArticleSearchMatch>*> result) {
	auto group = std::vector<MarkdownArticleTextRange>();
	for (auto i = begin(pieces); i != end(pieces);) {
		const auto &leaf = i->leaf;
		group.clear();
		do {
			group.push_back({ .from = i->from, .till = i->till });
		} while (++i != end(pieces)
			&& i->leaf == leaf
			&& i->from >= group.back().till);
		article.appendDisplayRangesForLeafText(leaf, group, result);
	}
	ranges::sort(*result, ranges::less(), [](const auto &match) {
		return std::make_pair(match.segment, match.from);
	});
}

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
	AppendDisplayRanges(
		rich->article,
		model->pageRanges({ quote.from, quote.till }),
		&result);
	return result;
}

[[nodiscard]] std::optional<int> ProjectionPoint(
		const MarkdownArticle &article,
		const Iv::RichQuoteModel &model,
		int segment,
		int offset,
		MarkdownArticleTextEdge edge) {
	const auto point = article.leafPointForDisplay(segment, offset, edge);
	if (!point) {
		return std::nullopt;
	}
	const auto atEnd = (edge == MarkdownArticleTextEdge::End);
	const auto found = (point->labelOffset >= 0)
		? model.labelPointAt(
			point->leaf,
			point->position,
			point->labelOffset,
			atEnd)
		: model.pointAt(point->leaf, point->position);
	if (!found) {
		return std::nullopt;
	}
	return atEnd ? found->till : found->from;
}

[[nodiscard]] bool FindSliceEdge(
		const MarkdownArticle &article,
		const Iv::RichQuoteModel &model,
		const MarkdownArticleSearchMatch &slice,
		MarkdownArticleTextEdge edge,
		not_null<std::optional<int>*> result) {
	const auto leaf = (slice.from < slice.to)
		? article.editableLeafForSegment(slice.segment)
		: std::nullopt;
	if (!leaf) {
		return true;
	}
	auto known = false;
	auto pieces = std::vector<Iv::RichQuotePageRange>();
	for (const auto &node : model.nodes()) {
		if (node.leaf == leaf) {
			const auto part = model.pageRanges({ node.from, node.till });
			pieces.insert(end(pieces), begin(part), end(part));
			known = true;
		}
	}
	if (!known) {
		return true;
	} else if (!article.leafPointForDisplay(
			slice.segment,
			slice.from,
			MarkdownArticleTextEdge::Start)
		|| !article.leafPointForDisplay(
			slice.segment,
			slice.to,
			MarkdownArticleTextEdge::End)) {
		return false;
	}
	auto coverage = std::vector<MarkdownArticleSearchMatch>();
	AppendDisplayRanges(article, pieces, &coverage);
	if (edge == MarkdownArticleTextEdge::End) {
		ranges::reverse(coverage);
	}
	for (const auto &match : coverage) {
		const auto from = std::max(match.from, slice.from);
		const auto till = std::min(match.to, slice.to);
		if ((match.segment != slice.segment) || (from >= till)) {
			continue;
		}
		const auto quoteFrom = ProjectionPoint(
			article,
			model,
			slice.segment,
			from,
			MarkdownArticleTextEdge::Start);
		const auto quoteTill = ProjectionPoint(
			article,
			model,
			slice.segment,
			till,
			MarkdownArticleTextEdge::End);
		if (!quoteFrom || !quoteTill) {
			return false;
		} else if (*quoteFrom < *quoteTill) {
			*result = (edge == MarkdownArticleTextEdge::Start)
				? *quoteFrom
				: *quoteTill;
			return true;
		}
	}
	return true;
}

[[nodiscard]] std::optional<Iv::RichQuoteRange> SelectionRange(
		const MarkdownArticle &article,
		const Iv::RichQuoteModel &model,
		MarkdownArticleSelection selection) {
	if (CompareMessageSelectionPositions(selection.from, selection.to) > 0) {
		std::swap(selection.from, selection.to);
	}
	const auto from = selection.from;
	const auto to = selection.to;
	const auto valid = [&](int segment) {
		return article.segmentIsText(segment)
			|| article.segmentIsDisplayMath(segment)
			|| (article.segmentLength(segment) > 0);
	};
	if (!valid(from.segment) || !valid(to.segment)) {
		return std::nullopt;
	}
	const auto slice = [&](int segment) {
		const auto length = article.segmentLength(segment);
		return MarkdownArticleSearchMatch{
			.segment = segment,
			.from = (segment == from.segment)
				? std::clamp(from.offset, 0, length)
				: 0,
			.to = (segment == to.segment)
				? std::clamp(to.offset, 0, length)
				: length,
		};
	};
	const auto find = [&](
			const MarkdownArticleSearchMatch &part,
			MarkdownArticleTextEdge edge,
			not_null<std::optional<int>*> result) {
		return FindSliceEdge(article, model, part, edge, result);
	};
	const auto grouped = [&](
			int segment,
			MarkdownArticleTextEdge edge,
			not_null<std::optional<int>*> result) {
		const auto caption = article.groupedCaptionSegment(segment);
		const auto part = slice(segment);
		if (caption < 0 || part.from >= part.to) {
			return true;
		}
		const auto whole = MarkdownArticleSearchMatch{
			.segment = caption,
			.from = 0,
			.to = article.segmentLength(caption),
		};
		return find(whole, edge, result);
	};
	auto quoteFrom = std::optional<int>();
	for (auto i = from.segment; !quoteFrom && i <= to.segment; ++i) {
		if (!find(slice(i), MarkdownArticleTextEdge::Start, &quoteFrom)) {
			return std::nullopt;
		}
	}
	if (!quoteFrom) {
		return std::nullopt;
	}
	auto quoteTill = std::optional<int>();
	for (auto i = to.segment; !quoteTill && i >= from.segment; --i) {
		if (!find(slice(i), MarkdownArticleTextEdge::End, &quoteTill)) {
			return std::nullopt;
		}
	}
	if (!quoteTill
		|| !grouped(from.segment, MarkdownArticleTextEdge::Start, &quoteFrom)
		|| !grouped(to.segment, MarkdownArticleTextEdge::End, &quoteTill)
		|| (*quoteFrom >= *quoteTill)) {
		return std::nullopt;
	}
	return Iv::RichQuoteRange{ .from = *quoteFrom, .till = *quoteTill };
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

std::optional<Iv::BuiltRichQuote> RichQuoteForSelection(
		not_null<const HistoryMessageRichPage*> rich,
		not_null<const HistoryItem*> item,
		const MarkdownArticleSelection &selection) {
	const auto page = rich->page.get();
	const auto model = page ? page->quoteModel.get() : nullptr;
	if (!model
		|| (rich->page != item->richPage())
		|| (&item->translatedText() != &item->originalText())) {
		return std::nullopt;
	}
	const auto range = SelectionRange(rich->article, *model, selection);
	if (!range) {
		return std::nullopt;
	}
	const auto session = &item->history()->session();
	return Iv::BuildRichQuote(
		session,
		*page,
		*range,
		session->appConfig().quoteLengthMax());
}

SelectedQuote Element::selectedReplyQuote(
		const MessageSelection &selection) const {
	if (!selection.isRichPage()) {
		return selectedQuote(selection);
	}
	const auto built = selectedRichQuote(selection);
	if (!built) {
		return {};
	}
	const auto item = textItem() ? textItem() : data().get();
	return {
		.item = item,
		.highlight = {
			.richQuote = Iv::ParseRichQuote(
				&item->history()->session(),
				built->fragments,
				built->offset),
		},
		.overflown = built->overflown,
	};
}

} // namespace HistoryView
