/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_caption_hit.h"

#include "iv/markdown/iv_markdown_article_paint.h"

namespace Iv::Markdown {
namespace {

[[nodiscard]] bool MayHitScrolledCaptionBand(
		const SelectableSegment &segment,
		QPoint point) {
	const auto block = segment.block;
	return block
		&& (segment.leaf == &block->leaf)
		&& ((segment.parentSegmentIndex >= 0)
			|| (block->kind == PreparedBlockKind::EmbedPost))
		&& (block->horizontalScrollAncestorShift < 0)
		&& block->textRect.contains(point);
}

// Details paint captions under bodyRect, wider than the scroll viewport.
[[nodiscard]] std::optional<QRect> ScrolledCaptionBandRect(
		const SelectableSegment &segment,
		QPoint point,
		const LaidOutBlock *owner) {
	if (!owner
		|| (owner == segment.block)
		|| (owner->kind != PreparedBlockKind::Details)
		|| (owner->horizontalScrollLeft <= 0)
		|| owner->scrollViewportRect.contains(point)) {
		return std::nullopt;
	}
	const auto clip = DescendantPaintClip(
		*owner,
		segment.block,
		owner->outer);
	if (!clip
		|| !clip->contains(point)
		|| !segment.block->outer.intersects(*clip)) {
		return std::nullopt;
	}
	return segment.block->textRect.intersected(*clip);
}

} // namespace

MarkdownArticleHitTestResult HitScrolledCaptionBand(
		const std::vector<SelectableSegment> &segments,
		SegmentSpan span,
		QPoint point,
		Fn<const LaidOutBlock*(int segmentIndex)> scrollOwner,
		Fn<MarkdownArticleHitTestResult(const SelectableSegment &)> hitText) {
	for (auto i = span.from; i != span.till; ++i) {
		const auto &segment = segments[i];
		if (!MayHitScrolledCaptionBand(segment, point)) {
			continue;
		}
		const auto painted = ScrolledCaptionBandRect(
			segment,
			point,
			scrollOwner(segment.index));
		if (!painted) {
			continue;
		}
		auto band = segment;
		band.outerRect = band.textRect = *painted;
		const auto result = hitText(band);
		if (result.valid() && result.state.uponSymbol) {
			return result;
		}
	}
	return {};
}

} // namespace Iv::Markdown
