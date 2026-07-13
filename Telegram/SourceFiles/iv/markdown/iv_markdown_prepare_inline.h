/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_prepare_state.h"

namespace Iv::Markdown {

void PrepareInlineRichText(
	const MarkdownNode &node,
	int textSize,
	int renderWidthCap,
	int renderHeightCap,
	QString *blockAnchorId,
	TextWithEntities *text,
	std::vector<PreparedLink> *links,
	PrepareState *state);

} // namespace Iv::Markdown
