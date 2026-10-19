/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_article_selection.h"

namespace Iv::Markdown {

[[nodiscard]] MarkdownArticleHitTestResult HitScrolledCaptionBand(
	const std::vector<SelectableSegment> &segments,
	SegmentSpan span,
	QPoint point,
	Fn<const LaidOutBlock*(int segmentIndex)> scrollOwner,
	Fn<MarkdownArticleHitTestResult(const SelectableSegment &)> hitText);

} // namespace Iv::Markdown
