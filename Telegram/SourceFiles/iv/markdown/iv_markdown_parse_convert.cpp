/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_parse_convert.h"

#include <algorithm>
#include <array>
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

namespace Iv::Markdown {
namespace {

[[nodiscard]] QString ExtensionError(
		const QString &prefix,
		const char *name) {
	return u"%1-%2"_q.arg(prefix, QString::fromUtf8(name));
}

void AddWarning(ParserState *state, QString warning) {
	if (state && state->warnings && !warning.isEmpty()) {
		state->warnings->push_back(std::move(warning));
	}
}

[[nodiscard]] QString FromCmarkString(const char *value) {
	return value ? QString::fromUtf8(value) : QString();
}

[[nodiscard]] QString RawTypeString(cmark_node *node) {
	return node ? FromCmarkString(cmark_node_get_type_string(node)) : QString();
}

[[nodiscard]] bool IsCoreNodeType(cmark_node_type type) {
	constexpr auto kCoreTypes = std::array{
		CMARK_NODE_DOCUMENT,
		CMARK_NODE_BLOCK_QUOTE,
		CMARK_NODE_LIST,
		CMARK_NODE_ITEM,
		CMARK_NODE_CODE_BLOCK,
		CMARK_NODE_HTML_BLOCK,
		CMARK_NODE_CUSTOM_BLOCK,
		CMARK_NODE_PARAGRAPH,
		CMARK_NODE_HEADING,
		CMARK_NODE_THEMATIC_BREAK,
		CMARK_NODE_FOOTNOTE_DEFINITION,
		CMARK_NODE_TEXT,
		CMARK_NODE_SOFTBREAK,
		CMARK_NODE_LINEBREAK,
		CMARK_NODE_CODE,
		CMARK_NODE_HTML_INLINE,
		CMARK_NODE_CUSTOM_INLINE,
		CMARK_NODE_EMPH,
		CMARK_NODE_STRONG,
		CMARK_NODE_LINK,
		CMARK_NODE_IMAGE,
		CMARK_NODE_FOOTNOTE_REFERENCE,
	};
	return std::find(kCoreTypes.begin(), kCoreTypes.end(), type)
		!= kCoreTypes.end();
}

[[nodiscard]] QString CmarkKind(cmark_node *node) {
	if (!node) {
		return u"unknown"_q;
	}
	const auto type = cmark_node_get_type(node);
	const auto raw = RawTypeString(node);
	if (!IsCoreNodeType(type)) {
		return raw.isEmpty() ? u"unknown"_q : raw;
	}
	switch (type) {
	case CMARK_NODE_DOCUMENT: return u"document"_q;
	case CMARK_NODE_BLOCK_QUOTE: return u"block_quote"_q;
	case CMARK_NODE_LIST: return u"list"_q;
	case CMARK_NODE_ITEM: return u"item"_q;
	case CMARK_NODE_CODE_BLOCK: return u"code_block"_q;
	case CMARK_NODE_HTML_BLOCK: return u"html_block"_q;
	case CMARK_NODE_CUSTOM_BLOCK: return u"custom_block"_q;
	case CMARK_NODE_PARAGRAPH: return u"paragraph"_q;
	case CMARK_NODE_HEADING: return u"heading"_q;
	case CMARK_NODE_THEMATIC_BREAK: return u"thematic_break"_q;
	case CMARK_NODE_FOOTNOTE_DEFINITION:
		return u"footnote_definition"_q;
	case CMARK_NODE_TEXT: return u"text"_q;
	case CMARK_NODE_SOFTBREAK: return u"softbreak"_q;
	case CMARK_NODE_LINEBREAK: return u"linebreak"_q;
	case CMARK_NODE_CODE: return u"code"_q;
	case CMARK_NODE_HTML_INLINE: return u"html_inline"_q;
	case CMARK_NODE_CUSTOM_INLINE: return u"custom_inline"_q;
	case CMARK_NODE_EMPH: return u"emph"_q;
	case CMARK_NODE_STRONG: return u"strong"_q;
	case CMARK_NODE_LINK: return u"link"_q;
	case CMARK_NODE_IMAGE: return u"image"_q;
	case CMARK_NODE_FOOTNOTE_REFERENCE:
		return u"footnote_reference"_q;
	case CMARK_NODE_NONE: return u"none"_q;
	}
	return raw.isEmpty() ? u"unknown"_q : raw;
}

[[nodiscard]] SourceRange RangeFromCmarkLines(
		const std::vector<int> &lineStarts,
		int sourceSize,
		int startLine,
		int startColumn,
		int endLine,
		int endColumn) {
	auto result = SourceRange();
	result.startLine = startLine;
	result.startColumn = startColumn;
	result.endLine = endLine;
	result.endColumn = endColumn;
	const auto linesCount = static_cast<int>(lineStarts.size());
	if (startLine <= 0
		|| endLine <= 0
		|| startLine > linesCount
		|| endLine > linesCount) {
		return result;
	}
	const auto maxOffset = std::max(sourceSize, 0);
	const auto startOffset = lineStarts[startLine - 1]
		+ std::max(0, startColumn - 1);
	const auto endOffset = lineStarts[endLine - 1]
		+ std::max(0, endColumn);
	result.available = true;
	result.startOffset = std::clamp(startOffset, 0, maxOffset);
	result.endOffset = std::clamp(endOffset, 0, maxOffset);
	return result;
}

[[nodiscard]] SourceRange NodeRange(
		cmark_node *node,
		const std::vector<int> &lineStarts,
		int sourceSize) {
	return node
		? RangeFromCmarkLines(
			lineStarts,
			sourceSize,
			cmark_node_get_start_line(node),
			cmark_node_get_start_column(node),
			cmark_node_get_end_line(node),
			cmark_node_get_end_column(node))
		: SourceRange();
}

[[nodiscard, maybe_unused]] QString SourceSlice(
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

template <std::size_t Size>
[[nodiscard]] bool IsAnyKind(
		const QString &kind,
		const std::array<QString, Size> &values) {
	for (const auto &value : values) {
		if (kind == value) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool HasCmarkSourceRange(cmark_node *node) {
	return node
		&& cmark_node_get_start_line(node) > 0
		&& cmark_node_get_end_line(node) > 0;
}

[[nodiscard]] bool IsBlockNodeType(cmark_node_type type) {
	return (type & CMARK_NODE_TYPE_MASK) == CMARK_NODE_TYPE_BLOCK;
}

void MarkMaskRange(std::vector<bool> *mask, const SourceRange &range) {
	if (!mask || !range.available) {
		return;
	}
	const auto size = static_cast<int>(mask->size());
	const auto start = std::clamp(range.startOffset, 0, size);
	const auto end = std::clamp(range.endOffset, start, size);
	for (auto i = start; i != end; ++i) {
		(*mask)[i] = true;
	}
}

[[nodiscard]] bool IsMathMaskedNode(cmark_node *node) {
	if (!node) {
		return false;
	}
	constexpr auto kMaskedTypes = std::array{
		CMARK_NODE_CODE,
		CMARK_NODE_CODE_BLOCK,
		CMARK_NODE_HTML_INLINE,
		CMARK_NODE_HTML_BLOCK,
	};
	const auto type = cmark_node_get_type(node);
	return std::find(kMaskedTypes.begin(), kMaskedTypes.end(), type)
		!= kMaskedTypes.end();
}

[[nodiscard]] bool IsTaskListItem(cmark_node *node) {
	if (!node
		|| cmark_node_get_type(node) != CMARK_NODE_ITEM
		|| RawTypeString(node) != u"tasklist"_q) {
		return false;
	}
	(void)cmark_gfm_extensions_get_tasklist_item_checked(node);
	return true;
}

[[nodiscard]] bool IsBlockForMathParent(cmark_node *node) {
	if (!node) {
		return false;
	}
	const auto kind = CmarkKind(node);
	static const auto kBlockKinds = std::array{
		u"paragraph"_q,
		u"heading"_q,
		u"list"_q,
		u"item"_q,
		u"block_quote"_q,
		u"code_block"_q,
		u"html_block"_q,
		u"thematic_break"_q,
		u"table"_q,
		u"table_header"_q,
		u"table_row"_q,
		u"table_cell"_q,
		u"footnote_definition"_q,
	};
	if (IsAnyKind(kind, kBlockKinds)) {
		return true;
	}
	const auto type = cmark_node_get_type(node);
	return !IsCoreNodeType(type)
		&& IsBlockNodeType(type)
		&& HasCmarkSourceRange(node);
}

void RecordCapabilities(cmark_node *node, ParserState *state) {
	if (!node || !state || !state->stats) {
		return;
	}
	const auto kind = CmarkKind(node);
	static const auto kTableKinds = std::array{
		u"table"_q,
		u"table_header"_q,
		u"table_row"_q,
		u"table_cell"_q,
	};
	if (IsAnyKind(kind, kTableKinds)) {
		state->stats->tablesSeen = true;
	}
	if (IsTaskListItem(node)) {
		state->stats->taskListsSeen = true;
	}
	if (kind == u"strikethrough"_q) {
		state->stats->strikethroughSeen = true;
	} else if (kind == u"footnote_reference"_q
		|| kind == u"footnote_definition"_q) {
		state->stats->footnotesSeen = true;
	}
}

[[nodiscard]] bool FailScanMetadata(ParserState *state, QString error) {
	if (state) {
		state->error = std::move(error);
		state->failed = true;
	}
	return false;
}

[[nodiscard]] NodeKind ExtensionNodeKind(const QString &raw) {
	struct Entry {
		QString name;
		NodeKind kind = NodeKind::Unsupported;
	};
	static const auto kEntries = std::array{
		Entry{ u"strikethrough"_q, NodeKind::Strike },
		Entry{ u"table"_q, NodeKind::Table },
		Entry{ u"table_header"_q, NodeKind::TableRow },
		Entry{ u"table_row"_q, NodeKind::TableRow },
		Entry{ u"table_cell"_q, NodeKind::TableCell },
	};
	for (const auto &entry : kEntries) {
		if (raw == entry.name) {
			return entry.kind;
		}
	}
	return NodeKind::Unsupported;
}

[[nodiscard]] NodeKind NodeKindFor(cmark_node *node) {
	if (!node) {
		return NodeKind::Unsupported;
	}
	const auto type = cmark_node_get_type(node);
	switch (type) {
	case CMARK_NODE_DOCUMENT: return NodeKind::Document;
	case CMARK_NODE_BLOCK_QUOTE: return NodeKind::Blockquote;
	case CMARK_NODE_LIST: return NodeKind::List;
	case CMARK_NODE_ITEM: return NodeKind::ListItem;
	case CMARK_NODE_CODE_BLOCK: return NodeKind::CodeBlock;
	case CMARK_NODE_HTML_BLOCK: return NodeKind::HtmlBlock;
	case CMARK_NODE_PARAGRAPH: return NodeKind::Paragraph;
	case CMARK_NODE_HEADING: return NodeKind::Heading;
	case CMARK_NODE_THEMATIC_BREAK: return NodeKind::ThematicBreak;
	case CMARK_NODE_FOOTNOTE_DEFINITION: return NodeKind::FootnoteDefinition;
	case CMARK_NODE_TEXT: return NodeKind::Text;
	case CMARK_NODE_SOFTBREAK: return NodeKind::SoftBreak;
	case CMARK_NODE_LINEBREAK: return NodeKind::LineBreak;
	case CMARK_NODE_CODE: return NodeKind::InlineCode;
	case CMARK_NODE_HTML_INLINE: return NodeKind::HtmlInline;
	case CMARK_NODE_EMPH: return NodeKind::Emphasis;
	case CMARK_NODE_STRONG: return NodeKind::Strong;
	case CMARK_NODE_LINK: return NodeKind::Link;
	case CMARK_NODE_FOOTNOTE_REFERENCE: return NodeKind::FootnoteReference;
	default: break;
	}
	return ExtensionNodeKind(RawTypeString(node));
}

[[nodiscard]] ListKind ListKindFor(cmark_node *node) {
	return (node && cmark_node_get_list_type(node) == CMARK_ORDERED_LIST)
		? ListKind::Ordered
		: ListKind::Bullet;
}

[[nodiscard]] ListDelimiter ListDelimiterFor(cmark_node *node) {
	if (!node) {
		return ListDelimiter::None;
	}
	switch (cmark_node_get_list_delim(node)) {
	case CMARK_PERIOD_DELIM: return ListDelimiter::Period;
	case CMARK_PAREN_DELIM: return ListDelimiter::Parenthesis;
	case CMARK_NO_DELIM: return ListDelimiter::None;
	}
	return ListDelimiter::None;
}

[[nodiscard]] TaskState TaskStateFor(cmark_node *node) {
	if (!IsTaskListItem(node)) {
		return TaskState::None;
	}
	return cmark_gfm_extensions_get_tasklist_item_checked(node)
		? TaskState::Checked
		: TaskState::Unchecked;
}

[[nodiscard]] TableAlignment TableAlignmentFor(uint8_t value) {
	switch (value) {
	case 'l': return TableAlignment::Left;
	case 'c': return TableAlignment::Center;
	case 'r': return TableAlignment::Right;
	default: return TableAlignment::None;
	}
}

[[nodiscard]] std::vector<TableAlignment> TableAlignmentsFor(cmark_node *node) {
	auto result = std::vector<TableAlignment>();
	if (!node) {
		return result;
	}
	const auto columns = cmark_gfm_extensions_get_table_columns(node);
	if (!columns) {
		return result;
	}
	result.reserve(columns);
	const auto alignments = cmark_gfm_extensions_get_table_alignments(node);
	for (auto i = uint16_t(0); i != columns; ++i) {
		result.push_back(
			alignments
				? TableAlignmentFor(alignments[i])
				: TableAlignment::None);
	}
	return result;
}

[[nodiscard]] bool TableRowIsHeader(cmark_node *node) {
	return node && cmark_gfm_extensions_get_table_row_is_header(node) != 0;
}

[[nodiscard]] int TableCellColumn(cmark_node *node) {
	if (!node) {
		return -1;
	}
	auto result = 0;
	for (auto previous = cmark_node_previous(node); previous;) {
		if (NodeKindFor(previous) == NodeKind::TableCell) {
			++result;
		}
		previous = cmark_node_previous(previous);
	}
	return result;
}

[[nodiscard]] QString ExtractFootnoteLabel(
		QString raw,
		bool definition) {
	raw = raw.trimmed();
	if (!raw.startsWith(u"[^"_q)) {
		return QString();
	}
	const auto closing = raw.indexOf(u']');
	if (closing <= 2) {
		return QString();
	}
	if (definition && (closing + 1 >= raw.size() || raw[closing + 1] != u':')) {
		return QString();
	}
	return raw.mid(2, closing - 2).trimmed();
}

[[nodiscard]] bool ParseDetailsOpenAttribute(QString raw) {
	raw = raw.trimmed().toLower();
	if (raw.isEmpty()) {
		return false;
	}
	return (raw == u"open"_q)
		|| (raw == u"open=\"\""_q)
		|| (raw == u"open=''"_q)
		|| (raw == u"open=\"open\""_q)
		|| (raw == u"open='open'"_q);
}

[[nodiscard]] ParsedDetailsBlock ParseDetailsBlock(QString raw) {
	auto result = ParsedDetailsBlock();
	raw = raw.trimmed();
	if (!raw.startsWith(u"<details"_q, Qt::CaseInsensitive)
		|| !raw.endsWith(u"</details>"_q, Qt::CaseInsensitive)) {
		return result;
	}
	const auto openingEnd = raw.indexOf(QChar('>'));
	if (openingEnd < 0) {
		return result;
	}
	const auto openingAttributes = raw.mid(8, openingEnd - 8);
	if (!openingAttributes.trimmed().isEmpty()
		&& !ParseDetailsOpenAttribute(openingAttributes)) {
		return result;
	}
	result.open = ParseDetailsOpenAttribute(openingAttributes);
	auto inner = raw.mid(
		openingEnd + 1,
		raw.size() - openingEnd - 11);
	if (inner.contains(u"<details"_q, Qt::CaseInsensitive)) {
		return result;
	}
	inner = inner.trimmed();
	if (!inner.startsWith(u"<summary>"_q, Qt::CaseInsensitive)) {
		return result;
	}
	const auto summaryClosing = inner.indexOf(
		u"</summary>"_q,
		0,
		Qt::CaseInsensitive);
	if (summaryClosing < 0) {
		return result;
	}
	result.summary = inner.mid(9, summaryClosing - 9).trimmed();
	result.body = inner.mid(summaryClosing + 10).trimmed();
	result.ok = !result.summary.isEmpty();
	return result;
}

[[nodiscard]] QString PlainText(const MarkdownNode &node) {
	auto result = node.text;
	for (const auto &child : node.children) {
		result.append(PlainText(child));
	}
	return result;
}

[[nodiscard]] bool LinkLooksAutolink(const MarkdownNode &node) {
	const auto text = PlainText(node);
	if (text.isEmpty() || node.url.isEmpty()) {
		return false;
	}
	if (text == node.url) {
		return true;
	}
	const auto mailto = u"mailto:"_q;
	return node.url.startsWith(mailto, Qt::CaseInsensitive)
		&& text == node.url.mid(mailto.size());
}

void FillNodeAttributes(
		cmark_node *node,
		ParserState *state,
		MarkdownNode *out) {
	switch (out->kind) {
	case NodeKind::Text:
	case NodeKind::InlineCode:
		out->text = FromCmarkString(cmark_node_get_literal(node));
		break;
	case NodeKind::CodeBlock:
		out->text = FromCmarkString(cmark_node_get_literal(node));
		out->info = FromCmarkString(cmark_node_get_fence_info(node));
		break;
	case NodeKind::HtmlBlock: {
		out->raw = FromCmarkString(cmark_node_get_literal(node));
		const auto trimmed = out->raw.trimmed();
		if (trimmed.startsWith(u"<!--"_q) && trimmed.endsWith(u"-->"_q)) {
			out->htmlBlockKind = HtmlBlockKind::Comment;
		} else if (trimmed.startsWith(u"<details"_q, Qt::CaseInsensitive)) {
			const auto details = ParseDetailsBlock(out->raw);
			if (details.ok) {
				out->htmlBlockKind = HtmlBlockKind::Details;
				out->detailsSummary = details.summary;
				out->detailsBody = details.body;
				out->detailsOpen = details.open;
			} else {
				out->htmlBlockKind = HtmlBlockKind::Unsupported;
				AddWarning(
					state,
					u"Malformed details block at %1:%2"_q.arg(
						out->range.startLine
					).arg(
						out->range.startColumn));
			}
		} else if (!trimmed.isEmpty()) {
			out->htmlBlockKind = HtmlBlockKind::Unsupported;
			AddWarning(
				state,
				u"Unsupported HTML block at %1:%2"_q.arg(
					out->range.startLine
				).arg(
					out->range.startColumn));
		}
	} break;
	case NodeKind::HtmlInline:
		out->raw = FromCmarkString(cmark_node_get_literal(node));
		break;
	case NodeKind::SoftBreak:
	case NodeKind::LineBreak:
		out->text = u"\n"_q;
		break;
	case NodeKind::Heading:
		out->headingLevel = cmark_node_get_heading_level(node);
		break;
	case NodeKind::List:
		out->listKind = ListKindFor(node);
		out->listDelimiter = ListDelimiterFor(node);
		out->listStart = cmark_node_get_list_start(node);
		out->tight = cmark_node_get_list_tight(node) != 0;
		break;
	case NodeKind::ListItem:
		out->taskState = TaskStateFor(node);
		break;
	case NodeKind::Table:
		out->tableAlignments = TableAlignmentsFor(node);
		break;
	case NodeKind::TableRow:
		out->tableHeader = TableRowIsHeader(node);
		break;
	case NodeKind::TableCell:
		out->tableColumn = TableCellColumn(node);
		break;
	case NodeKind::Link:
		out->url = FromCmarkString(cmark_node_get_url(node));
		out->title = FromCmarkString(cmark_node_get_title(node));
		break;
	case NodeKind::FootnoteReference:
		out->raw = SourceSlice(state->normalizedSource, out->range);
		if (const auto parent = cmark_node_parent_footnote_def(node)) {
			out->footnoteLabel = FromCmarkString(cmark_node_get_literal(parent));
		}
		if (out->footnoteLabel.isEmpty()) {
			out->footnoteLabel = ExtractFootnoteLabel(out->raw, false);
		}
		break;
	case NodeKind::FootnoteDefinition:
		out->raw = SourceSlice(state->normalizedSource, out->range);
		out->footnoteLabel = FromCmarkString(cmark_node_get_literal(node));
		if (out->footnoteLabel.isEmpty()) {
			out->footnoteLabel = ExtractFootnoteLabel(out->raw, true);
		}
		break;
	default:
		break;
	}
}

void EnsureCmarkExtensionsRegistered() {
	static std::once_flag once;
	std::call_once(once, [] {
		cmark_gfm_core_extensions_ensure_registered();
	});
}

} // namespace

void ParserDeleter::operator()(cmark_parser *parser) const {
	if (parser) {
		cmark_parser_free(parser);
	}
}

void NodeDeleter::operator()(cmark_node *node) const {
	if (node) {
		cmark_node_free(node);
	}
}

bool AttachExtensions(cmark_parser *parser, QString *error) {
	if (!parser) {
		if (error) {
			*error = u"cmark-parser-failed"_q;
		}
		return false;
	}
	EnsureCmarkExtensionsRegistered();
	constexpr auto kExtensions = std::array<const char *, 5>{
		"table",
		"strikethrough",
		"autolink",
		"tagfilter",
		"tasklist",
	};
	for (const auto name : kExtensions) {
		const auto extension = cmark_find_syntax_extension(name);
		if (!extension) {
			if (error) {
				*error = ExtensionError(u"cmark-extension-missing"_q, name);
			}
			return false;
		}
		if (!cmark_parser_attach_syntax_extension(parser, extension)) {
			if (error) {
				*error = ExtensionError(
					u"cmark-extension-attach-failed"_q,
					name);
			}
			return false;
		}
	}
	if (error) {
		error->clear();
	}
	return true;
}

bool CollectScanMetadata(
		cmark_node *node,
		ParserState *state,
		int depth) {
	const auto &limits = ParseLimitsForIv();
	if (!node || !state || state->failed) {
		return false;
	}
	if (state->stats) {
		state->stats->maxDepth = std::max(state->stats->maxDepth, depth);
	}
	if (depth > limits.maxNesting) {
		return FailScanMetadata(state, u"cmark-nesting-too-deep"_q);
	}
	if (state->stats) {
		++state->stats->cmarkNodeCount;
		if (state->stats->cmarkNodeCount > limits.maxCmarkNodes) {
			return FailScanMetadata(state, u"too-many-cmark-nodes"_q);
		}
	}
	RecordCapabilities(node, state);
	const auto range = NodeRange(
		node,
		state->lineStarts,
		static_cast<int>(state->normalizedSource.size()));
	if (IsBlockForMathParent(node) && state->scanBlocks) {
		state->scanBlocks->push_back(MathScanBlock{ range, CmarkKind(node) });
	}
	if (IsMathMaskedNode(node)) {
		MarkMaskRange(state->mask, range);
	}
	for (auto child = cmark_node_first_child(node); child;) {
		const auto next = cmark_node_next(child);
		if (!CollectScanMetadata(child, state, depth + 1)) {
			return false;
		}
		child = next;
	}
	return true;
}

QString NormalizeParsedFragmentId(QString fragment) {
	fragment = QString::fromUtf8(
		QByteArray::fromPercentEncoding(fragment.toUtf8()));
	fragment = fragment.trimmed().toLower();
	while (fragment.startsWith(u"#"_q)) {
		fragment.remove(0, 1);
	}
	return fragment;
}

bool ConvertNode(
		cmark_node *node,
		ParserState *state,
		int depth,
		MarkdownNode *out) {
	const auto &limits = ParseLimitsForIv();
	if (!node || !state || !out || state->failed) {
		return false;
	}
	if (depth > limits.maxNesting) {
		return FailScanMetadata(state, u"cmark-nesting-too-deep"_q);
	}
	out->kind = NodeKindFor(node);
	out->range = NodeRange(
		node,
		state->lineStarts,
		static_cast<int>(state->normalizedSource.size()));
	if (out->kind == NodeKind::Unsupported) {
		out->unsupportedKind = CmarkKind(node);
		out->raw = SourceSlice(state->normalizedSource, out->range);
	}
	FillNodeAttributes(node, state, out);
	for (auto child = cmark_node_first_child(node); child;) {
		const auto next = cmark_node_next(child);
		auto converted = MarkdownNode();
		if (!ConvertNode(child, state, depth + 1, &converted)) {
			return false;
		}
		out->children.push_back(std::move(converted));
		child = next;
	}
	if (out->kind == NodeKind::Link) {
		out->autolink = LinkLooksAutolink(*out);
		if (out->autolink && state->stats) {
			state->stats->autolinksSeen = true;
		}
	}
	if (state->stats) {
		++state->stats->convertedNodeCount;
	}
	return true;
}

} // namespace Iv::Markdown
