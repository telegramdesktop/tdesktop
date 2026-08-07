/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_prepare.h"
#include "ui/text/text_entity.h"

#include <optional>
#include <vector>

namespace Iv::Editor {

struct RichTextEditorOffsetReplacement {
	int richOffset = 0;
	int richLength = 0;
	int editorLength = 0;
};

struct RichTextEditorConversion {
	TextWithTags text;
	std::vector<RichTextEditorOffsetReplacement> replacements;
};

[[nodiscard]] RichTextEditorConversion ConvertRichTextToEditorTags(
	TextWithEntities text);
[[nodiscard]] TextWithEntities FormulaSourceToRichText(QString source);
[[nodiscard]] int MapRichTextOffsetToEditorOffset(
	const std::vector<RichTextEditorOffsetReplacement> &replacements,
	int offset);
[[nodiscard]] TextWithEntities ConvertEditorTagsToRichText(TextWithTags text);
[[nodiscard]] auto ButtonDataFromEntity(const EntityInText &entity)
-> std::optional<Markdown::InlineTextObjectButtonData>;
[[nodiscard]] auto ButtonDataFromEntity(QStringView data)
-> std::optional<Markdown::InlineTextObjectButtonData>;

} // namespace Iv::Editor
