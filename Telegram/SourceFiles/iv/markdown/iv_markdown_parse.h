/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_document.h"

#include <QtCore/QByteArray>

namespace Iv::Markdown {

struct MarkdownParseLimits {
	int maxSourceBytes = 0;
	int maxCmarkNodes = 0;
	int maxNesting = 0;
	int maxFormulaBytes = 0;
	int maxFormulaCount = 0;
};

struct ValidatedMarkdownSource {
	QByteArray normalized;
	QString decoded;
	std::vector<int> lineStarts;
	QString sourceName;
};

struct MarkdownSourceValidationResult {
	ValidatedMarkdownSource source;
	QString error;
	bool ok = true;
};

[[nodiscard]] const MarkdownParseLimits &ParseLimitsForIv();
[[nodiscard]] MarkdownSourceValidationResult ValidateMarkdownSourceForIv(
	const QByteArray &source,
	ParseOptions options = {});
[[nodiscard]] ParseResult ParseMarkdownForIv(ValidatedMarkdownSource source);
[[nodiscard]] ParseResult ParseMarkdownForIv(
	const QByteArray &source,
	ParseOptions options = {});

} // namespace Iv::Markdown
