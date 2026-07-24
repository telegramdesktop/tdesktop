/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_article_layout_blocks.h"

namespace Iv::Markdown {

[[nodiscard]] int LayoutBlocks(
	const std::vector<PreparedBlock> &prepared,
	std::vector<PreparedFormulaSlot> *formulas,
	std::vector<RenderedFormula> *renderedFormulas,
	MathRenderer *renderer,
	InlineFormulaObjectCache *inlineFormulaObjects,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	std::vector<LaidOutBlock> *blocks,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context);
[[nodiscard]] std::optional<int> RecountLaidOutBlocks(
	const std::vector<PreparedBlock> &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	std::vector<LaidOutBlock> *blocks,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context);
[[nodiscard]] int ArticleContentMaxRight(
	const std::vector<LaidOutBlock> &blocks,
	const style::Markdown &st,
	bool rtl);

} // namespace Iv::Markdown
