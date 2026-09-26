/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_article_style.h"

#include "styles/style_chat.h"

#include <QtGui/QTextBlock>
#include <QtGui/QTextDocument>
#include <QtGui/QTextLayout>

#include <cmath>

namespace Iv::Editor {
namespace {

void EnableQTextEditLineMetrics(style::TextStyle &style) {
	style.qtextEditLineMetrics = true;
}

void EnableQTextEditLineMetrics(style::Markdown &style) {
	EnableQTextEditLineMetrics(style.body);
	EnableQTextEditLineMetrics(style.heading1);
	EnableQTextEditLineMetrics(style.heading2);
	EnableQTextEditLineMetrics(style.heading3);
	EnableQTextEditLineMetrics(style.heading4);
	EnableQTextEditLineMetrics(style.heading5);
	EnableQTextEditLineMetrics(style.heading6);
	EnableQTextEditLineMetrics(style.footer);
	EnableQTextEditLineMetrics(style.quoteAuthorStyle);
	EnableQTextEditLineMetrics(style.code);
	EnableQTextEditLineMetrics(style.displayMath.fallbackStyle);
	EnableQTextEditLineMetrics(style.table.headerStyle);
	EnableQTextEditLineMetrics(style.table.bodyStyle);
	EnableQTextEditLineMetrics(style.details.summaryStyle);
	EnableQTextEditLineMetrics(style.embedPost.authorStyle);
	EnableQTextEditLineMetrics(style.embedPost.dateStyle);
	EnableQTextEditLineMetrics(style.placeholder.labelStyle);
	EnableQTextEditLineMetrics(style.audio.titleStyle);
	EnableQTextEditLineMetrics(style.audio.subtitleStyle);
	EnableQTextEditLineMetrics(style.channel.titleStyle);
	EnableQTextEditLineMetrics(style.channel.subtitleStyle);
	EnableQTextEditLineMetrics(style.channel.button.textStyle);
	EnableQTextEditLineMetrics(style.relatedArticle.titleStyle);
	EnableQTextEditLineMetrics(style.relatedArticle.subtitleStyle);
	EnableQTextEditLineMetrics(style.relatedArticle.footerStyle);
}

} // namespace

style::Markdown CreateEditorMarkdownStyle() {
	auto result = st::messageMarkdown;
	EnableQTextEditLineMetrics(result);
	return result;
}

const style::margins &EditorBodyPadding() {
	return st::ivEditorBodyPadding;
}

int MaxVisualLineWidth(not_null<const QTextDocument*> document) {
	auto result = 0.;
	for (auto block = document->begin(); block.isValid(); block = block.next()) {
		const auto layout = block.layout();
		if (!layout) {
			continue;
		}
		for (auto i = 0, count = layout->lineCount(); i != count; ++i) {
			result = std::max(
				result,
				double(layout->lineAt(i).naturalTextWidth()));
		}
	}
	return std::max(int(std::ceil(result)), 0);
}

int MaxVisualLineWidthForWidth(
		not_null<const QTextDocument*> document,
		int width) {
	width = std::max(width, 1);
	const auto clone = std::unique_ptr<QTextDocument>(document->clone());
	clone->setTextWidth(width);
	clone->adjustSize();
	return MaxVisualLineWidth(clone.get());
}

} // namespace Iv::Editor
