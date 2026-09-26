/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_document.h"

#include <QtCore/QByteArray>

#include <vector>

namespace Iv::Markdown {

struct MathScanBlock {
	SourceRange range;
	QString nodeKind;
};

[[nodiscard]] std::vector<int> BuildLineStarts(const QByteArray &source);
[[nodiscard]] SourceRange RangeFromLineColumns(
	const std::vector<int> &lineStarts,
	int sourceSize,
	int startLine,
	int startColumn,
	int endLine,
	int endColumn);
[[nodiscard]] bool ExtractMathRegions(
	const QByteArray &source,
	const std::vector<bool> &mask,
	const std::vector<int> &lineStarts,
	const std::vector<MathScanBlock> &blocks,
	int maxFormulaBytes,
	int maxFormulaCount,
	std::vector<MathFormula> *out,
	QString *error);

} // namespace Iv::Markdown
