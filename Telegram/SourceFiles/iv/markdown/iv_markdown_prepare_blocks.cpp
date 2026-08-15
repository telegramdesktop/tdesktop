/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_blocks.h"
#include "iv/markdown/iv_markdown_prepare_inline.h"
#include "iv/markdown/iv_markdown_prepare_links.h"

#include <QtCore/QString>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace Iv::Markdown {
namespace {

[[nodiscard]] QString DetailsAnchorId(PrepareState *state) {
	return u"details-"_q + QString::number(++state->nextGeneratedId);
}

[[nodiscard]] int CappedListDepth(int depth) {
	return std::min(depth, std::max(PrepareLimitsForIv().visualListDepth, 0));
}

[[nodiscard]] int CappedQuoteDepth(int depth) {
	return std::min(depth, std::max(PrepareLimitsForIv().visualQuoteDepth, 0));
}

[[nodiscard]] int ScaleFormulaCap(int cap, int textSize, int baseTextSize) {
	if (cap <= 0) {
		return 0;
	}
	const auto numerator = int64(cap) * std::max(textSize, 1);
	const auto denominator = std::max(baseTextSize, 1);
	return std::max(int((numerator + denominator - 1) / denominator), 1);
}

[[nodiscard]] QString StripOneTrailingNewline(QString text) {
	if (text.endsWith(u"\r\n"_q)) {
		text.chop(2);
	} else if (!text.isEmpty()) {
		const auto last = text.back();
		if ((last == QChar(u'\n')) || (last == QChar(u'\r'))) {
			text.chop(1);
		}
	}
	return text;
}

[[nodiscard]] int FlowFormulaTextSize(
		PreparedBlockKind kind,
		int headingLevel,
		const MarkdownPrepareDimensions &dimensions) {
	if (kind != PreparedBlockKind::Heading) {
		return dimensions.bodyTextSize;
	}
	const auto index = std::clamp(headingLevel, 1, 6) - 1;
	if (index < int(dimensions.headingTextSizes.size())) {
		return dimensions.headingTextSizes[index];
	}
	return dimensions.bodyTextSize;
}

[[nodiscard]] int TableCellFormulaTextSize(
		bool header,
		const MarkdownPrepareDimensions &dimensions) {
	return header
		? dimensions.tableHeaderTextSize
		: dimensions.tableBodyTextSize;
}

void PrepareTableCellText(
		const MarkdownNode &cell,
		bool header,
		TextWithEntities *text,
		std::vector<PreparedLink> *links,
		PrepareState *state) {
	const auto textSize = TableCellFormulaTextSize(
		header,
		state->request->dimensions);
	PrepareInlineRichText(
		cell,
		textSize,
		ScaleFormulaCap(
			state->request->dimensions.displayMathMaxRenderWidth,
			textSize,
			state->request->dimensions.displayMathTextSize),
		ScaleFormulaCap(
			state->request->dimensions.displayMathMaxRenderHeight,
			textSize,
			state->request->dimensions.displayMathTextSize),
		nullptr,
		text,
		links,
	state);
	SortEntities(text);
}

[[nodiscard]] int EffectiveTableRowWidth(const MarkdownNode &row) {
	auto result = 0;
	auto expectedColumn = 0;
	for (const auto &cell : row.children) {
		if (cell.kind != NodeKind::TableCell) {
			return 0;
		}
		const auto column = (cell.tableColumn >= 0)
			? cell.tableColumn
			: expectedColumn;
		if (column != expectedColumn) {
			return 0;
		}
		result = std::max(result, column + 1);
		++expectedColumn;
	}
	return result;
}

[[nodiscard]] int EffectiveTableColumnCount(const MarkdownNode &node) {
	auto result = int(node.tableAlignments.size());
	for (const auto &row : node.children) {
		if (row.kind != NodeKind::TableRow) {
			return 0;
		}
		const auto width = EffectiveTableRowWidth(row);
		if (!width) {
			return 0;
		}
		result = std::max(result, width);
	}
	return result;
}

[[nodiscard]] std::vector<TableAlignment> NormalizedTableAlignments(
		const MarkdownNode &node,
		int columnCount) {
	auto result = std::vector<TableAlignment>(
		std::max(columnCount, 0),
		TableAlignment::None);
	const auto limit = std::min(columnCount, int(node.tableAlignments.size()));
	for (auto i = 0; i != limit; ++i) {
		result[i] = node.tableAlignments[i];
	}
	return result;
}

[[nodiscard]] bool ShouldFlattenTable(
		const MarkdownNode &node,
		PrepareState *state) {
	const auto &limits = PrepareMarkdownTableRenderLimitsForIv();
	if (node.children.empty()) {
		if (state) {
			state->addPrepareWarning();
		}
		return true;
	}
	const auto rowCount = int(node.children.size());
	if (rowCount > limits.maxRows) {
		if (state) {
			state->addPrepareWarning();
		}
		return true;
	}
	auto cellCount = 0;
	for (const auto &row : node.children) {
		if (row.kind != NodeKind::TableRow || row.children.empty()) {
			if (state) {
				state->addPrepareWarning();
			}
			return true;
		}
		const auto width = EffectiveTableRowWidth(row);
		if (!width || width > limits.maxColumns) {
			if (state) {
				state->addPrepareWarning();
			}
			return true;
		}
		cellCount += width;
		if (cellCount > limits.maxCells) {
			if (state) {
				state->addPrepareWarning();
			}
			return true;
		}
	}
	const auto columnCount = EffectiveTableColumnCount(node);
	if (!columnCount || columnCount > limits.maxColumns) {
		if (state) {
			state->addPrepareWarning();
		}
		return true;
	}
	if ((rowCount * columnCount) > limits.maxCells) {
		if (state) {
			state->addPrepareWarning();
		}
		return true;
	}
	return false;
}

[[nodiscard]] std::vector<PreparedBlock> PrepareFallbackBlocks(
	const MarkdownNode &node,
	PrepareContext context,
	PrepareState *state);

[[nodiscard]] std::vector<PreparedBlock> PrepareTableBlocks(
		const MarkdownNode &node,
		PrepareContext context,
	PrepareState *state) {
	const auto columnCount = EffectiveTableColumnCount(node);
	if (ShouldFlattenTable(node, state) || !columnCount) {
		return PrepareFallbackBlocks(node, context, state);
	}

	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Table;
	block.tableColumnCount = columnCount;
	block.tableAlignments = NormalizedTableAlignments(node, columnCount);
	block.tableBordered = true;
	block.tableStriped = false;
	block.tableRows.reserve(node.children.size());

	for (const auto &rowNode : node.children) {
		if (rowNode.kind != NodeKind::TableRow) {
			return PrepareFallbackBlocks(node, context, state);
		}

		auto row = PreparedTableRow();
		row.header = rowNode.tableHeader;
		row.cells.reserve(columnCount);

		auto expectedColumn = 0;
		for (const auto &cellNode : rowNode.children) {
			if (cellNode.kind != NodeKind::TableCell) {
				return PrepareFallbackBlocks(node, context, state);
			}
			const auto column = (cellNode.tableColumn >= 0)
				? cellNode.tableColumn
				: expectedColumn;
			if (column != expectedColumn || column >= columnCount) {
				return PrepareFallbackBlocks(node, context, state);
			}

			auto cell = PreparedTableCell();
			cell.column = column;
			cell.alignment = block.tableAlignments[column];
			cell.header = rowNode.tableHeader;
			cell.verticalAlignment = PreparedTableCellVerticalAlignment::Top;
			cell.colspan = 1;
			cell.rowspan = 1;
			PrepareTableCellText(
				cellNode,
				rowNode.tableHeader,
				&cell.text,
				&cell.links,
				state);
			row.cells.push_back(std::move(cell));
			++expectedColumn;
		}
		for (auto column = expectedColumn; column != columnCount; ++column) {
			auto cell = PreparedTableCell();
			cell.column = column;
			cell.alignment = block.tableAlignments[column];
			cell.header = rowNode.tableHeader;
			cell.verticalAlignment = PreparedTableCellVerticalAlignment::Top;
			cell.colspan = 1;
			cell.rowspan = 1;
			row.cells.push_back(std::move(cell));
		}
		block.tableRows.push_back(std::move(row));
	}
	return { std::move(block) };
}

void AppendPrepared(
		std::vector<PreparedBlock> &&from,
		std::vector<PreparedBlock> *to) {
	for (auto &block : from) {
		to->push_back(std::move(block));
	}
}

void AppendRichBlock(
		std::vector<PreparedBlock> *blocks,
		PreparedBlockKind kind,
		int headingLevel,
		TextWithEntities text,
		std::vector<PreparedLink> links,
		QString anchorId = QString(),
		bool collapsed = false,
		bool allowEmpty = false) {
	SortEntities(&text);
	if (text.text.isEmpty() && !allowEmpty) {
		return;
	}
	auto block = PreparedBlock();
	block.kind = kind;
	block.headingLevel = headingLevel;
	block.text = std::move(text);
	block.links = std::move(links);
	block.anchorId = std::move(anchorId);
	block.collapsed = collapsed;
	blocks->push_back(std::move(block));
}

[[nodiscard]] PreparedBlock EmptyParagraphBlock() {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Paragraph;
	return block;
}

[[nodiscard]] PreparedBlock PrepareCodeBlock(const MarkdownNode &node) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::CodeBlock;
	block.text.text = StripOneTrailingNewline(node.text);
	block.codeLanguage = FirstInfoToken(node.info);
	return block;
}

[[nodiscard]] PreparedBlock PrepareRuleBlock() {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Rule;
	return block;
}

[[nodiscard]] PreparedBlock PrepareDisplayMathBlock(
		const MarkdownNode &node,
		PrepareState *state) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::DisplayMath;
	block.formulaTex = !node.text.isEmpty() ? node.text : node.raw;
	block.mathKind = MathKind::Display;
	block.formulaIndex = node.formulaIndex;
	state->rememberFormula(block);
	return block;
}

void CollectFootnoteDefinitions(
		const MarkdownNode &node,
		std::vector<FootnoteDefinitionEntry> *definitions) {
	if (!definitions) {
		return;
	}
	if (node.kind == NodeKind::FootnoteDefinition && node.footnoteOrdinal > 0) {
		if (node.footnoteOrdinal > int(definitions->size())) {
			definitions->resize(node.footnoteOrdinal);
		}
		auto &entry = (*definitions)[node.footnoteOrdinal - 1];
		if (!entry.node) {
			entry.node = &node;
		}
	}
	for (const auto &child : node.children) {
		CollectFootnoteDefinitions(child, definitions);
	}
}

[[nodiscard]] std::vector<PreparedBlock> PrepareBlocks(
	const MarkdownNode &node,
	PrepareContext context,
	PrepareState *state);

[[nodiscard]] std::vector<PreparedBlock> PrepareChildren(
		const MarkdownNode &node,
		PrepareContext context,
		PrepareState *state) {
	auto result = std::vector<PreparedBlock>();
	for (const auto &child : node.children) {
		AppendPrepared(PrepareBlocks(child, context, state), &result);
	}
	return result;
}

[[nodiscard]] uint16 InternalPreparedLinkIndex(const QString &data) {
	const auto prefix = u"internal:index"_q;
	return (data.size() == prefix.size() + 1 && data.startsWith(prefix))
		? uint16(data.back().unicode())
		: uint16(0);
}

[[nodiscard]] uint16 RemappedPreparedLinkIndex(
		const std::vector<std::pair<uint16, uint16>> &remapped,
		uint16 index) {
	for (const auto &[from, to] : remapped) {
		if (from == index) {
			return to;
		}
	}
	return 0;
}

void AppendFootnoteTextFragment(
		const TextWithEntities &fromText,
		const std::vector<PreparedLink> &fromLinks,
		TextWithEntities *toText,
		std::vector<PreparedLink> *toLinks) {
	auto remapped = std::vector<std::pair<uint16, uint16>>();
	remapped.reserve(fromLinks.size());
	for (const auto &link : fromLinks) {
		const auto index = toLinks->size() + 1;
		if (index > std::numeric_limits<uint16>::max()) {
			continue;
		}
		auto copy = link;
		copy.index = uint16(index);
		remapped.push_back({ link.index, copy.index });
		toLinks->push_back(std::move(copy));
	}

	const auto shift = toText->text.size();
	toText->text.append(fromText.text);
	toText->entities.reserve(toText->entities.size() + fromText.entities.size());
	for (const auto &entity : fromText.entities) {
		auto data = entity.data();
		if (entity.type() == EntityType::CustomUrl) {
			if (const auto from = InternalPreparedLinkIndex(data)) {
				if (const auto to = RemappedPreparedLinkIndex(remapped, from)) {
					data = InternalLinkData(to);
				}
			}
		}
		toText->entities.push_back(EntityInText(
			entity.type(),
			entity.offset() + shift,
			entity.length(),
			data));
	}
}

void AppendFootnoteVisibleText(
		const TextWithEntities &fromText,
		const std::vector<PreparedLink> &fromLinks,
		TextWithEntities *toText,
		std::vector<PreparedLink> *toLinks) {
	if (fromText.text.isEmpty()) {
		return;
	}
	if (!toText->text.isEmpty()) {
		toText->text.append(u"\n\n"_q);
	}
	AppendFootnoteTextFragment(fromText, fromLinks, toText, toLinks);
}

void FlattenFootnoteBlock(
		const PreparedBlock &block,
		TextWithEntities *text,
		std::vector<PreparedLink> *links) {
	AppendFootnoteVisibleText(block.text, block.links, text, links);
	for (const auto &row : block.tableRows) {
		for (const auto &cell : row.cells) {
			AppendFootnoteVisibleText(cell.text, cell.links, text, links);
		}
	}
	for (const auto &child : block.children) {
		FlattenFootnoteBlock(child, text, links);
	}
}

void FlattenFootnoteBlocks(
		const std::vector<PreparedBlock> &blocks,
		TextWithEntities *text,
		std::vector<PreparedLink> *links) {
	for (const auto &block : blocks) {
		FlattenFootnoteBlock(block, text, links);
	}
	SortEntities(text);
}

void PrepareFootnotes(PrepareState *state) {
	if (!state || state->footnoteDefinitions.empty()) {
		return;
	}
	state->result.footnotes.reserve(state->footnoteDefinitions.size());
	for (const auto &entry : state->footnoteDefinitions) {
		if (!entry.node) {
			continue;
		}
		auto footnote = PreparedFootnote();
		footnote.label = !entry.node->footnoteLabel.isEmpty()
			? entry.node->footnoteLabel
			: QString::number(entry.node->footnoteOrdinal);
		footnote.displayText = u"["_q + footnote.label + u"]"_q;
		footnote.blocks = PrepareChildren(*entry.node, {}, state);
		FlattenFootnoteBlocks(footnote.blocks, &footnote.text, &footnote.links);
		if (footnote.text.text.isEmpty()) {
			footnote.text = TextWithEntities::Simple(footnote.displayText);
		}
		state->result.footnotes.push_back(std::move(footnote));
	}
}

[[nodiscard]] std::vector<PreparedBlock> PrepareNestedDetailsBody(
		const MarkdownNode &node,
		PrepareState *state) {
	const auto fallback = [&] {
		if (state) {
			state->addPrepareWarning();
		}
		auto blocks = std::vector<PreparedBlock>();
		AppendRichBlock(
			&blocks,
			PreparedBlockKind::Paragraph,
			0,
			TextWithEntities::Simple(node.detailsBody),
			std::vector<PreparedLink>());
		return blocks;
	};
	if (node.detailsBody.isEmpty()) {
		return {};
	}
	const auto parsed = ParseMarkdownForIv(
		node.detailsBody.toUtf8(),
		ParseOptions{ state->request->document->sourceName + u"#details"_q });
	if (!parsed.ok
		|| !parsed.document.formulas.empty()
		|| parsed.document.stats.footnotesSeen) {
		return fallback();
	}
	auto nestedRequest = PrepareRequest{
		.document = std::make_shared<const PreparedDocument>(parsed.document),
		.renderer = state->request->renderer,
		.dimensions = state->request->dimensions,
		.sourcePath = state->request->sourcePath,
	};
	auto nested = PrepareSynchronously(std::move(nestedRequest));
	state->addPrepareWarnings(nested.debug.prepareWarningCount);
	state->addFormulaWarnings(nested.debug.formulaWarningCount);
	return nested.failure.failed()
		? fallback()
		: std::move(nested.blocks.blocks);
}

[[nodiscard]] std::vector<PreparedBlock> PrepareDetailsBlocks(
		const MarkdownNode &node,
		PrepareState *state) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Details;
	block.anchorId = DetailsAnchorId(state);
	block.collapsed = !node.detailsOpen;
	block.text = TextWithEntities::Simple(node.detailsSummary);
	block.children = PrepareNestedDetailsBody(node, state);
	return { std::move(block) };
}

[[nodiscard]] std::vector<PreparedBlock> PrepareDocumentBlocks(
		const MarkdownNode &node,
		PrepareState *state) {
	return PrepareChildren(node, {}, state);
}

[[nodiscard]] std::vector<PreparedBlock> PrepareFlowBlock(
		const MarkdownNode &node,
		PreparedBlockKind kind,
		PrepareState *state) {
	auto result = std::vector<PreparedBlock>();
	auto anchorId = (kind == PreparedBlockKind::Heading)
		? node.anchorId
		: QString();
	auto text = TextWithEntities();
	auto links = std::vector<PreparedLink>();
	const auto textSize = FlowFormulaTextSize(
		kind,
		(kind == PreparedBlockKind::Heading) ? node.headingLevel : 0,
		state->request->dimensions);
	PrepareInlineRichText(
		node,
		textSize,
		ScaleFormulaCap(
			state->request->dimensions.displayMathMaxRenderWidth,
			textSize,
			state->request->dimensions.displayMathTextSize),
		ScaleFormulaCap(
			state->request->dimensions.displayMathMaxRenderHeight,
			textSize,
			state->request->dimensions.displayMathTextSize),
		&anchorId,
		&text,
		&links,
		state);
	AppendRichBlock(
		&result,
		kind,
		(kind == PreparedBlockKind::Heading) ? node.headingLevel : 0,
		std::move(text),
		std::move(links),
		std::move(anchorId));
	return result;
}

[[nodiscard]] PreparedBlock PrepareListItemBlock(
		const MarkdownNode &node,
		PrepareContext context,
		const PreparedBlock &list,
		int orderedNumber,
		PrepareState *state) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::ListItem;
	block.listKind = list.listKind;
	block.listDelimiter = list.listDelimiter;
	block.taskState = node.taskState;
	block.orderedNumber = orderedNumber;
	block.actualDepth = list.actualDepth;
	block.visualDepth = list.visualDepth;
	block.depthClamped = list.depthClamped;

	auto childContext = context;
	childContext.listDepth = context.listDepth + 1;
	for (const auto &child : node.children) {
		AppendPrepared(PrepareBlocks(child, childContext, state), &block.children);
	}
	if (block.children.empty()) {
		block.children.push_back(EmptyParagraphBlock());
	}
	return block;
}

[[nodiscard]] PreparedBlock PrepareListBlock(
		const MarkdownNode &node,
		PrepareContext context,
		PrepareState *state) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::List;
	block.listKind = node.listKind;
	block.listDelimiter = node.listDelimiter;
	block.startNumber = (node.listKind == ListKind::Ordered && node.listStart > 0)
		? node.listStart
		: 1;
	block.actualDepth = context.listDepth;
	block.visualDepth = CappedListDepth(block.actualDepth);
	block.depthClamped = (block.actualDepth > block.visualDepth);
	block.tight = node.tight;

	auto nextNumber = block.startNumber;
	auto childContext = context;
	childContext.listDepth = context.listDepth + 1;
	for (const auto &child : node.children) {
		if (child.kind == NodeKind::ListItem) {
			auto item = PrepareListItemBlock(
				child,
				context,
				block,
				(node.listKind == ListKind::Ordered) ? nextNumber : 0,
				state);
			block.children.push_back(std::move(item));
			if (node.listKind == ListKind::Ordered) {
				++nextNumber;
			}
		} else {
			AppendPrepared(PrepareBlocks(child, childContext, state), &block.children);
		}
	}
	return block;
}

[[nodiscard]] PreparedBlock PrepareQuoteBlock(
		const MarkdownNode &node,
		PrepareContext context,
		PrepareState *state) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Quote;
	block.actualDepth = context.quoteDepth;
	block.visualDepth = CappedQuoteDepth(block.actualDepth);
	block.depthClamped = (block.actualDepth > block.visualDepth);

	auto childContext = context;
	childContext.quoteDepth = context.quoteDepth + 1;
	block.children = PrepareChildren(node, childContext, state);
	return block;
}

[[nodiscard]] std::vector<PreparedBlock> PrepareFallbackBlocks(
		const MarkdownNode &node,
		PrepareContext context,
		PrepareState *state) {
	if (node.kind == NodeKind::HtmlBlock) {
		if (node.htmlBlockKind == HtmlBlockKind::Comment) {
			return {};
		} else if (node.htmlBlockKind == HtmlBlockKind::Details) {
			return PrepareDetailsBlocks(node, state);
		}
	}
	if (!node.children.empty()) {
		return PrepareChildren(node, context, state);
	}
	const auto text = !node.text.isEmpty() ? node.text : node.raw;
	if (text.isEmpty()) {
		return {};
	}
	auto result = std::vector<PreparedBlock>();
	AppendRichBlock(
		&result,
		PreparedBlockKind::Paragraph,
		0,
		TextWithEntities::Simple(text),
		std::vector<PreparedLink>());
	return result;
}

[[nodiscard]] std::vector<PreparedBlock> PrepareBlocks(
		const MarkdownNode &node,
		PrepareContext context,
		PrepareState *state) {
	switch (node.kind) {
	case NodeKind::Document:
		return PrepareDocumentBlocks(node, state);
	case NodeKind::TableRow:
	case NodeKind::TableCell:
	case NodeKind::HtmlBlock:
	case NodeKind::Unsupported:
		return PrepareFallbackBlocks(node, context, state);
	case NodeKind::DisplayMath:
		return { PrepareDisplayMathBlock(node, state) };
	case NodeKind::Paragraph:
		return PrepareFlowBlock(node, PreparedBlockKind::Paragraph, state);
	case NodeKind::Heading:
		return PrepareFlowBlock(node, PreparedBlockKind::Heading, state);
	case NodeKind::FootnoteDefinition:
		return {};
	case NodeKind::CodeBlock:
		return { PrepareCodeBlock(node) };
	case NodeKind::ThematicBreak:
		return { PrepareRuleBlock() };
	case NodeKind::List: {
		auto block = PrepareListBlock(node, context, state);
		return { std::move(block) };
	} break;
	case NodeKind::ListItem: {
		auto list = PreparedBlock();
		list.kind = PreparedBlockKind::List;
		list.actualDepth = context.listDepth;
		list.visualDepth = CappedListDepth(list.actualDepth);
		list.depthClamped = (list.actualDepth > list.visualDepth);
		auto block = PrepareListItemBlock(node, context, list, 0, state);
		return { std::move(block) };
	} break;
	case NodeKind::Blockquote: {
		auto block = PrepareQuoteBlock(node, context, state);
		return { std::move(block) };
	} break;
	case NodeKind::Table:
		return PrepareTableBlocks(node, context, state);
	default:
		return PrepareFallbackBlocks(node, context, state);
	}
	return {};
}

} // namespace

PreparedRenderDocument PrepareRenderData(
		const PreparedDocument &document,
		PrepareState *state) {
	auto result = PreparedRenderDocument();
	state->result.footnotes.clear();
	state->footnoteDefinitions.clear();
	CollectFootnoteDefinitions(document.document, &state->footnoteDefinitions);
	result.blocks = PrepareBlocks(document.document, {}, state);
	PrepareFootnotes(state);
	return result;
}

} // namespace Iv::Markdown
