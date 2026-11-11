/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_prepare.h"
#include "iv/iv_rich_page.h"

namespace Iv::Editor {

void EraseTableRowsKeepingSpans(
	RichPage::Block *table,
	const Markdown::MarkdownPrepareTableRenderLimits &limits,
	int from,
	int till);

} // namespace Iv::Editor
