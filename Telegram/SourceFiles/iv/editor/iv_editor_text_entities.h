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

struct TextRange {
	int offset = 0;
	int length = 0;
};

inline constexpr auto kMaxRichTextNodeLength = 16000;
inline constexpr auto kMaxCommittedFieldLength = 256 * 1024;

[[nodiscard]] bool RangeInsideText(
	const QString &text,
	int offset,
	int length);
[[nodiscard]] bool TagContains(QStringView tags, QStringView tagId);
[[nodiscard]] bool HasFullTextTag(
	const TextWithTags &textWithTags,
	const QString &tag);
[[nodiscard]] std::vector<TextWithEntities> SplitFieldText(
	TextWithEntities text);
[[nodiscard]] bool SplitTextSpan(
	const TextWithEntities &text,
	int from,
	int till,
	TextWithEntities *before,
	TextWithEntities *selected,
	TextWithEntities *after);

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
