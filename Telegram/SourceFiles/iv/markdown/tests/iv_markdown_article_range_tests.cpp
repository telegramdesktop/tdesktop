/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_tests.h"

#include "iv/markdown/iv_markdown_article_selection.h"

#include <array>

namespace Iv::Markdown::Tests {
namespace {

constexpr auto kMathSegment = 2;

struct SegmentRanges {
	int segment = 0;
	std::optional<TextSelection> text;
	std::optional<TextSelection> paint;
};

struct RangeCase {
	MarkdownArticleSelection selection;
	PreparedEditSelection structural;
	std::vector<SegmentRanges> ranges;
	bool math = false;
	QString copy;
	TextForMimeDataTags tags;
};

struct StructuralKindRow {
	PreparedEditSelectionKind kind = PreparedEditSelectionKind::None;
	bool block = false;
	bool listItem = false;
	bool row = false;
	bool cell = false;
};

struct CellCase {
	PreparedEditTableCellSource cell;
	PreparedEditSelection selection;
	bool selected = false;
};

[[nodiscard]] Article RangeArticle() {
	auto result = Article();
	auto &blocks = result.blocks;
	blocks.resize(4);
	SetLeaf(blocks[0].leaf, u"Hello world"_q);
	blocks[0].editBlock = PreparedEditBlockSource{ .path = RootPath(0) };
	SetLeaf(blocks[1].leaf, u"Second"_q);
	blocks[1].editBlock = PreparedEditBlockSource{ .path = RootPath(1) };
	blocks[2].kind = PreparedBlockKind::DisplayMath;
	blocks[2].copyText = u"x"_q;
	blocks[2].editBlock = PreparedEditBlockSource{ .path = ChildPath(1, 0) };
	SetLeaf(blocks[3].leaf, u"Item"_q);
	blocks[3].editLeaf = PreparedEditLeafSource{
		.kind = PreparedEditLeafKind::ListItemText,
		.block = RootPath(3),
		.listItemIndex = 1,
	};
	CollectSelectableSegments(&result.blocks, &result.segments);
	return result;
}

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wswitch"
[[nodiscard]] int EditSelectionKindRowIndex(PreparedEditSelectionKind kind) {
	using Kind = PreparedEditSelectionKind;
	switch (kind) {
	case Kind::None: return 0;
	case Kind::Blocks: return 1;
	case Kind::ListItems: return 2;
	case Kind::TableRows: return 3;
	case Kind::TableCells: return 4;
	}
	return -1;
}
#pragma clang diagnostic pop

[[nodiscard]] PreparedEditSelection ListItemsSelection(int from, int till) {
	return {
		.kind = PreparedEditSelectionKind::ListItems,
		.listItems = { .block = RootPath(3), .from = from, .till = till },
	};
}

[[nodiscard]] PreparedEditTableCellSource Cell(
		int row,
		int column,
		int colspan,
		int rowspan) {
	return {
		.block = RootPath(4),
		.tableRowIndex = row,
		.tableCellIndex = 0,
		.column = column,
		.colspan = colspan,
		.rowspan = rowspan,
	};
}

[[nodiscard]] PreparedEditTableCellSource SpanningCell() {
	return Cell(1, 2, 2, 2);
}

[[nodiscard]] TextForMimeDataTags MathTag(int offset) {
	return { { .offset = offset, .length = 1, .id = u"iv-math"_q } };
}

[[nodiscard]] QString ComparePositions() {
	auto expect = Expect();
	expect.number(
		u"{1,5} {1,5}"_q,
		CompareSelectionPositions({ 1, 5 }, { 1, 5 }),
		0);
	expect.number(
		u"{1,2} {1,5}"_q,
		CompareSelectionPositions({ 1, 2 }, { 1, 5 }),
		-1);
	expect.number(
		u"{1,5} {1,2}"_q,
		CompareSelectionPositions({ 1, 5 }, { 1, 2 }),
		1);
	expect.number(
		u"{0,9} {1,0}"_q,
		CompareSelectionPositions({ 0, 9 }, { 1, 0 }),
		-1);
	expect.number(
		u"{2,0} {1,9}"_q,
		CompareSelectionPositions({ 2, 0 }, { 1, 9 }),
		1);
	return expect.result();
}

[[nodiscard]] Fn<QString()> NormalizeCheck(
		MarkdownArticleSelection selection,
		MarkdownArticleSelection expected) {
	return [=] {
		auto expect = Expect();
		expect.position(
			u"normalized"_q,
			NormalizeSelection(selection),
			expected);
		return expect.result();
	};
}

void ExpectRangeCase(
		Expect &expect,
		const QString &what,
		const Article &article,
		const RangeCase &entry) {
	const auto &segments = article.segments;
	const auto state = PaintSelectionState{
		.segments = &segments,
		.selection = entry.selection,
		.structuralSelection = &entry.structural,
	};
	for (const auto &range : entry.ranges) {
		const auto &segment = segments[range.segment];
		const auto prefix = what + u" segment %1 "_q.arg(range.segment);
		expect.selection(
			prefix + u"text"_q,
			TextSelectionForSegment(segment, state),
			range.text);
		expect.selection(
			prefix + u"paint"_q,
			PaintTextSelectionForSegment(segment, state),
			range.paint);
	}
	expect.flag(
		what + u" whole math"_q,
		WholeSegmentSelected(state, kMathSegment),
		entry.math);
	expect.copy(
		what + u" copy"_q,
		TextForSelectedSegments(
			segments,
			entry.selection,
			nullptr,
			&entry.structural),
		entry.copy,
		{},
		entry.tags);
}

[[nodiscard]] Fn<QString()> RangeCheck(std::vector<RangeCase> cases) {
	return [=] {
		const auto article = RangeArticle();
		auto expect = Expect();
		if (!expect.number(u"segments"_q, int(article.segments.size()), 4)) {
			return expect.result();
		}
		for (auto i = 0; i != int(cases.size()); ++i) {
			ExpectRangeCase(expect, u"case %1"_q.arg(i), article, cases[i]);
		}
		return expect.result();
	};
}

[[nodiscard]] QString StructuralKindMatrix() {
	static_assert(int(PreparedEditSelectionKind::TableCells) == 4);
	using Kind = PreparedEditSelectionKind;
	const auto rows = std::array{
		StructuralKindRow{ .kind = Kind::None },
		StructuralKindRow{ .kind = Kind::Blocks, .block = true },
		StructuralKindRow{ .kind = Kind::ListItems, .listItem = true },
		StructuralKindRow{ .kind = Kind::TableRows, .row = true },
		StructuralKindRow{ .kind = Kind::TableCells, .cell = true },
	};
	auto selection = PreparedEditSelection{
		.blocks = BlocksSelection(0, 2).blocks,
		.listItems = ListItemsSelection(1, 3).listItems,
		.tableRows = TableRowsSelection(4, 0, 2).tableRows,
		.tableCells = TableCellsSelection(4, 2, 3, 3, 4).tableCells,
	};
	const auto state = PaintSelectionState{ .structuralSelection = &selection };
	const auto block = PreparedEditBlockSource{ RootPath(1) };
	const auto listItem = PreparedEditListItemSource{ RootPath(3), 2 };
	const auto row = PreparedEditTableRowSource{ RootPath(4), 1 };
	const auto cell = SpanningCell();
	auto expect = Expect();
	expect.number(u"rows"_q, int(rows.size()), 5);
	for (auto i = 0; i != int(rows.size()); ++i) {
		const auto &expected = rows[i];
		const auto what = u"kind %1 "_q.arg(i);
		expect.number(what + u"order"_q, int(expected.kind), i);
		expect.number(
			what + u"declared row"_q,
			EditSelectionKindRowIndex(expected.kind),
			i);
		selection.kind = expected.kind;
		expect.flag(
			what + u"block"_q,
			StructuralBlockSelected(state, block),
			expected.block);
		expect.flag(
			what + u"list item"_q,
			StructuralListItemSelected(state, listItem),
			expected.listItem);
		expect.flag(
			what + u"row"_q,
			StructuralTableRowSelected(state, row),
			expected.row);
		expect.flag(
			what + u"cell"_q,
			StructuralTableCellSelected(state, cell),
			expected.cell);
	}
	const auto past = int(rows.size());
	expect.number(
		u"kind %1 past inventory"_q.arg(past),
		EditSelectionKindRowIndex(PreparedEditSelectionKind(past)),
		-1);
	return expect.result();
}

[[nodiscard]] QString StructuralBounds() {
	const auto article = RangeArticle();
	const auto blocks = BlocksSelection(0, 2);
	const auto items = ListItemsSelection(1, 3);
	const auto rows = TableRowsSelection(4, 0, 2);
	const auto state = [&](const PreparedEditSelection &selection) {
		return PaintSelectionState{
			.segments = &article.segments,
			.structuralSelection = &selection,
		};
	};
	auto expect = Expect();
	expect.flag(
		u"block 0"_q,
		StructuralBlockSelected(state(blocks), { RootPath(0) }),
		true);
	expect.flag(
		u"block 2"_q,
		StructuralBlockSelected(state(blocks), { RootPath(2) }),
		false);
	expect.flag(
		u"nested block"_q,
		StructuralBlockSelected(state(blocks), { ChildPath(1, 0) }),
		false);
	expect.flag(
		u"nested segment"_q,
		WholeSegmentSelected(state(blocks), kMathSegment),
		true);
	expect.flag(
		u"item 1"_q,
		StructuralListItemSelected(state(items), { RootPath(3), 1 }),
		true);
	expect.flag(
		u"item 3"_q,
		StructuralListItemSelected(state(items), { RootPath(3), 3 }),
		false);
	expect.flag(
		u"other list"_q,
		StructuralListItemSelected(state(items), { RootPath(4), 1 }),
		false);
	expect.flag(
		u"row 0"_q,
		StructuralTableRowSelected(state(rows), { RootPath(4), 0 }),
		true);
	expect.flag(
		u"row 2"_q,
		StructuralTableRowSelected(state(rows), { RootPath(4), 2 }),
		false);
	expect.flag(
		u"other table"_q,
		StructuralTableRowSelected(state(rows), { RootPath(5), 0 }),
		false);
	return expect.result();
}

[[nodiscard]] Fn<QString()> CellCheck(std::vector<CellCase> cases) {
	return [=] {
		auto expect = Expect();
		for (auto i = 0; i != int(cases.size()); ++i) {
			const auto &entry = cases[i];
			const auto state = PaintSelectionState{
				.structuralSelection = &entry.selection,
			};
			expect.flag(
				u"case %1"_q.arg(i),
				StructuralTableCellSelected(state, entry.cell),
				entry.selected);
		}
		return expect.result();
	};
}

} // namespace

std::vector<Check> RangeChecks() {
	const auto all = AllTextSelection;
	const auto wide = TableCellsSelection(4, 0, 4, 0, 5);
	const auto cross = std::vector<SegmentRanges>{
		{ 0, TextSelection(3, 11), TextSelection(3, 11) },
		{ 1, TextSelection(0, 2), TextSelection(0, 2) },
	};
	return {
		{ u"range.compare-positions"_q, ComparePositions },
		{
			u"range.normalize-empty"_q,
			NormalizeCheck({ { 2, 2 }, { 2, 2 } }, { { -1, 0 }, { -1, 0 } }),
		},
		{
			u"range.normalize-invalid"_q,
			NormalizeCheck({ { -1, 0 }, { 2, 3 } }, { { -1, 0 }, { -1, 0 } }),
		},
		{
			u"range.normalize-forward"_q,
			NormalizeCheck({ { 1, 2 }, { 3, 4 } }, { { 1, 2 }, { 3, 4 } }),
		},
		{
			u"range.normalize-reversed-same-segment"_q,
			NormalizeCheck({ { 1, 7 }, { 1, 2 } }, { { 1, 2 }, { 1, 7 } }),
		},
		{
			u"range.normalize-reversed-cross-segment"_q,
			NormalizeCheck({ { 3, 1 }, { 1, 4 } }, { { 1, 4 }, { 3, 1 } }),
		},
		{
			u"range.text-selection-cross"_q,
			RangeCheck({ {
				.selection = { { 0, 3 }, { 1, 2 } },
				.ranges = cross,
				.copy = u"lo world\nSe"_q,
			} }),
		},
		{
			u"range.text-selection-reversed-copy"_q,
			RangeCheck({ {
				.selection = { { 1, 2 }, { 0, 3 } },
				.ranges = cross,
				.copy = u"lo world\nSe"_q,
			} }),
		},
		{
			u"range.text-selection-clipped"_q,
			RangeCheck({ {
				.selection = { { 0, -5 }, { 0, 50 } },
				.ranges = {
					{ 0, TextSelection(0, 11), TextSelection(0, 11) },
					{ 1, std::nullopt, std::nullopt },
				},
				.copy = u"Hello world"_q,
			} }),
		},
		{
			u"range.text-selection-beyond-length"_q,
			RangeCheck({ {
				.selection = { { 0, 20 }, { 0, 30 } },
				.ranges = { { 0, std::nullopt, std::nullopt } },
			} }),
		},
		{
			u"range.non-text-segment"_q,
			RangeCheck({
				{
					.selection = { { 1, 4 }, { 2, 1 } },
					.ranges = { { 2, std::nullopt, std::nullopt } },
					.math = true,
					.copy = u"nd\nx"_q,
					.tags = MathTag(3),
				},
				{
					.selection = { { 1, 4 }, { 2, 0 } },
					.copy = u"nd"_q,
				},
			}),
		},
		{
			u"range.structural-copy-vs-paint"_q,
			RangeCheck({ {
				.structural = BlocksSelection(1, 2),
				.ranges = {
					{ 0, std::nullopt, std::nullopt },
					{ 1, all, std::nullopt },
					{ 3, std::nullopt, std::nullopt },
				},
				.math = true,
				.copy = u"Second\nx"_q,
				.tags = MathTag(7),
			} }),
		},
		{
			u"range.structural-list-items"_q,
			RangeCheck({
				{
					.structural = ListItemsSelection(1, 2),
					.ranges = { { 3, all, std::nullopt } },
					.copy = u"Item"_q,
				},
				{
					.structural = ListItemsSelection(2, 3),
					.ranges = { { 3, std::nullopt, std::nullopt } },
				},
			}),
		},
		{
			u"range.structural-table-kinds-use-range"_q,
			RangeCheck({ {
				.selection = { { 0, 0 }, { 0, 5 } },
				.structural = TableRowsSelection(0, 0, 1),
				.ranges = {
					{ 0, TextSelection(0, 5), TextSelection(0, 5) },
				},
			} }),
		},
		{ u"range.structural-kind-matrix"_q, StructuralKindMatrix },
		{ u"range.structural-bounds"_q, StructuralBounds },
		{
			u"range.spanning-cell-overlap"_q,
			CellCheck({
				{ SpanningCell(), TableCellsSelection(4, 2, 3, 3, 4), true },
				{ SpanningCell(), TableCellsSelection(4, 3, 4, 2, 4), false },
				{ SpanningCell(), TableCellsSelection(4, 1, 3, 4, 5), false },
				{ SpanningCell(), TableCellsSelection(4, 0, 2, 0, 3), true },
			}),
		},
		{
			u"range.spanning-cell-invalid"_q,
			CellCheck({
				{ SpanningCell(), wide, true },
				{ Cell(1, 2, 0, 2), wide, false },
				{ Cell(1, 2, 2, 0), wide, false },
				{ Cell(1, -1, 2, 2), wide, false },
				{ Cell(-1, 2, 2, 2), wide, false },
				{ SpanningCell(), TableCellsSelection(4, 3, 1, 2, 4), false },
				{ SpanningCell(), TableCellsSelection(5, 1, 3, 2, 4), false },
			}),
		},
	};
}

} // namespace Iv::Markdown::Tests
