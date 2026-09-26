/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_parse.h"
#include "iv/markdown/iv_markdown_parse_finalize.h"
#include "iv/markdown/iv_markdown_parse_validate.h"

#include <utility>

namespace Iv::Markdown {

const MarkdownParseLimits &ParseLimitsForIv() {
	static const auto result = MarkdownParseLimits{
		.maxSourceBytes = 4 * 1024 * 1024,
		.maxCmarkNodes = 100000,
		.maxNesting = 128,
		.maxFormulaBytes = 64 * 1024,
		.maxFormulaCount = 10000,
	};
	return result;
}

ParseResult ParseMarkdownForIv(const QByteArray &source, ParseOptions options) {
	auto validated = ValidateMarkdownSourceForIv(source, std::move(options));
	return validated.ok
		? ParseMarkdownForIv(std::move(validated.source))
		: Failure(
			std::move(validated.source.sourceName),
			std::move(validated.error));
}

} // namespace Iv::Markdown
