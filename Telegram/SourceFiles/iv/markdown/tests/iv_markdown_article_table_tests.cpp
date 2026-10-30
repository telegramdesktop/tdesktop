/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_tests.h"

#include "iv/markdown/iv_markdown_article_selection.h"

namespace Iv::Markdown::Tests {
namespace {

constexpr auto kTableSegment = 2;
const auto kTableText = u"Caption\nA\t\n\t\nB\tC"_q;

struct SegmentText {
	int segment = 0;
	std::optional<TextSelection> text;
};

struct TableCase {
	MarkdownArticleSelection selection;
	std::optional<MarkdownArticleSelectionEndpoints> ends;
	PreparedEditSelection structural;
	bool whole = false;
	std::vector<SegmentText> ranges;
	QString copy;
};

void AddCell(
		LaidOutTableRow &row,
		const QString &text,
		int column,
		int span = 1) {
	auto &cell = row.cells.emplace_back();
	SetLeaf(cell.leaf, text);
	cell.column = column;
	cell.colspan = span;
	cell.rowspan = span;
}

[[nodiscard]] Article TableArticle() {
	auto result = Article();
	auto &blocks = result.blocks;
	blocks.resize(3);
	SetLeaf(blocks[0].leaf, u"Before"_q);
	blocks[0].textRect = QRect(0, 0, 100, 20);
	auto &table = blocks[1];
	table.kind = PreparedBlockKind::Table;
	table.editBlock = PreparedEditBlockSource{ .path = RootPath(1) };
	table.tableColumnWidths = { 40, 40 };
	SetLeaf(table.leaf, u"Caption"_q);
	table.textRect = QRect(0, 30, 100, 20);
	table.tableRows.resize(3);
	AddCell(table.tableRows[0], u"A"_q, 0, 2);
	AddCell(table.tableRows[2], u"B"_q, 0);
	AddCell(table.tableRows[2], u"C"_q, 1);
	SetLeaf(blocks[2].leaf, u"After"_q);
	blocks[2].textRect = QRect(0, 200, 100, 20);
	CollectSelectableSegments(&result.blocks, &result.segments);
	return result;
}

[[nodiscard]] MarkdownArticleSelectionEndpoints Ends(int from, int to) {
	return { { from, true }, { to, true } };
}

[[nodiscard]] QString FullCopy() {
	const auto article = TableArticle();
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(article.segments.size()), 7)) {
		return expect.result();
	}
	auto lengths = std::vector<int>();
	auto parents = std::vector<int>();
	auto tables = std::vector<int>();
	for (const auto &segment : article.segments) {
		lengths.push_back(segment.length);
		parents.push_back(segment.parentSegmentIndex);
		tables.push_back(segment.tableSegmentIndex);
	}
	expect.numbers(u"lengths"_q, lengths, { 6, 7, 1, 1, 1, 1, 5 });
	expect.numbers(u"parents"_q, parents, { -1, 2, -1, -1, -1, -1, -1 });
	expect.numbers(u"tables"_q, tables, { -1, -1, -1, 2, 2, 2, -1 });
	expect.copy(
		u"table"_q,
		TextForSegment(article.segments[kTableSegment]),
		kTableText,
		{},
		{});
	return expect.result();
}

void ExpectTableCase(
		Expect &expect,
		const QString &what,
		const Article &article,
		const TableCase &entry) {
	const auto &segments = article.segments;
	const auto ends = entry.ends ? &*entry.ends : nullptr;
	const auto state = PaintSelectionState{
		.segments = &segments,
		.selection = entry.selection,
		.endpoints = ends,
		.structuralSelection = &entry.structural,
	};
	expect.flag(
		what + u" whole"_q,
		TableSegmentSelected(state, kTableSegment),
		entry.whole);
	for (const auto &range : entry.ranges) {
		expect.selection(
			what + u" segment %1"_q.arg(range.segment),
			TextSelectionForSegment(segments[range.segment], state),
			range.text);
	}
	expect.copy(
		what + u" copy"_q,
		TextForSelectedSegments(
			segments,
			entry.selection,
			ends,
			&entry.structural),
		entry.copy,
		{},
		{});
}

[[nodiscard]] Fn<QString()> TableCheck(std::vector<TableCase> cases) {
	return [=] {
		const auto article = TableArticle();
		auto expect = Expect();
		if (!expect.number(u"segments"_q, int(article.segments.size()), 7)) {
			return expect.result();
		}
		for (auto i = 0; i != int(cases.size()); ++i) {
			ExpectTableCase(expect, u"case %1"_q.arg(i), article, cases[i]);
		}
		return expect.result();
	};
}

[[nodiscard]] QString CaptionEdgeCases() {
	auto article = Article();
	auto &blocks = article.blocks;
	blocks.resize(2);
	blocks[0].kind = PreparedBlockKind::Table;
	blocks[0].tableColumnWidths = { 10, 10 };
	auto &row = blocks[0].tableRows.emplace_back();
	AddCell(row, u"L"_q, 0);
	AddCell(row, u"R"_q, 1);
	blocks[1].kind = PreparedBlockKind::Table;
	blocks[1].tableColumnWidths = { 10 };
	SetLeaf(blocks[1].leaf, u"Only"_q);
	CollectSelectableSegments(&article.blocks, &article.segments);
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(article.segments.size()), 4)) {
		return expect.result();
	}
	expect.copy(
		u"without caption"_q,
		TextForSegment(article.segments[0]),
		u"L\tR"_q,
		{},
		{});
	expect.copy(
		u"without rows"_q,
		TextForSegment(article.segments[3]),
		u"Only"_q,
		{},
		{});
	return expect.result();
}

[[nodiscard]] QString PhotoCaptionOnce() {
	auto article = Article();
	auto &photo = article.blocks.emplace_back();
	photo.kind = PreparedBlockKind::Photo;
	photo.mediaRect = QRect(0, 0, 100, 100);
	SetLeaf(photo.leaf, u"Cap"_q);
	photo.textRect = QRect(0, 100, 100, 20);
	CollectSelectableSegments(&article.blocks, &article.segments);
	const auto &segments = article.segments;
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(segments.size()), 2)) {
		return expect.result();
	}
	expect.number(
		u"photo kind"_q,
		int(segments[0].kind),
		int(SelectableSegmentKind::Photo));
	expect.number(u"caption parent"_q, segments[1].parentSegmentIndex, 0);
	expect.copy(
		u"photo and caption"_q,
		TextForSelectedSegments(segments, { { 0, 0 }, { 1, 3 } }, nullptr),
		u"Photo\nCap"_q,
		{},
		{});
	expect.copy(
		u"caption only"_q,
		TextForSelectedSegments(segments, { { 1, 0 }, { 1, 3 } }, nullptr),
		u"Cap"_q,
		{},
		{});
	return expect.result();
}

} // namespace

std::vector<Check> TableChecks() {
	return {
		{ u"table.full-copy"_q, FullCopy },
		{
			u"table.direct-cell-leaving-table"_q,
			TableCheck({ {
				.selection = { { 3, 0 }, { 6, 2 } },
				.ends = Ends(3, 6),
				.ranges = {
					{ 3, TextSelection(0, 1) },
					{ 4, TextSelection(0, 1) },
					{ 5, TextSelection(0, 1) },
					{ 6, TextSelection(0, 2) },
				},
				.copy = u"A\nB\nC\nAf"_q,
			} }),
		},
		{
			u"table.two-cells-whole-table"_q,
			TableCheck({ {
				.selection = { { 3, 0 }, { 5, 1 } },
				.ends = Ends(3, 5),
				.whole = true,
				.copy = kTableText,
			} }),
		},
		{
			u"table.single-cell-direct"_q,
			TableCheck({ {
				.selection = { { 4, 0 }, { 4, 1 } },
				.ends = Ends(4, 4),
				.ranges = {
					{ 3, std::nullopt },
					{ 4, TextSelection(0, 1) },
					{ 5, std::nullopt },
				},
				.copy = u"B"_q,
			} }),
		},
		{
			u"table.single-cell-no-endpoints"_q,
			TableCheck({ {
				.selection = { { 4, 0 }, { 4, 1 } },
				.copy = u"B"_q,
			} }),
		},
		{
			u"table.whole-from-outside"_q,
			TableCheck({ {
				.selection = { { 0, 0 }, { 6, 5 } },
				.ends = Ends(0, 6),
				.whole = true,
				.copy = u"Before\nCaption\nA\t\n\t\nB\tC\nAfter"_q,
			} }),
		},
		{
			u"table.into-cell-from-outside"_q,
			TableCheck({ {
				.selection = { { 0, 2 }, { 3, 1 } },
				.ends = Ends(0, 3),
				.copy = u"fore\nCaption\nA"_q,
			} }),
		},
		{
			u"table.table-segment-range"_q,
			TableCheck({ {
				.selection = { { 2, 0 }, { 2, 1 } },
				.ends = Ends(2, 2),
				.whole = true,
				.copy = kTableText,
			} }),
		},
		{
			u"table.structural-blocks"_q,
			TableCheck({ {
				.structural = BlocksSelection(1, 2),
				.whole = true,
				.copy = kTableText,
			} }),
		},
		{
			u"table.rows-0-2"_q,
			TableCheck({ {
				.structural = TableRowsSelection(1, 0, 2),
				.copy = u"A\t\n\t"_q,
			} }),
		},
		{
			u"table.rows-1-4"_q,
			TableCheck({ {
				.structural = TableRowsSelection(1, 1, 4),
				.copy = u"A\t\nB\tC"_q,
			} }),
		},
		{
			u"table.cells-interior"_q,
			TableCheck({ {
				.structural = TableCellsSelection(1, 1, 2, 1, 2),
				.copy = u"A"_q,
			} }),
		},
		{
			u"table.cells-column-1"_q,
			TableCheck({ {
				.structural = TableCellsSelection(1, 0, 3, 1, 2),
				.copy = u"A\n\nC"_q,
			} }),
		},
		{
			u"table.structural-invalid"_q,
			TableCheck({
				{ .structural = TableCellsSelection(1, 2, 2, 0, 2) },
				{ .structural = TableRowsSelection(7, 0, 2) },
			}),
		},
		{ u"table.caption-edge-cases"_q, CaptionEdgeCases },
		{ u"media.photo-caption-once"_q, PhotoCaptionOnce },
	};
}

} // namespace Iv::Markdown::Tests
