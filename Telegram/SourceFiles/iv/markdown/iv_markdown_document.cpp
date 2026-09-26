/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_document.h"

#include <utility>

namespace Iv::Markdown {
namespace {

[[nodiscard]] QString BoolString(bool value) {
	return value ? u"true"_q : u"false"_q;
}

[[nodiscard]] QString NodeKindName(NodeKind kind) {
	switch (kind) {
	case NodeKind::Document: return u"Document"_q;
	case NodeKind::Paragraph: return u"Paragraph"_q;
	case NodeKind::Heading: return u"Heading"_q;
	case NodeKind::Text: return u"Text"_q;
	case NodeKind::Emphasis: return u"Emphasis"_q;
	case NodeKind::Strong: return u"Strong"_q;
	case NodeKind::Strike: return u"Strike"_q;
	case NodeKind::InlineCode: return u"InlineCode"_q;
	case NodeKind::CodeBlock: return u"CodeBlock"_q;
	case NodeKind::Link: return u"Link"_q;
	case NodeKind::List: return u"List"_q;
	case NodeKind::ListItem: return u"ListItem"_q;
	case NodeKind::Blockquote: return u"Blockquote"_q;
	case NodeKind::ThematicBreak: return u"ThematicBreak"_q;
	case NodeKind::Table: return u"Table"_q;
	case NodeKind::TableRow: return u"TableRow"_q;
	case NodeKind::TableCell: return u"TableCell"_q;
	case NodeKind::HtmlInline: return u"HtmlInline"_q;
	case NodeKind::HtmlBlock: return u"HtmlBlock"_q;
	case NodeKind::FootnoteReference: return u"FootnoteReference"_q;
	case NodeKind::FootnoteDefinition: return u"FootnoteDefinition"_q;
	case NodeKind::DisplayMath: return u"DisplayMath"_q;
	case NodeKind::InlineMath: return u"InlineMath"_q;
	case NodeKind::SoftBreak: return u"SoftBreak"_q;
	case NodeKind::LineBreak: return u"LineBreak"_q;
	case NodeKind::Unsupported: return u"Unsupported"_q;
	}
	return u"Unsupported"_q;
}

[[nodiscard]] QString MathKindName(MathKind kind) {
	switch (kind) {
	case MathKind::Inline: return u"Inline"_q;
	case MathKind::Display: return u"Display"_q;
	}
	return u"Inline"_q;
}

[[nodiscard]] QString ListKindName(ListKind kind) {
	switch (kind) {
	case ListKind::Bullet: return u"Bullet"_q;
	case ListKind::Ordered: return u"Ordered"_q;
	}
	return u"Bullet"_q;
}

[[nodiscard]] QString ListDelimiterName(ListDelimiter delimiter) {
	switch (delimiter) {
	case ListDelimiter::None: return u"None"_q;
	case ListDelimiter::Period: return u"Period"_q;
	case ListDelimiter::Parenthesis: return u"Parenthesis"_q;
	}
	return u"None"_q;
}

[[nodiscard]] QString HtmlBlockKindName(HtmlBlockKind kind) {
	switch (kind) {
	case HtmlBlockKind::None: return u"None"_q;
	case HtmlBlockKind::Comment: return u"Comment"_q;
	case HtmlBlockKind::Details: return u"Details"_q;
	case HtmlBlockKind::Unsupported: return u"Unsupported"_q;
	}
	return u"None"_q;
}

[[nodiscard]] QString TaskStateName(TaskState state) {
	switch (state) {
	case TaskState::None: return u"None"_q;
	case TaskState::Unchecked: return u"Unchecked"_q;
	case TaskState::Checked: return u"Checked"_q;
	}
	return u"None"_q;
}

[[nodiscard]] QString TableAlignmentName(TableAlignment alignment) {
	switch (alignment) {
	case TableAlignment::None: return u"None"_q;
	case TableAlignment::Left: return u"Left"_q;
	case TableAlignment::Center: return u"Center"_q;
	case TableAlignment::Right: return u"Right"_q;
	}
	return u"None"_q;
}

[[nodiscard]] QString RangeString(const SourceRange &range) {
	if (!range.available) {
		return u"unavailable"_q;
	}
	return u"%1:%2-%3:%4[%5,%6]"_q.arg(
		range.startLine
	).arg(
		range.startColumn
	).arg(
		range.endLine
	).arg(
		range.endColumn
	).arg(
		range.startOffset
	).arg(
		range.endOffset);
}

[[nodiscard]] QString EscapedValue(QString value) {
	value.replace(u"\\"_q, u"\\\\"_q);
	value.replace(u"\r"_q, u"\\r"_q);
	value.replace(u"\n"_q, u"\\n"_q);
	value.replace(u"\t"_q, u"\\t"_q);
	return value;
}

void AddRequiredStringAttribute(
		QString *line,
		const QString &name,
		const QString &value) {
	line->append(u" "_q);
	line->append(name);
	line->append(u"=\""_q);
	line->append(EscapedValue(value));
	line->append(u"\""_q);
}

void AddStringAttribute(
		QString *line,
		const QString &name,
		const QString &value) {
	if (value.isEmpty()) {
		return;
	}
	AddRequiredStringAttribute(line, name, value);
}

void AddIntAttribute(QString *line, const QString &name, int value) {
	line->append(u" "_q);
	line->append(name);
	line->append(u"="_q);
	line->append(QString::number(value));
}

void AddBoolAttribute(QString *line, const QString &name, bool value) {
	if (!value) {
		return;
	}
	line->append(u" "_q);
	line->append(name);
	line->append(u"="_q);
	line->append(BoolString(value));
}

[[nodiscard]] QString TableAlignmentsString(
		const std::vector<TableAlignment> &alignments) {
	auto names = QStringList();
	for (const auto alignment : alignments) {
		names.append(TableAlignmentName(alignment));
	}
	return names.join(u","_q);
}

void DumpNode(
		const MarkdownNode &node,
		int depth,
		QStringList *lines) {
	auto line = u"node"_q;
	AddIntAttribute(&line, u"depth"_q, depth);
	AddStringAttribute(&line, u"kind"_q, NodeKindName(node.kind));
	AddStringAttribute(&line, u"range"_q, RangeString(node.range));
	AddIntAttribute(
		&line,
		u"children"_q,
		static_cast<int>(node.children.size()));
	AddStringAttribute(&line, u"text"_q, node.text);
	AddStringAttribute(&line, u"url"_q, node.url);
	AddStringAttribute(&line, u"title"_q, node.title);
	AddStringAttribute(&line, u"info"_q, node.info);
	AddStringAttribute(&line, u"raw"_q, node.raw);
	AddStringAttribute(&line, u"anchorId"_q, node.anchorId);
	AddStringAttribute(&line, u"footnoteLabel"_q, node.footnoteLabel);
	AddStringAttribute(&line, u"detailsSummary"_q, node.detailsSummary);
	AddStringAttribute(&line, u"detailsBody"_q, node.detailsBody);
	AddStringAttribute(&line, u"unsupportedKind"_q, node.unsupportedKind);
	if (node.headingLevel != 0) {
		AddIntAttribute(&line, u"headingLevel"_q, node.headingLevel);
	}
	if (node.listStart != 0) {
		AddIntAttribute(&line, u"listStart"_q, node.listStart);
	}
	if (node.tableColumn != -1) {
		AddIntAttribute(&line, u"tableColumn"_q, node.tableColumn);
	}
	if (node.formulaIndex != -1) {
		AddIntAttribute(&line, u"formulaIndex"_q, node.formulaIndex);
	}
	if (node.footnoteOrdinal != 0) {
		AddIntAttribute(&line, u"footnoteOrdinal"_q, node.footnoteOrdinal);
	}
	if (node.kind == NodeKind::List) {
		AddStringAttribute(&line, u"listKind"_q, ListKindName(node.listKind));
		AddStringAttribute(
			&line,
			u"listDelimiter"_q,
			ListDelimiterName(node.listDelimiter));
	}
	if (node.htmlBlockKind != HtmlBlockKind::None) {
		AddStringAttribute(
			&line,
			u"htmlBlockKind"_q,
			HtmlBlockKindName(node.htmlBlockKind));
	}
	if (node.taskState != TaskState::None) {
		AddStringAttribute(&line, u"taskState"_q, TaskStateName(node.taskState));
	}
	AddBoolAttribute(&line, u"tight"_q, node.tight);
	AddBoolAttribute(&line, u"autolink"_q, node.autolink);
	AddBoolAttribute(&line, u"tableHeader"_q, node.tableHeader);
	AddBoolAttribute(&line, u"detailsOpen"_q, node.detailsOpen);
	if (!node.tableAlignments.empty()) {
		AddStringAttribute(
			&line,
			u"tableAlignments"_q,
			TableAlignmentsString(node.tableAlignments));
	}
	lines->append(line);
	for (const auto &child : node.children) {
		DumpNode(child, depth + 1, lines);
	}
}

} // namespace

PreparedDocument EmptyDocument(QString sourceName) {
	auto document = PreparedDocument();
	document.sourceName = std::move(sourceName);
	document.document.kind = NodeKind::Document;
	return document;
}

QString DumpForDebug(const PreparedDocument &document) {
	auto lines = QStringList();
	lines.append(u"sourceName=\"%1\""_q.arg(
		EscapedValue(document.sourceName)));
	lines.append(u"title=\"%1\""_q.arg(
		EscapedValue(document.title)));
	lines.append(u"sourceLength=%1"_q.arg(
		static_cast<qlonglong>(document.sourceText.size())));
	lines.append(u"empty=%1"_q.arg(BoolString(document.empty)));
	lines.append(u"cmarkNodeCount=%1"_q.arg(
		document.stats.cmarkNodeCount));
	lines.append(u"convertedNodeCount=%1"_q.arg(
		document.stats.convertedNodeCount));
	lines.append(u"maxDepth=%1"_q.arg(
		document.stats.maxDepth));
	lines.append(u"inlineFormulaCount=%1"_q.arg(
		document.stats.inlineFormulaCount));
	lines.append(u"displayFormulaCount=%1"_q.arg(
		document.stats.displayFormulaCount));
	lines.append(u"tablesSeen=%1"_q.arg(
		BoolString(document.stats.tablesSeen)));
	lines.append(u"taskListsSeen=%1"_q.arg(
		BoolString(document.stats.taskListsSeen)));
	lines.append(u"strikethroughSeen=%1"_q.arg(
		BoolString(document.stats.strikethroughSeen)));
	lines.append(u"autolinksSeen=%1"_q.arg(
		BoolString(document.stats.autolinksSeen)));
	lines.append(u"footnotesSeen=%1"_q.arg(
		BoolString(document.stats.footnotesSeen)));
	lines.append(u"warnings=%1"_q.arg(
		static_cast<qlonglong>(document.warnings.size())));
	for (auto i = qsizetype(0); i != document.warnings.size(); ++i) {
		lines.append(u"warning index=%1 text=\"%2\""_q.arg(
			i + 1
		).arg(
			EscapedValue(document.warnings[i])));
	}
	lines.append(u"nodes=preorder"_q);
	DumpNode(document.document, 0, &lines);
	lines.append(u"formulas=%1"_q.arg(
		static_cast<qlonglong>(document.formulas.size())));
	for (const auto &formula : document.formulas) {
		auto line = u"formula"_q;
		AddIntAttribute(&line, u"index"_q, formula.index);
		AddStringAttribute(&line, u"kind"_q, MathKindName(formula.kind));
		AddStringAttribute(&line, u"range"_q, RangeString(formula.range));
		AddRequiredStringAttribute(&line, u"parent"_q, formula.parentNodeKind);
		AddRequiredStringAttribute(&line, u"tex"_q, formula.tex);
		lines.append(line);
	}
	return lines.join(u"\n"_q);
}

} // namespace Iv::Markdown
