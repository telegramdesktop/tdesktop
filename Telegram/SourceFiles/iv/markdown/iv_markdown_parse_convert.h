/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_math.h"
#include "iv/markdown/iv_markdown_parse.h"

#include <cmark-gfm.h>

#include <memory>
#include <vector>

namespace Iv::Markdown {

struct ParserDeleter {
	void operator()(cmark_parser *parser) const;
};

struct NodeDeleter {
	void operator()(cmark_node *node) const;
};

using ParserPointer = std::unique_ptr<cmark_parser, ParserDeleter>;
using NodePointer = std::unique_ptr<cmark_node, NodeDeleter>;

struct ParserState {
	const QByteArray &normalizedSource;
	const std::vector<int> &lineStarts;
	std::vector<bool> *mask = nullptr;
	std::vector<MathScanBlock> *scanBlocks = nullptr;
	ParseStats *stats = nullptr;
	QStringList *warnings = nullptr;
	QString error;
	bool failed = false;
};

struct ParsedDetailsBlock {
	QString summary;
	QString body;
	bool open = false;
	bool ok = false;
};

struct BudgetedParseResult {
	cmark_node *root = nullptr;
	bool overMemoryLimit = false;
};

// Runs the whole cmark parse (parser construction, extension attach,
// feed and finish) under a memory budget. cmark allocates a couple of
// hundred bytes of heap per node and several markdown constructs create
// multiple nodes per input byte, so without a budget a hostile source of
// a few megabytes transiently allocates gigabytes on the main thread
// before any Telegram-side limit is consulted. When cmark heap usage
// crosses the limit, the parser stops at its next abort checkpoint,
// frees everything through normal cleanup paths and finish() returns
// NULL. The returned root (on success) stays owned by the budgeted
// allocator until it is freed with cmark_node_free().
[[nodiscard]] BudgetedParseResult RunBudgetedCmarkParse(
	const QByteArray &source,
	int parserOptions);

[[nodiscard]] bool CollectScanMetadata(
	cmark_node *node,
	ParserState *state,
	int depth);
[[nodiscard]] QString NormalizeParsedFragmentId(QString fragment);
[[nodiscard]] bool ConvertNode(
	cmark_node *node,
	ParserState *state,
	int depth,
	MarkdownNode *out);

} // namespace Iv::Markdown
