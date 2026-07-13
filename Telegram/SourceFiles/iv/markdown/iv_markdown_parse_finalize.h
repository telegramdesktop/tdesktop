/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_parse_convert.h"

namespace Iv::Markdown {

void NormalizeDisplayMathBlocks(
	PreparedDocument *document,
	const QByteArray &source,
	const std::vector<int> &lineStarts);
void FinalizeDocumentSemantics(PreparedDocument *document);

} // namespace Iv::Markdown
