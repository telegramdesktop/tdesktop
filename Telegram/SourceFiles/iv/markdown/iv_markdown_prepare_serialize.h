/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_prepare.h"

namespace style {
struct Markdown;
} // namespace style

namespace Iv::Markdown {

[[nodiscard]] QString SerializeInlineTextObjectEntity(
	const InlineTextObjectEntity &object);
[[nodiscard]] std::optional<InlineTextObjectEntity> ParseInlineTextObjectEntity(
	QStringView data);
void ExpandInlineTextObjects(TextWithEntities *text, bool withIcons);
[[nodiscard]] TextWithEntities NormalizeRichButtonLabel(
	TextWithEntities text);

enum class RichButtonLabelDates : uchar {
	Keep,
	Resolve,
};

struct InlineLinkButtonSpan {
	int offset = 0;
	int length = 0;
	QString data;
};

[[nodiscard]] TextWithEntities ResolveRichButtonLabelDates(
	TextWithEntities label,
	const Ui::Text::FormattedDateFactory &dates);
[[nodiscard]] bool TextHasInlineLinkButton(const TextWithEntities &text);
std::vector<InlineLinkButtonSpan> ExpandInlineLinkButtons(
	TextWithEntities *text,
	RichButtonLabelDates dates,
	const Ui::Text::FormattedDateFactory &factory);

[[nodiscard]] QString InlineFormulaCopySource(const QString &source);
[[nodiscard]] MarkdownPrepareDimensions CaptureMarkdownPrepareDimensions();
[[nodiscard]] MarkdownPrepareDimensions CaptureMarkdownPrepareDimensions(
	const style::Markdown &st);

} // namespace Iv::Markdown
