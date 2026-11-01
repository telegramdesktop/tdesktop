/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_quote.h"

#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "iv/markdown/iv_markdown_article_paint.h"
#include "iv/markdown/iv_markdown_prepare_native_blocks.h"

#include <QtGui/QPaintEngine>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <limits>

namespace Iv::Markdown {
namespace {

constexpr auto kMaxDisplayPosition = int(std::numeric_limits<uint16>::max());

struct QuoteInterval {
	int64 from = 0;
	int64 till = 0;
};

struct QuoteSourceRange {
	int64 from = 0;
	int64 till = 0;
	const PreparedTextReplacement *owner = nullptr;
};

struct QuoteObject {
	int offset = 0;
	int length = 0;
};

struct QuoteHighlightTarget {
	QPainterPath *path = nullptr;
	QRect clip;
	QRect to;
	float64 progress = 0.;
};

[[nodiscard]] bool ClampIntervals(
		std::vector<QuoteInterval> *intervals,
		int64 length) {
	for (auto &[from, till] : *intervals) {
		from = std::clamp(from, int64(), length);
		till = std::clamp(till, from, length);
	}
	intervals->erase(
		ranges::remove_if(*intervals, [](const QuoteInterval &interval) {
			return (interval.from >= interval.till);
		}),
		end(*intervals));
	return !intervals->empty();
}

[[nodiscard]] bool ReplayReplacements(
		QuoteInterval *interval,
		std::span<const PreparedTextReplacement> replacements,
		int64 previous) {
	auto &[from, till] = *interval;
	for (const auto &replacement : replacements) {
		const auto position = int64(replacement.position);
		const auto skipped = int64(replacement.skipped);
		const auto added = int64(replacement.added);
		if (position < previous || skipped < 0 || added < 0) {
			return false;
		} else if (position >= till) {
			break;
		}
		previous = position;
		till += added - std::min(skipped, till + added - position);
		if (position < from) {
			from += added - std::min(skipped, from + added - position);
		} else if (position == from && !skipped) {
			from += added;
		}
	}
	return true;
}

[[nodiscard]] bool ApplyReplacements(
		std::vector<QuoteInterval> *intervals,
		const std::vector<PreparedTextReplacement> &replacements) {
	auto next = begin(replacements);
	auto previous = int64();
	auto shift = int64();
	for (auto i = begin(*intervals); i != end(*intervals); ++i) {
		for (; next != end(replacements); ++next) {
			const auto position = int64(next->position);
			const auto skipped = int64(next->skipped);
			const auto added = int64(next->added);
			const auto from = i->from + shift;
			if (position < previous
				|| skipped < 0
				|| added < 0
				|| position >= from
				|| from + added - skipped < position) {
				break;
			}
			previous = position;
			shift += added - skipped;
		}
		i->from += shift;
		i->till += shift;
		if (!ReplayReplacements(&*i, { next, end(replacements) }, previous)) {
			intervals->erase(i, end(*intervals));
			break;
		}
	}
	return !intervals->empty();
}

[[nodiscard]] const PreparedLinkSplices *FindLinkSplices(
		const std::vector<PreparedLinkSplices> &splices,
		const TextWithEntities *page) {
	const auto i = ranges::find(splices, page, &PreparedLinkSplices::source);
	return (i != end(splices)) ? &*i : nullptr;
}

[[nodiscard]] std::optional<int64> ReplacedSourceSize(
		std::span<const PreparedTextReplacement> replacements,
		int64 size) {
	auto previous = int64();
	auto result = size;
	for (const auto &[position, skipped, added] : replacements) {
		const auto till = int64(position) + added;
		if (position < previous || skipped < 0 || added < 0 || till > size) {
			return std::nullopt;
		}
		previous = till;
		result += int64(skipped) - added;
	}
	return result;
}

[[nodiscard]] QuoteSourceRange UndoReplacements(
		std::span<const PreparedTextReplacement> replacements,
		int64 position,
		MarkdownArticleTextEdge edge) {
	const auto start = (edge == MarkdownArticleTextEdge::Start);
	auto shift = int64();
	for (const auto &replacement : replacements) {
		const auto from = int64(replacement.position);
		const auto till = from + replacement.added;
		if (start ? (position < from) : (position <= from)) {
			break;
		} else if (start ? (position < till) : (position <= till)) {
			const auto source = from - shift;
			return {
				.from = source,
				.till = source + replacement.skipped,
				.owner = &replacement,
			};
		}
		shift += int64(replacement.added) - replacement.skipped;
	}
	return { .from = position - shift, .till = position - shift };
}

[[nodiscard]] int64 EdgeOf(
		const QuoteSourceRange &range,
		MarkdownArticleTextEdge edge) {
	return (edge == MarkdownArticleTextEdge::Start) ? range.from : range.till;
}

[[nodiscard]] int64 VisibleCodeSize(const QString &page) {
	const auto size = int64(page.size());
	return page.endsWith(u"\r\n"_q)
		? (size - 2)
		: (page.endsWith(QChar(u'\n')) || page.endsWith(QChar(u'\r')))
		? (size - 1)
		: size;
}

[[nodiscard]] int64 CodeSourcePosition(
		const std::vector<int> &offsets,
		int64 position,
		MarkdownArticleTextEdge edge) {
	const auto after = std::upper_bound(
		begin(offsets),
		end(offsets),
		position);
	const auto index = int64(after - begin(offsets)) - 1;
	return ((after == end(offsets))
		|| (*(after - 1) == position)
		|| (edge == MarkdownArticleTextEdge::Start))
		? index
		: (index + 1);
}

[[nodiscard]] std::optional<int64> LabelSourcePosition(
		const PreparedLinkSplices &splices,
		const PreparedTextReplacement &label,
		int64 position,
		MarkdownArticleTextEdge edge) {
	const auto &dates = splices.labelDates;
	const auto from = int64(label.position);
	const auto till = from + label.added;
	const auto before = [](const PreparedTextReplacement &date, int64 value) {
		return (date.position < value);
	};
	const auto first = std::lower_bound(
		begin(dates),
		end(dates),
		from,
		before);
	const auto last = std::lower_bound(first, end(dates), till, before);
	const auto previous = (&label != splices.replacements.data())
		? (&label - 1)
		: nullptr;
	const auto adjacent = previous
		&& (int64(previous->position) + previous->added == from);
	if ((label.skipped <= 0)
		|| ((first != begin(dates))
			&& (int64((first - 1)->position) + (first - 1)->added > from))
		|| (adjacent
			&& (first != last)
			&& (first->position == from)
			&& !first->added)
		|| !ReplacedSourceSize({ first, last }, till)) {
		return std::nullopt;
	}
	const auto range = UndoReplacements({ first, last }, position, edge);
	return EdgeOf(range, edge) - from;
}

[[nodiscard]] std::optional<MarkdownArticleLeafPoint> PagePointForPrepared(
		const PreparedEditLeafSource &source,
		const PreparedLinkSplices *splices,
		int64 position,
		MarkdownArticleTextEdge edge,
		int64 prepared,
		int64 page) {
	const auto none = std::span<const PreparedTextReplacement>();
	const auto replacements = splices
		? std::span(splices->replacements)
		: none;
	if ((ReplacedSourceSize(replacements, prepared) != page)
		|| (splices && !ReplacedSourceSize(splices->labelDates, prepared))
		|| (page > std::numeric_limits<int>::max())) {
		return std::nullopt;
	}
	const auto range = UndoReplacements(replacements, position, edge);
	if (!range.owner) {
		return MarkdownArticleLeafPoint{
			.leaf = source,
			.position = int(range.from),
		};
	}
	const auto label = LabelSourcePosition(
		*splices,
		*range.owner,
		position,
		edge);
	if (!label || (*label > std::numeric_limits<int>::max())) {
		return std::nullopt;
	}
	return MarkdownArticleLeafPoint{
		.leaf = source,
		.position = int(range.from),
		.labelOffset = int(*label),
	};
}

[[nodiscard]] std::vector<QuoteObject> DisplayObjects(
		const TextWithEntities &text,
		const std::vector<int> *codeOffsets) {
	const auto length = int(text.text.size());
	const auto display = [&](int position) {
		return codeOffsets ? (*codeOffsets)[position] : position;
	};
	auto result = std::vector<QuoteObject>();
	for (const auto &entity : text.entities) {
		const auto from = entity.offset();
		if (entity.type() != EntityType::CustomEmoji
			|| from < 0
			|| from >= length
			|| entity.length() <= 0) {
			continue;
		}
		const auto till = from + std::min(entity.length(), length - from);
		result.push_back({
			.offset = display(from),
			.length = display(till) - display(from),
		});
	}
	ranges::sort(result, ranges::less(), &QuoteObject::offset);
	return result;
}

// WHY: modifications() also records an inline object's copy text over
// the source character it keeps on screen; only that export part of a
// record is subtracted, so a real trim, date or insert there survives.
[[nodiscard]] bool CollectRealModifications(
		const Ui::Text::String &leaf,
		const std::vector<QuoteObject> &objects,
		std::vector<PreparedTextReplacement> *result) {
	auto shift = int64();
	auto next = begin(objects);
	for (const auto &modification : leaf.modifications()) {
		auto skipped = int(modification.skipped);
		auto added = int(modification.added);
		const auto source = modification.position - shift;
		while (next != end(objects) && next->offset < source) {
			++next;
		}
		if (next != end(objects)) {
			const auto before = next->offset - source;
			if ((added == next->length) && (skipped - before > added)) {
				skipped = int(before);
				added = 0;
				++next;
			} else if (skipped > before) {
				return false;
			}
		}
		if (skipped || added) {
			result->push_back({
				.position = modification.position,
				.skipped = skipped,
				.added = added,
			});
		}
		shift += added - skipped;
	}
	return true;
}

[[nodiscard]] QuoteHighlightTarget PaintHighlightTarget(
		const MarkdownArticlePaintContext &context) {
	const auto active = context.quoteRanges
		&& (context.highlight.collapsion > 0.);
	return {
		.path = active ? context.highlightPathCache : nullptr,
		.clip = context.clip,
		.to = context.highlightInterpolateTo,
		.progress = (1. - context.highlight.collapsion),
	};
}

[[nodiscard]] std::span<const MarkdownArticleSearchMatch> SegmentRanges(
		const std::vector<MarkdownArticleSearchMatch> &matches,
		int segment) {
	const auto found = ranges::equal_range(
		matches,
		segment,
		ranges::less(),
		&MarkdownArticleSearchMatch::segment);
	return { found.begin(), found.end() };
}

[[nodiscard]] bool HasQuotedRange(
		std::span<const MarkdownArticleSearchMatch> matches) {
	return ranges::any_of(matches, [](const MarkdownArticleSearchMatch &match) {
		return (match.from < match.to);
	});
}

[[nodiscard]] TextSelection LeafSelection(
		const SelectableSegment &segment,
		const MarkdownArticleSearchMatch &range) {
	const auto length = std::min(SegmentLength(segment), kMaxDisplayPosition);
	const auto from = std::clamp(range.from, 0, length);
	const auto to = std::clamp(range.to, from, length);
	return { uint16(from), uint16(to) };
}

[[nodiscard]] std::vector<TextSelection> LeafSelections(
		const SelectableSegment &segment,
		std::span<const MarkdownArticleSearchMatch> matches) {
	auto result = std::vector<TextSelection>();
	result.reserve(matches.size());
	for (const auto &match : matches) {
		result.push_back(LeafSelection(segment, match));
	}
	return result;
}

[[nodiscard]] QRect VisibleSegmentRect(
		const SelectableSegment &segment,
		QRect outer,
		QRect logical,
		const LaidOutBlock *owner,
		const style::Markdown &st) {
	if (!owner) {
		return outer;
	} else if (segment.block != owner) {
		const auto clip = DescendantPaintClip(
			*owner,
			segment.block,
			owner->outer);
		const auto caption = (segment.parentSegmentIndex >= 0);
		const auto painted = caption ? logical : outer;
		return clip ? painted.intersected(*clip) : QRect();
	} else if (segment.cell) {
		return outer.intersected(HorizontalOverflowContentClip(
			*owner,
			st,
			owner->visibleTableRect));
	} else if ((owner->kind == PreparedBlockKind::Table)
		|| (owner->kind == PreparedBlockKind::Details)) {
		return outer;
	}
	return outer.intersected(owner->scrollViewportRect);
}

void AppendHighlightRect(
		const QuoteHighlightTarget &target,
		const QRectF &rect) {
	const auto visible = rect.intersected(QRectF(target.clip));
	if (visible.isEmpty()) {
		return;
	} else if (target.progress >= 1.) {
		target.path->addRect(target.to);
	} else if (target.progress <= 0.) {
		target.path->addRect(visible);
	} else {
		const auto lerp = [&](float64 from, float64 to) {
			return from + (to - from) * target.progress;
		};
		target.path->addRect(
			lerp(visible.x(), target.to.x()),
			lerp(visible.y(), target.to.y()),
			lerp(visible.width(), target.to.width()),
			lerp(visible.height(), target.to.height()));
	}
}

} // namespace

void AppendDisplayRangesForLeafText(
		const QuoteLeafLookup &leaf,
		const PreparedEditLeafSource &source,
		std::span<const MarkdownArticleTextRange> ranges,
		not_null<std::vector<MarkdownArticleSearchMatch>*> result) {
	auto intervals = std::vector<QuoteInterval>();
	intervals.reserve(ranges.size());
	for (const auto &[from, till] : ranges) {
		if (from < till) {
			Assert(intervals.empty() || intervals.back().till <= from);
			intervals.push_back({ from, till });
		}
	}
	const auto segment = leaf.segment;
	if (!leaf.content
		|| !IsQuotableNativeContent(*leaf.content)
		|| !segment
		|| intervals.empty()) {
		return;
	}
	const auto &page = *leaf.content->richPage;
	if (segment->kind == SelectableSegmentKind::DisplayMath) {
		const auto formula = (source.kind == PreparedEditLeafKind::MathFormula)
			? ResolveCanonicalNativeIvLeafText(page, source)
			: nullptr;
		if (formula && ClampIntervals(&intervals, formula->size())) {
			result->push_back({
				.segment = segment->index,
				.from = 0,
				.to = 1,
			});
		}
		return;
	}
	const auto text = ResolveCanonicalNativeIvLeafRichText(page, source);
	const auto prepared = leaf.cell
		? &leaf.cell->text
		: leaf.block
		? &leaf.block->text
		: nullptr;
	if (!segment->isTextLeaf()
		|| !text
		|| !prepared
		|| !ClampIntervals(&intervals, text->text.size())) {
		return;
	}
	const auto splices = FindLinkSplices(leaf.content->linkSplices, text);
	if ((splices && !ApplyReplacements(&intervals, splices->replacements))
		|| !ClampIntervals(&intervals, prepared->text.size())) {
		return;
	}
	const auto code = (segment->kind == SelectableSegmentKind::CodeBlock);
	const auto offsets = code
		? CodeBlockDisplayOffsets(prepared->text)
		: std::vector<int>();
	if (code) {
		for (auto &[from, till] : intervals) {
			from = offsets[std::size_t(from)];
			till = offsets[std::size_t(till)];
		}
	}
	const auto objects = DisplayObjects(*prepared, code ? &offsets : nullptr);
	const auto limit = std::min(SegmentLength(*segment), kMaxDisplayPosition);
	auto modifications = std::vector<PreparedTextReplacement>();
	if (!CollectRealModifications(*segment->leaf, objects, &modifications)
		|| !ApplyReplacements(&intervals, modifications)
		|| !ClampIntervals(&intervals, limit)) {
		return;
	}
	for (const auto &[from, till] : intervals) {
		result->push_back({
			.segment = segment->index,
			.from = int(from),
			.to = int(till),
		});
	}
}

std::optional<MarkdownArticleLeafPoint> LeafPointForDisplay(
		const QuoteLeafLookup &leaf,
		const PreparedEditLeafSource &source,
		int offset,
		MarkdownArticleTextEdge edge) {
	const auto segment = leaf.segment;
	if (!leaf.content
		|| !IsQuotableNativeContent(*leaf.content)
		|| !segment
		|| (offset < 0)
		|| (offset > SegmentLength(*segment))) {
		return std::nullopt;
	}
	const auto &page = *leaf.content->richPage;
	if (segment->kind == SelectableSegmentKind::DisplayMath) {
		const auto formula = (source.kind == PreparedEditLeafKind::MathFormula)
			? ResolveCanonicalNativeIvLeafText(page, source)
			: nullptr;
		if (!formula) {
			return std::nullopt;
		}
		return MarkdownArticleLeafPoint{
			.leaf = source,
			.position = (offset > 0) ? int(formula->size()) : 0,
		};
	}
	const auto text = ResolveCanonicalNativeIvLeafRichText(page, source);
	const auto prepared = leaf.cell
		? &leaf.cell->text
		: leaf.block
		? &leaf.block->text
		: nullptr;
	if (!segment->isTextLeaf() || !text || !prepared) {
		return std::nullopt;
	}
	const auto code = (segment->kind == SelectableSegmentKind::CodeBlock);
	const auto offsets = code
		? CodeBlockDisplayOffsets(prepared->text)
		: std::vector<int>();
	const auto displaySize = code
		? int64(CodeBlockDisplayText(prepared->text).size())
		: int64(prepared->text.size());
	const auto objects = DisplayObjects(*prepared, code ? &offsets : nullptr);
	auto modifications = std::vector<PreparedTextReplacement>();
	if (!CollectRealModifications(*segment->leaf, objects, &modifications)) {
		return std::nullopt;
	}
	const auto covered = ReplacedSourceSize(
		modifications,
		segment->leaf->length());
	if (!covered || (*covered > displaySize)) {
		return std::nullopt;
	}
	const auto display = EdgeOf(
		UndoReplacements(modifications, offset, edge),
		edge);
	return PagePointForPrepared(
		source,
		FindLinkSplices(leaf.content->linkSplices, text),
		code ? CodeSourcePosition(offsets, display, edge) : display,
		edge,
		prepared->text.size(),
		code ? VisibleCodeSize(text->text) : int64(text->text.size()));
}

int GroupedCaptionSegment(
		const std::vector<SelectableSegment> &segments,
		int segmentIndex) {
	const auto segment = FindSegment(&segments, segmentIndex);
	const auto block = segment ? segment->block : nullptr;
	if (!block
		|| segment->isTextLeaf()
		|| (block->kind != PreparedBlockKind::GroupedMedia)) {
		return -1;
	}
	const auto caption = FindSegment(&segments, block->secondarySegmentIndex);
	return (caption && (caption->leaf == &block->leaf)) ? caption->index : -1;
}

void AppendQuoteTextHighlight(
		QPainter &p,
		const Ui::Text::String &leaf,
		const Ui::Text::PaintContext &leafContext,
		const MarkdownArticlePaintContext &context,
		int segmentIndex) {
	const auto target = PaintHighlightTarget(context);
	const auto segment = target.path
		? FindSegment(context.selectionState.segments, segmentIndex)
		: nullptr;
	if (!segment || (segment->leaf != &leaf)) {
		return;
	}
	const auto matches = SegmentRanges(*context.quoteRanges, segmentIndex);
	if (matches.empty()) {
		return;
	}
	const auto rects = leaf.countHighlightRects(
		LeafSelections(*segment, matches),
		{
			.position = leafContext.position,
			.geometry = leafContext.geometry,
			.align = leafContext.align,
			.clip = leafContext.clip,
			.ratio = p.paintEngine()->paintDevice()->devicePixelRatioF(),
		});
	for (const auto &rect : rects) {
		AppendHighlightRect(target, QRectF(rect));
	}
}

void AppendQuoteFormulaHighlight(
		QRect formula,
		const MarkdownArticlePaintContext &context,
		int segmentIndex) {
	const auto target = PaintHighlightTarget(context);
	if (target.path
		&& (segmentIndex >= 0)
		&& HasQuotedRange(SegmentRanges(*context.quoteRanges, segmentIndex))) {
		AppendHighlightRect(target, QRectF(formula));
	}
}

QRect DisplayRangesRect(
		const std::vector<SelectableSegment> &segments,
		const std::vector<MarkdownArticleSearchMatch> &ranges,
		const style::Markdown &st,
		float64 ratio,
		Fn<QuoteSegmentGeometry(int segmentIndex)> geometry) {
	auto result = QRect();
	const auto add = [&](QRect part) {
		if (!part.isEmpty()) {
			result = result.united(part);
		}
	};
	for (auto i = begin(ranges); i != end(ranges);) {
		const auto from = i;
		while (i != end(ranges) && i->segment == from->segment) {
			++i;
		}
		const auto matches = std::span<const MarkdownArticleSearchMatch>(
			from,
			i);
		const auto segment = FindSegment(&segments, from->segment);
		if (!segment || !HasQuotedRange(matches)) {
			continue;
		}
		const auto math = (segment->kind == SelectableSegmentKind::DisplayMath);
		const auto outer = math
			? segment->outerRect
			: segment->isTextLeaf()
			? segment->textRect
			: QRect();
		if (outer.isEmpty()) {
			continue;
		}
		const auto [logical, owner] = geometry(segment->index);
		const auto visible = VisibleSegmentRect(
			*segment,
			outer,
			logical,
			owner,
			st);
		if (math) {
			add(visible);
		} else if (!visible.isEmpty() && !logical.isEmpty()) {
			const auto parts = segment->leaf->countHighlightRects(
				LeafSelections(*segment, matches),
				{
					.position = logical.topLeft(),
					.geometry = TextGeometry(segment->textWidth),
					.align = segment->align,
					.clip = visible,
					.ratio = ratio,
				});
			for (const auto &part : parts) {
				add(part.intersected(visible));
			}
		}
	}
	return result;
}

} // namespace Iv::Markdown
