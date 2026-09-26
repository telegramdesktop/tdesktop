/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_common.h"

#include <QtCore/QMargins>
#include <QtCore/QSize>
#include <QtCore/QStringList>

#include <map>
#include <memory>
#include <vector>

namespace Iv::Markdown {

enum class NodeKind {
	Document,
	Paragraph,
	Heading,
	Text,
	Emphasis,
	Strong,
	Strike,
	InlineCode,
	CodeBlock,
	Link,
	List,
	ListItem,
	Blockquote,
	ThematicBreak,
	Table,
	TableRow,
	TableCell,
	HtmlInline,
	HtmlBlock,
	FootnoteReference,
	FootnoteDefinition,
	DisplayMath,
	InlineMath,
	SoftBreak,
	LineBreak,
	Unsupported,
};

enum class MathKind {
	Inline,
	Display,
};

enum class ListKind {
	Bullet,
	Ordered,
};

enum class ListDelimiter {
	None,
	Period,
	Parenthesis,
};

enum class TaskState {
	None,
	Unchecked,
	Checked,
};

enum class TableAlignment {
	None,
	Left,
	Center,
	Right,
};

enum class HtmlBlockKind {
	None,
	Comment,
	Details,
	Unsupported,
};

struct SourceRange {
	bool available = false;
	int startLine = 0;
	int startColumn = 0;
	int endLine = 0;
	int endColumn = 0;
	int startOffset = 0;
	int endOffset = 0;
};

struct MarkdownNode {
	NodeKind kind = NodeKind::Unsupported;
	SourceRange range;
	QString text;
	QString url;
	QString title;
	QString info;
	QString raw;
	QString anchorId;
	QString footnoteLabel;
	QString detailsSummary;
	QString detailsBody;
	QString unsupportedKind;
	std::vector<MarkdownNode> children;
	std::vector<TableAlignment> tableAlignments;
	int headingLevel = 0;
	int listStart = 0;
	int tableColumn = -1;
	int formulaIndex = -1;
	int footnoteOrdinal = 0;
	ListKind listKind = ListKind::Bullet;
	ListDelimiter listDelimiter = ListDelimiter::None;
	TaskState taskState = TaskState::None;
	HtmlBlockKind htmlBlockKind = HtmlBlockKind::None;
	bool tight = false;
	bool autolink = false;
	bool tableHeader = false;
	bool detailsOpen = false;
};

struct MathFormula {
	int index = 0;
	MathKind kind = MathKind::Inline;
	QString tex;
	SourceRange range;
	QString parentNodeKind;
};

struct ParseStats {
	int cmarkNodeCount = 0;
	int convertedNodeCount = 0;
	int maxDepth = 0;
	int inlineFormulaCount = 0;
	int displayFormulaCount = 0;
	bool tablesSeen = false;
	bool taskListsSeen = false;
	bool strikethroughSeen = false;
	bool autolinksSeen = false;
	bool footnotesSeen = false;
};

inline constexpr auto kFormulaExactMetricScale = 64;

struct FormulaExactMetrics {
	QSize scaledSize;
	int scaledAscent = 0;
	QMargins scaledInsets;
};

struct MeasuredFormula {
	QSize logicalSize;
	int logicalDepth = 0;
	FormulaExactMetrics exact;
	QString fallbackText;
	QString error;
	bool success = false;
	bool overflow = false;
	bool tooLarge = false;
};

struct PreparedFormulaMeasurementSignature {
	QString trimmedTex;
	MathKind kind = MathKind::Display;
	int textSize = 0;
	int renderWidthCap = 0;
	int renderHeightCap = 0;
};

inline bool operator==(
		const PreparedFormulaMeasurementSignature &a,
		const PreparedFormulaMeasurementSignature &b) {
	return a.trimmedTex == b.trimmedTex
		&& a.kind == b.kind
		&& a.textSize == b.textSize
		&& a.renderWidthCap == b.renderWidthCap
		&& a.renderHeightCap == b.renderHeightCap;
}

struct PreparedFormulaMeasurementCacheEntry {
	PreparedFormulaMeasurementSignature signature;
	std::shared_ptr<const MeasuredFormula> data;
};

struct PreparedFormulaMeasurementSignatureLess {
	[[nodiscard]] bool operator()(
		const PreparedFormulaMeasurementSignature &a,
		const PreparedFormulaMeasurementSignature &b) const;
};

inline bool PreparedFormulaMeasurementSignatureLess::operator()(
		const PreparedFormulaMeasurementSignature &a,
		const PreparedFormulaMeasurementSignature &b) const {
	if (a.trimmedTex != b.trimmedTex) {
		return a.trimmedTex < b.trimmedTex;
	} else if (a.kind != b.kind) {
		return (int(a.kind) < int(b.kind));
	} else if (a.textSize != b.textSize) {
		return a.textSize < b.textSize;
	} else if (a.renderWidthCap != b.renderWidthCap) {
		return a.renderWidthCap < b.renderWidthCap;
	}
	return a.renderHeightCap < b.renderHeightCap;
}

struct PreparedFormulaMeasurementCacheState {
	std::vector<PreparedFormulaMeasurementCacheEntry> slots;
	std::map<
		PreparedFormulaMeasurementSignature,
		std::shared_ptr<const MeasuredFormula>,
		PreparedFormulaMeasurementSignatureLess> bySignature;
};

struct PreparedDocument {
	QString sourceName;
	QString title;
	QString sourceText;
	MarkdownNode document;
	std::vector<MathFormula> formulas;
	ParseStats stats;
	QStringList warnings;
	bool empty = true;
	std::shared_ptr<PreparedFormulaMeasurementCacheState> formulaMeasurementCache
		= std::make_shared<PreparedFormulaMeasurementCacheState>();

	PreparedDocument() = default;

	PreparedDocument(const PreparedDocument &other);

	PreparedDocument &operator=(const PreparedDocument &other);

	PreparedDocument(PreparedDocument &&) noexcept = default;
	PreparedDocument &operator=(PreparedDocument &&) noexcept = default;
};

inline PreparedDocument::PreparedDocument(const PreparedDocument &other)
	: sourceName(other.sourceName)
	, title(other.title)
	, sourceText(other.sourceText)
	, document(other.document)
	, formulas(other.formulas)
	, stats(other.stats)
	, warnings(other.warnings)
	, empty(other.empty)
	, formulaMeasurementCache(
		std::make_shared<PreparedFormulaMeasurementCacheState>()) {
}

inline PreparedDocument &PreparedDocument::operator=(
		const PreparedDocument &other) {
	if (this != &other) {
		sourceName = other.sourceName;
		title = other.title;
		sourceText = other.sourceText;
		document = other.document;
		formulas = other.formulas;
		stats = other.stats;
		warnings = other.warnings;
		empty = other.empty;
		formulaMeasurementCache = std::make_shared<
			PreparedFormulaMeasurementCacheState>();
	}
	return *this;
}

struct ParseResult {
	PreparedDocument document;
	QString error;
	bool ok = true;
};

[[nodiscard]] PreparedDocument EmptyDocument(QString sourceName = QString());
[[nodiscard]] QString DumpForDebug(const PreparedDocument &document);

} // namespace Iv::Markdown
