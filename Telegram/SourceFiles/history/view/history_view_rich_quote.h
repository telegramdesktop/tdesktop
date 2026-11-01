/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QPainterPath>

class HistoryItem;

namespace Iv {
struct BuiltRichQuote;
} // namespace Iv

namespace Iv::Markdown {
struct MarkdownArticlePaintContext;
struct MarkdownArticleSearchMatch;
struct MarkdownArticleSelection;
} // namespace Iv::Markdown

namespace Ui {
struct ChatPaintRichQuote;
} // namespace Ui

namespace HistoryView {

struct HistoryMessageRichPage;
struct HighlightYRange;

class RichQuotePaint final {
public:
	RichQuotePaint(
		not_null<const HistoryMessageRichPage*> rich,
		not_null<const HistoryItem*> item,
		not_null<Iv::Markdown::MarkdownArticlePaintContext*> context);
	RichQuotePaint(const RichQuotePaint &) = delete;
	RichQuotePaint &operator=(const RichQuotePaint &) = delete;

	void finish(QPoint origin);

private:
	std::vector<Iv::Markdown::MarkdownArticleSearchMatch> _ranges;
	QPainterPath _path;
	QPainterPath *_target = nullptr;

};

[[nodiscard]] HighlightYRange RichQuoteHighlightYRange(
	not_null<const HistoryMessageRichPage*> rich,
	not_null<const HistoryItem*> item,
	const Ui::ChatPaintRichQuote &quote,
	int articleTop);

[[nodiscard]] std::optional<Iv::BuiltRichQuote> RichQuoteForSelection(
	not_null<const HistoryMessageRichPage*> rich,
	not_null<const HistoryItem*> item,
	const Iv::Markdown::MarkdownArticleSelection &selection);

} // namespace HistoryView
