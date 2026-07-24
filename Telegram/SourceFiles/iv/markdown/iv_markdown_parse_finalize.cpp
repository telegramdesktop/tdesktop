/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_parse_finalize.h"
#include "iv/markdown/iv_markdown_parse_validate.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace Iv::Markdown {
namespace {

void OffsetToPosition(
		const std::vector<int> &lineStarts,
		int offset,
		int *line,
		int *column) {
	const auto clampedOffset = std::max(offset, 0);
	auto resultLine = 1;
	auto resultColumn = clampedOffset + 1;
	if (!lineStarts.empty()) {
		auto i = std::upper_bound(
			lineStarts.begin(),
			lineStarts.end(),
			clampedOffset);
		if (i != lineStarts.begin()) {
			--i;
			resultLine = static_cast<int>(i - lineStarts.begin()) + 1;
			resultColumn = clampedOffset - *i + 1;
		}
	}
	if (line) {
		*line = resultLine;
	}
	if (column) {
		*column = resultColumn;
	}
}

[[nodiscard]] QString SourceSlice(
		const QByteArray &source,
		const SourceRange &range) {
	if (!range.available) {
		return QString();
	}
	const auto sourceSize = static_cast<int>(source.size());
	const auto start = std::clamp(range.startOffset, 0, sourceSize);
	const auto end = std::clamp(range.endOffset, start, sourceSize);
	const auto slice = source.mid(start, end - start);
	return QString::fromUtf8(slice.constData(), slice.size());
}

[[nodiscard]] SourceRange RangeForOffsets(
		const std::vector<int> &lineStarts,
		int sourceSize,
		int startOffset,
		int endOffset) {
	auto startLine = 0;
	auto startColumn = 0;
	auto endLine = 0;
	auto endColumn = 0;
	OffsetToPosition(lineStarts, startOffset, &startLine, &startColumn);
	OffsetToPosition(
		lineStarts,
		std::max(startOffset, endOffset - 1),
		&endLine,
		&endColumn);
	return RangeFromLineColumns(
		lineStarts,
		sourceSize,
		startLine,
		startColumn,
		endLine,
		endColumn);
}

[[nodiscard]] QString PlainText(const MarkdownNode &node) {
	auto result = node.text;
	for (const auto &child : node.children) {
		result.append(PlainText(child));
	}
	return result;
}

[[nodiscard]] QString AnchorIdBaseFromText(QString text) {
	text = text.trimmed().toLower();
	auto result = QString();
	auto pendingHyphen = false;
	for (const auto ch : text) {
		if (ch.isLetterOrNumber()) {
			if (pendingHyphen && !result.isEmpty()) {
				result.append(QChar('-'));
			}
			result.append(ch);
			pendingHyphen = false;
		} else if (!result.isEmpty()) {
			pendingHyphen = true;
		}
	}
	if (result.isEmpty()) {
		return u"section"_q;
	}
	return result;
}

[[nodiscard]] QString FootnoteDefinitionAnchorId(int ordinal) {
	return (ordinal > 0) ? (u"fn-"_q + QString::number(ordinal)) : QString();
}

[[nodiscard]] MarkdownNode DisplayMathNode(
		const MathFormula &formula,
		int vectorIndex) {
	auto result = MarkdownNode();
	result.kind = NodeKind::DisplayMath;
	result.range = formula.range;
	result.text = formula.tex;
	result.formulaIndex = vectorIndex;
	return result;
}

[[nodiscard]] int RangeEndOffset(const SourceRange &range) {
	return range.available ? range.endOffset : std::numeric_limits<int>::min();
}

[[nodiscard]] int RangeStartOffset(const SourceRange &range) {
	return range.available
		? range.startOffset
		: std::numeric_limits<int>::max();
}

[[nodiscard]] bool IsBreakNode(NodeKind kind) {
	return kind == NodeKind::SoftBreak || kind == NodeKind::LineBreak;
}

void TrimBreakEdges(std::vector<MarkdownNode> *children) {
	if (!children) {
		return;
	}
	while (!children->empty() && IsBreakNode(children->front().kind)) {
		children->erase(children->begin());
	}
	while (!children->empty() && IsBreakNode(children->back().kind)) {
		children->pop_back();
	}
}

[[nodiscard]] bool RangeContains(
		const SourceRange &outer,
		const SourceRange &inner) {
	return outer.available
		&& inner.available
		&& outer.startOffset <= inner.startOffset
		&& outer.endOffset >= inner.endOffset;
}

[[nodiscard]] bool RangeOverlaps(
		const SourceRange &range,
		int clipStart,
		int clipEnd) {
	return range.available
		&& (range.endOffset > clipStart)
		&& (range.startOffset < clipEnd);
}

[[nodiscard]] bool FirstAvailableRange(
		const MarkdownNode &node,
		SourceRange *out);
[[nodiscard]] bool LastAvailableRange(
		const MarkdownNode &node,
		SourceRange *out);

[[nodiscard]] bool FirstAvailableRangeInChildren(
		const std::vector<MarkdownNode> &children,
		SourceRange *out) {
	for (const auto &child : children) {
		if (FirstAvailableRange(child, out)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool LastAvailableRangeInChildren(
		const std::vector<MarkdownNode> &children,
		SourceRange *out) {
	for (auto i = children.rbegin(); i != children.rend(); ++i) {
		if (LastAvailableRange(*i, out)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool FirstAvailableRange(
		const MarkdownNode &node,
		SourceRange *out) {
	if (node.range.available) {
		if (out) {
			*out = node.range;
		}
		return true;
	}
	return FirstAvailableRangeInChildren(node.children, out);
}

[[nodiscard]] bool LastAvailableRange(
		const MarkdownNode &node,
		SourceRange *out) {
	if (node.range.available) {
		if (out) {
			*out = node.range;
		}
		return true;
	}
	return LastAvailableRangeInChildren(node.children, out);
}

[[nodiscard]] bool RangeFromChildren(
		const std::vector<MarkdownNode> &children,
		SourceRange *out) {
	auto first = SourceRange();
	auto last = SourceRange();
	if (!FirstAvailableRangeInChildren(children, &first)
		|| !LastAvailableRangeInChildren(children, &last)) {
		return false;
	}
	auto result = first;
	result.endLine = last.endLine;
	result.endColumn = last.endColumn;
	result.endOffset = last.endOffset;
	if (out) {
		*out = result;
	}
	return true;
}

[[nodiscard]] bool ClipNodeToOffsets(
		const MarkdownNode &node,
		const QByteArray &source,
		const std::vector<int> &lineStarts,
		int clipStart,
		int clipEnd,
		MarkdownNode *out) {
	if (!out || clipEnd <= clipStart || IsBreakNode(node.kind)) {
		return false;
	}
	if (node.range.available && !RangeOverlaps(node.range, clipStart, clipEnd)) {
		return false;
	}
	if (node.children.empty()) {
		if (!node.range.available) {
			return false;
		}
		const auto clippedStart = std::max(node.range.startOffset, clipStart);
		const auto clippedEnd = std::min(node.range.endOffset, clipEnd);
		if (clippedEnd <= clippedStart) {
			return false;
		}
		*out = node;
		out->range = RangeForOffsets(
			lineStarts,
			static_cast<int>(source.size()),
			clippedStart,
			clippedEnd);
		if (clippedStart == node.range.startOffset
			&& clippedEnd == node.range.endOffset) {
			return true;
		}
		switch (node.kind) {
		case NodeKind::Text:
		case NodeKind::InlineCode:
			out->text = SourceSlice(source, out->range);
			break;
		case NodeKind::HtmlBlock:
		case NodeKind::HtmlInline:
		case NodeKind::Unsupported:
			out->raw = SourceSlice(source, out->range);
			break;
		default:
			return false;
		}
		return true;
	}
	auto result = node;
	result.children.clear();
	for (const auto &child : node.children) {
		if (IsBreakNode(child.kind)) {
			if (!result.children.empty()) {
				result.children.push_back(child);
			}
			continue;
		}
		auto clippedChild = MarkdownNode();
		if (ClipNodeToOffsets(
				child,
				source,
				lineStarts,
				clipStart,
				clipEnd,
				&clippedChild)) {
			result.children.push_back(std::move(clippedChild));
		}
	}
	TrimBreakEdges(&result.children);
	if (result.children.empty()) {
		return false;
	}
	auto clippedRange = SourceRange();
	if (RangeFromChildren(result.children, &clippedRange)) {
		result.range = clippedRange;
	}
	*out = std::move(result);
	return true;
}

[[nodiscard]] bool ParagraphSegment(
		const MarkdownNode &paragraph,
		const QByteArray &source,
		const std::vector<int> &lineStarts,
		int clipStart,
		int clipEnd,
		MarkdownNode *out) {
	if (clipEnd <= clipStart) {
		return false;
	}
	return ClipNodeToOffsets(
		paragraph,
		source,
		lineStarts,
		clipStart,
		clipEnd,
		out);
}

[[nodiscard]] std::vector<MarkdownNode> SplitParagraphAroundDisplayMath(
		const MarkdownNode &paragraph,
		const std::vector<MathFormula> &formulas,
		const std::vector<int> &displayIndexes,
		int begin,
		int end,
		const QByteArray &source,
		const std::vector<int> &lineStarts) {
	auto result = std::vector<MarkdownNode>();
	if (!paragraph.range.available) {
		result.push_back(paragraph);
		return result;
	}
	auto cursor = paragraph.range.startOffset;
	for (auto i = begin; i != end; ++i) {
		const auto formulaIndex = displayIndexes[i];
		const auto &formula = formulas[formulaIndex];
		if (!RangeContains(paragraph.range, formula.range)) {
			continue;
		}
		auto segment = MarkdownNode();
		if (ParagraphSegment(
				paragraph,
				source,
				lineStarts,
				cursor,
				formula.range.startOffset,
				&segment)) {
			result.push_back(std::move(segment));
		}
		result.push_back(DisplayMathNode(formula, formulaIndex));
		cursor = std::max(cursor, formula.range.endOffset);
	}
	auto tail = MarkdownNode();
	if (ParagraphSegment(
			paragraph,
			source,
			lineStarts,
			cursor,
			paragraph.range.endOffset,
			&tail)) {
		result.push_back(std::move(tail));
	}
	return result;
}

[[nodiscard]] std::vector<int> DisplayFormulaIndexes(
		const std::vector<MathFormula> &formulas) {
	auto result = std::vector<int>();
	const auto count = static_cast<int>(formulas.size());
	for (auto i = 0; i != count; ++i) {
		if (formulas[i].kind == MathKind::Display) {
			result.push_back(i);
		}
	}
	std::sort(
		result.begin(),
		result.end(),
		[&](int left, int right) {
			const auto leftOffset = RangeStartOffset(formulas[left].range);
			const auto rightOffset = RangeStartOffset(formulas[right].range);
			return (leftOffset != rightOffset)
				? (leftOffset < rightOffset)
				: (left < right);
		});
	return result;
}

[[nodiscard]] bool CanNormalizeDisplayMathChildren(NodeKind kind) {
	switch (kind) {
	case NodeKind::Document:
	case NodeKind::List:
	case NodeKind::ListItem:
	case NodeKind::Blockquote:
	case NodeKind::Table:
	case NodeKind::TableRow:
		return true;
	default:
		return false;
	}
}

void NormalizeDisplayMathChildren(
		MarkdownNode *node,
		const std::vector<MathFormula> &formulas,
		const std::vector<int> &displayIndexes,
		int begin,
		int end,
		const QByteArray &source,
		const std::vector<int> &lineStarts) {
	if (!node || begin >= end || node->children.empty()) {
		return;
	}
	auto originalChildren = std::move(node->children);
	auto children = std::vector<MarkdownNode>();
	children.reserve(originalChildren.size() + (end - begin));
	auto formula = begin;
	for (auto &child : originalChildren) {
		while (formula != end
			&& child.range.available
			&& RangeEndOffset(formulas[displayIndexes[formula]].range)
				<= child.range.startOffset) {
			const auto formulaIndex = displayIndexes[formula];
			children.push_back(DisplayMathNode(formulas[formulaIndex], formulaIndex));
			++formula;
		}
		auto childEnd = formula;
		while (childEnd != end) {
			const auto &formulaRange = formulas[displayIndexes[childEnd]].range;
			if (!RangeContains(child.range, formulaRange)) {
				break;
			}
			++childEnd;
		}
		if (formula != childEnd && child.kind == NodeKind::Paragraph) {
			auto split = SplitParagraphAroundDisplayMath(
				child,
				formulas,
				displayIndexes,
				formula,
				childEnd,
				source,
				lineStarts);
			for (auto &part : split) {
				children.push_back(std::move(part));
			}
			formula = childEnd;
			continue;
		}
		if (formula != childEnd && child.kind == NodeKind::TableCell) {
			children.push_back(std::move(child));
			formula = childEnd;
			continue;
		}
		if (formula != childEnd && CanNormalizeDisplayMathChildren(child.kind)) {
			NormalizeDisplayMathChildren(
				&child,
				formulas,
				displayIndexes,
				formula,
				childEnd,
				source,
				lineStarts);
			formula = childEnd;
		}
		children.push_back(std::move(child));
	}
	while (formula != end) {
		const auto formulaIndex = displayIndexes[formula];
		children.push_back(DisplayMathNode(formulas[formulaIndex], formulaIndex));
		++formula;
	}
	node->children = std::move(children);
}

[[nodiscard]] int CountNodes(const MarkdownNode &node) {
	auto result = 1;
	for (const auto &child : node.children) {
		result += CountNodes(child);
	}
	return result;
}

[[nodiscard]] int *FindNamedCounter(
		std::vector<std::pair<QString, int>> *entries,
		const QString &key) {
	if (!entries) {
		return nullptr;
	}
	for (auto &entry : *entries) {
		if (entry.first == key) {
			return &entry.second;
		}
	}
	return nullptr;
}

[[nodiscard]] int FindNamedValue(
		const std::vector<std::pair<QString, int>> &entries,
		const QString &key) {
	for (const auto &entry : entries) {
		if (entry.first == key) {
			return entry.second;
		}
	}
	return 0;
}

[[nodiscard]] bool ContainsAnchorId(
		const std::vector<QString> &anchors,
		const QString &value) {
	return std::find(anchors.begin(), anchors.end(), value) != anchors.end();
}

void AssignHeadingAnchors(
		MarkdownNode *node,
		std::vector<std::pair<QString, int>> *counts,
		QStringList *warnings) {
	if (!node) {
		return;
	}
	if (node->kind == NodeKind::Heading) {
		const auto base = AnchorIdBaseFromText(PlainText(*node));
		auto count = FindNamedCounter(counts, base);
		if (count) {
			++(*count);
			node->anchorId = base + u"-"_q + QString::number(*count);
			if (warnings) {
				warnings->push_back(
					u"Duplicate heading anchor \"%1\" remapped to \"%2\""_q.arg(
						base
					).arg(
						node->anchorId));
			}
		} else {
			counts->push_back({ base, 1 });
			node->anchorId = base;
		}
	}
	for (auto &child : node->children) {
		AssignHeadingAnchors(&child, counts, warnings);
	}
}

void AssignFootnoteDefinitionOrdinals(
		MarkdownNode *node,
		std::vector<std::pair<QString, int>> *definitions,
		int *nextOrdinal,
		QStringList *warnings) {
	if (!node || !definitions || !nextOrdinal) {
		return;
	}
	if (node->kind == NodeKind::FootnoteDefinition) {
		if (node->footnoteLabel.isEmpty()) {
			if (warnings) {
				warnings->push_back(
					u"Footnote definition without label at %1:%2"_q.arg(
						node->range.startLine
					).arg(
						node->range.startColumn));
			}
		} else if (const auto existing = FindNamedValue(
				*definitions,
				node->footnoteLabel)) {
			node->footnoteOrdinal = existing;
			node->anchorId = FootnoteDefinitionAnchorId(existing);
			if (warnings) {
				warnings->push_back(
					u"Duplicate footnote definition \"%1\""_q.arg(
						node->footnoteLabel));
			}
		} else {
			node->footnoteOrdinal = *nextOrdinal;
			node->anchorId = FootnoteDefinitionAnchorId(*nextOrdinal);
			definitions->push_back({ node->footnoteLabel, *nextOrdinal });
			++(*nextOrdinal);
		}
	}
	for (auto &child : node->children) {
		AssignFootnoteDefinitionOrdinals(&child, definitions, nextOrdinal, warnings);
	}
}

void AssignFootnoteReferenceOrdinals(
		MarkdownNode *node,
		const std::vector<std::pair<QString, int>> &definitions,
		QStringList *warnings) {
	if (!node) {
		return;
	}
	if (node->kind == NodeKind::FootnoteReference) {
		if (node->footnoteLabel.isEmpty()) {
			if (warnings) {
				warnings->push_back(
					u"Footnote reference without label at %1:%2"_q.arg(
						node->range.startLine
					).arg(
						node->range.startColumn));
			}
		} else if (const auto ordinal = FindNamedValue(
				definitions,
				node->footnoteLabel)) {
			node->footnoteOrdinal = ordinal;
		} else if (warnings) {
			warnings->push_back(
				u"Unresolved footnote reference \"%1\""_q.arg(
					node->footnoteLabel));
		}
	}
	for (auto &child : node->children) {
		AssignFootnoteReferenceOrdinals(&child, definitions, warnings);
	}
}

void CollectAnchorIds(
		const MarkdownNode &node,
		std::vector<QString> *anchors) {
	if (!anchors) {
		return;
	}
	if (!node.anchorId.isEmpty()
		&& (node.kind == NodeKind::Heading
			|| node.kind == NodeKind::FootnoteDefinition)) {
		anchors->push_back(node.anchorId);
	}
	for (const auto &child : node.children) {
		CollectAnchorIds(child, anchors);
	}
}

void ValidateLocalFragments(
		const MarkdownNode &node,
		const std::vector<QString> &anchors,
		QStringList *warnings) {
	if (node.kind == NodeKind::Link && node.url.startsWith(QChar('#'))) {
		const auto fragment = NormalizeParsedFragmentId(node.url.mid(1));
		if (fragment.isEmpty() || !ContainsAnchorId(anchors, fragment)) {
			if (warnings) {
				warnings->push_back(
					u"Unresolved local fragment \"%1\""_q.arg(
						node.url));
			}
		}
	}
	for (const auto &child : node.children) {
		ValidateLocalFragments(child, anchors, warnings);
	}
}

[[nodiscard]] QString FirstHeadingTitle(const MarkdownNode &node) {
	if (node.kind == NodeKind::Heading) {
		return PlainText(node).trimmed();
	}
	for (const auto &child : node.children) {
		const auto result = FirstHeadingTitle(child);
		if (!result.isEmpty()) {
			return result;
		}
	}
	return QString();
}

void FillFormulaStats(PreparedDocument *document) {
	if (!document) {
		return;
	}
	document->stats.inlineFormulaCount = 0;
	document->stats.displayFormulaCount = 0;
	for (const auto &formula : document->formulas) {
		switch (formula.kind) {
		case MathKind::Inline:
			++document->stats.inlineFormulaCount;
			break;
		case MathKind::Display:
			++document->stats.displayFormulaCount;
			break;
		}
	}
}

} // namespace

void FinalizeDocumentSemantics(PreparedDocument *document) {
	if (!document) {
		return;
	}
	auto headingCounts = std::vector<std::pair<QString, int>>();
	AssignHeadingAnchors(
		&document->document,
		&headingCounts,
		&document->warnings);

	auto footnoteDefinitions = std::vector<std::pair<QString, int>>();
	auto nextFootnoteOrdinal = 1;
	AssignFootnoteDefinitionOrdinals(
		&document->document,
		&footnoteDefinitions,
		&nextFootnoteOrdinal,
		&document->warnings);
	AssignFootnoteReferenceOrdinals(
		&document->document,
		footnoteDefinitions,
		&document->warnings);

	auto anchors = std::vector<QString>();
	CollectAnchorIds(document->document, &anchors);
	ValidateLocalFragments(document->document, anchors, &document->warnings);
}

void NormalizeDisplayMathBlocks(
		PreparedDocument *document,
		const QByteArray &source,
		const std::vector<int> &lineStarts) {
	if (!document) {
		return;
	}
	const auto displayIndexes = DisplayFormulaIndexes(document->formulas);
	if (!displayIndexes.empty()) {
		NormalizeDisplayMathChildren(
			&document->document,
			document->formulas,
			displayIndexes,
			0,
			static_cast<int>(displayIndexes.size()),
			source,
			lineStarts);
	}
	document->stats.convertedNodeCount = CountNodes(document->document);
}

ParseResult ParseMarkdownForIv(ValidatedMarkdownSource source) {
	const auto &limits = ParseLimitsForIv();
	auto mask = std::vector<bool>(source.normalized.size(), false);
	const auto parserOptions = CMARK_OPT_DEFAULT
		| CMARK_OPT_SOURCEPOS
		| CMARK_OPT_FOOTNOTES
		| CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE;
	auto parser = ParserPointer(cmark_parser_new(parserOptions));
	if (!parser) {
		return Failure(
			source.sourceName,
			u"cmark-parser-failed"_q);
	}
	auto error = QString();
	if (!AttachExtensions(parser.get(), &error)) {
		return Failure(source.sourceName, std::move(error));
	}
	cmark_parser_feed(
		parser.get(),
		source.normalized.constData(),
		static_cast<std::size_t>(source.normalized.size()));
	auto root = NodePointer(cmark_parser_finish(parser.get()));
	if (!root) {
		return Failure(
			source.sourceName,
			u"cmark-parser-failed"_q);
	}
	auto document = EmptyDocument(std::move(source.sourceName));
	document.sourceText = std::move(source.decoded);
	auto scanBlocks = std::vector<MathScanBlock>();
	auto state = ParserState{
		source.normalized,
		source.lineStarts,
		&mask,
		&scanBlocks,
		&document.stats,
		&document.warnings,
	};
	if (!CollectScanMetadata(root.get(), &state, 0)) {
		return Failure(std::move(document.sourceName), std::move(state.error));
	}
	if (!ExtractMathRegions(
			source.normalized,
			mask,
			source.lineStarts,
			scanBlocks,
			limits.maxFormulaBytes,
			limits.maxFormulaCount,
			&document.formulas,
			&error)) {
		return Failure(std::move(document.sourceName), std::move(error));
	}
	if (!ConvertNode(
			root.get(),
			&state,
			0,
			&document.document)) {
		return Failure(
			std::move(document.sourceName),
			state.error.isEmpty()
				? u"cmark-conversion-failed"_q
				: std::move(state.error));
	}
	FillFormulaStats(&document);
	NormalizeDisplayMathBlocks(
		&document,
		source.normalized,
		source.lineStarts);
	FinalizeDocumentSemantics(&document);
	document.title = FirstHeadingTitle(document.document);
	document.empty = document.document.children.empty()
		&& document.formulas.empty();
	return ParseResult{ std::move(document), QString(), true };
}

} // namespace Iv::Markdown
