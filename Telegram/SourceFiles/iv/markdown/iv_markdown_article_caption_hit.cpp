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

// EmbedPost labels paint under the header's mediaRect clip.
[[nodiscard]] QRect ScrolledBandTextRect(const SelectableSegment &segment) {
	const auto block = segment.block;
	if (!block || (block->horizontalScrollAncestorShift >= 0)) {
		return QRect();
	}
	const auto embed = (block->kind == PreparedBlockKind::EmbedPost);
	if (segment.leaf == &block->leaf) {
		return ((segment.parentSegmentIndex >= 0) || embed)
			? block->textRect
			: QRect();
	} else if (embed && (segment.leaf == &block->labelLeaf)) {
		return block->labelRect.intersected(block->mediaRect);
	} else if (embed && (segment.leaf == &block->subtitleLeaf)) {
		return block->subtitleRect.intersected(block->mediaRect);
	}
	return QRect();
}

// Details paint captions under bodyRect, wider than the scroll viewport.
[[nodiscard]] std::optional<QRect> ScrolledCaptionBandRect(
		const SelectableSegment &segment,
		QRect text,
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
	return text.intersected(*clip);
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
		const auto text = ScrolledBandTextRect(segment);
		if (!text.contains(point)) {
			continue;
		}
		const auto painted = ScrolledCaptionBandRect(
			segment,
			text,
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
