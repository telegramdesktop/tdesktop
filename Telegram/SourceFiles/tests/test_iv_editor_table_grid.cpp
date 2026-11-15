/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_page_table_grid.h"

#include <cstdio>
#include <initializer_list>
#include <limits>
#include <vector>

namespace {

using namespace Iv::Editor;
using Iv::RichPage;
using Iv::Markdown::MarkdownPrepareTableRenderLimits;
using Iv::Markdown::PreparedEditTableCellRange;

constexpr auto kIntMin = std::numeric_limits<int>::min();
constexpr auto kIntMax = std::numeric_limits<int>::max();

constexpr auto kEmpty = "rows=0 columns=0 cells=[] occupancy=[]";
constexpr auto kBlankTwo = "rows=2 columns=0 cells=[] occupancy=[()()]";
constexpr auto kRagged = "rows=4 columns=3 "
	"cells=[0.0:0-1/0-1 0.1:0-1/1-2 0.2:0-1/2-3 1.0:1-2/0-1 3.0:3-4/0-1 3.1:3-4/1-2] "
	"occupancy=[(111)(1)()(11)]";
constexpr auto kRaggedTwoRows = "rows=2 columns=3 "
	"cells=[0.0:0-1/0-1 0.1:0-1/1-2 0.2:0-1/2-3 1.0:1-2/0-1] "
	"occupancy=[(111)(1)]";
constexpr auto kZeroSpans = "rows=2 columns=3 "
	"cells=[0.0:0-1/0-1 0.1:0-1/1-2 0.2:0-1/2-3 1.0:1-2/0-1 1.1:1-2/1-2 1.2:1-2/2-3] "
	"occupancy=[(111)(111)]";
constexpr auto kCollisions = "rows=2 columns=3 "
	"cells=[0.0:0-2/0-1 0.1:0-1/1-2 0.2:0-1/2-3 1.0:1-2/1-2 1.1:1-2/2-3] "
	"occupancy=[(111)(111)]";
constexpr auto kOverflowUnbounded = "rows=2 columns=2 "
	"cells=[0.0:0-2/0-1 0.1:0-1/1-2 1.0:1-2/1-2] "
	"occupancy=[(11)(11)]";
constexpr auto kOverflowBounded = "rows=2 columns=3 "
	"cells=[0.0:0-2/0-1 0.1:0-1/1-2 1.0:1-2/1-2 1.1:1-2/2-3] "
	"occupancy=[(11)(111)]";
constexpr auto kBottomUnbounded = "rows=3 columns=2 "
	"cells=[0.0:0-1/0-1 0.1:0-1/1-2 1.0:1-2/0-1 1.1:1-3/1-2 2.0:2-3/0-1] "
	"occupancy=[(11)(11)(11)]";
constexpr auto kFullTwoByTwo = "rows=2 columns=2 "
	"cells=[0.0:0-1/0-1 0.1:0-1/1-2 1.0:1-2/0-1 1.1:1-2/1-2] "
	"occupancy=[(11)(11)]";
constexpr auto kBottomBudget = "rows=3 columns=2 "
	"cells=[0.0:0-1/0-1 0.1:0-1/1-2 1.0:1-2/0-1 1.1:1-3/1-2] "
	"occupancy=[(11)(11)(01)]";
constexpr auto kRightEdgeBounded = "rows=1 columns=4 "
	"cells=[0.0:0-1/0-2 0.1:0-1/2-4] "
	"occupancy=[(1111)]";
constexpr auto kRightEdgeUnbounded = "rows=1 columns=5 "
	"cells=[0.0:0-1/0-2 0.1:0-1/2-5] "
	"occupancy=[(11111)]";
constexpr auto kWideBounded = "rows=1 columns=3 "
	"cells=[0.0:0-1/0-3] "
	"occupancy=[(111)]";
constexpr auto kWideUnbounded = "rows=1 columns=10 "
	"cells=[0.0:0-1/0-10] "
	"occupancy=[(1111111111)]";
constexpr auto kBudgetExceeded = "rows=2 columns=2 "
	"cells=[0.0:0-1/0-1 0.1:0-1/1-2 1.0:1-2/0-1] "
	"occupancy=[(11)(1)]";
constexpr auto kOversizedSkipped = "rows=1 columns=3 "
	"cells=[0.0:0-1/0-1 0.2:0-1/1-2 0.3:0-1/2-3] "
	"occupancy=[(111)]";
constexpr auto kOversizedAlone = "rows=2 columns=1 "
	"cells=[1.0:1-2/0-1] "
	"occupancy=[()(1)]";
constexpr auto kNoBudgetUnbounded = "rows=1 columns=6 "
	"cells=[0.0:0-1/0-1 0.1:0-1/1-4 0.2:0-1/4-5 0.3:0-1/5-6] "
	"occupancy=[(111111)]";

constexpr auto kMarkedTwice = "[(011)(011)(1)]";

struct Check {
	const char *name = nullptr;
	QString (*run)() = nullptr;
};

struct Span {
	int colspan = 1;
	int rowspan = 1;
};

constexpr auto kCell = Span();

struct NormalizeCase {
	int span = 0;
	int expected = 0;
};

struct ClampCase {
	int raw = 0;
	int start = 0;
	int count = 0;
	int expected = 0;
};

struct Area {
	int row = 0;
	int column = 0;
	int rowspan = 0;
	int colspan = 0;
};

struct OccupyCase {
	Area area;
	bool expected = false;
};

struct ColumnCase {
	int row = 0;
	int rowspan = 0;
	int colspan = 0;
	int maxColumns = 0;
	int expected = 0;
};

struct MarkStep {
	Area area;
	const char *expected = nullptr;
};

struct WidthCase {
	const char *label = nullptr;
	RichPage::Block table;
	int expected = 0;
};

struct IntersectCase {
	PreparedEditTableCellRange range;
	bool expected = false;
};

struct SelectCase {
	PreparedEditTableCellRange range;
	const char *expected = nullptr;
};

[[nodiscard]] RichPage::Block Table(
		std::initializer_list<std::initializer_list<Span>> rows) {
	auto result = RichPage::Block();
	result.kind = RichPage::BlockKind::Table;
	for (const auto &row : rows) {
		auto &cells = result.tableRows.emplace_back().cells;
		for (const auto &[colspan, rowspan] : row) {
			cells.push_back({ .colspan = colspan, .rowspan = rowspan });
		}
	}
	return result;
}

[[nodiscard]] MarkdownPrepareTableRenderLimits Limits(
		int maxRows,
		int maxColumns,
		int maxCells) {
	return {
		.maxRows = maxRows,
		.maxColumns = maxColumns,
		.maxCells = maxCells,
	};
}

[[nodiscard]] TableGridOccupancy Occupancy(
		std::initializer_list<std::initializer_list<int>> rows) {
	auto result = TableGridOccupancy();
	for (const auto &row : rows) {
		auto &marks = result.emplace_back();
		for (const auto mark : row) {
			marks.push_back(char(mark));
		}
	}
	return result;
}

[[nodiscard]] PreparedEditTableCellRange Range(
		int rowFrom,
		int rowTill,
		int columnFrom,
		int columnTill) {
	return {
		.rowFrom = rowFrom,
		.rowTill = rowTill,
		.columnFrom = columnFrom,
		.columnTill = columnTill,
	};
}

[[nodiscard]] RichPage::Block Ragged() {
	return Table({ { kCell, kCell, kCell }, { kCell }, {}, { kCell, kCell } });
}

[[nodiscard]] RichPage::Block ZeroSpans() {
	return Table({
		{ { 0, 0 }, { -3, -1 }, kCell },
		{ { 0, 0 }, kCell, kCell },
	});
}

[[nodiscard]] RichPage::Block Collisions() {
	return Table({ { { 1, 2 }, kCell, kCell }, { kCell, kCell } });
}

[[nodiscard]] RichPage::Block Overflow() {
	return Table({ { { 1, 2 }, kCell }, { kCell, kCell } });
}

[[nodiscard]] RichPage::Block Bottom() {
	return Table({ { kCell, kCell }, { kCell, { 1, 5 } }, { kCell } });
}

[[nodiscard]] RichPage::Block RightEdge() {
	return Table({ { { 2, 1 }, { 3, 1 } } });
}

[[nodiscard]] RichPage::Block Wide() {
	return Table({ { { 10, 1 } } });
}

[[nodiscard]] RichPage::Block TwoByTwo() {
	return Table({ { kCell, kCell }, { kCell, kCell } });
}

[[nodiscard]] RichPage::Block Oversized() {
	return Table({ { kCell, { 3, 1 }, kCell, kCell } });
}

[[nodiscard]] RichPage::Block OversizedAlone() {
	return Table({ { { 4, 1 } }, { kCell } });
}

[[nodiscard]] RichPage::Block Small() {
	return Table({ { kCell, kCell }, { kCell } });
}

[[nodiscard]] QString ArgsText(std::initializer_list<int> values) {
	auto parts = QStringList();
	for (const auto value : values) {
		parts.push_back(QString::number(value));
	}
	return parts.join(u", "_q);
}

[[nodiscard]] QString AreaText(const Area &area) {
	return ArgsText({ area.row, area.column, area.rowspan, area.colspan });
}

[[nodiscard]] QString RangeText(const PreparedEditTableCellRange &range) {
	return u"range(%1)"_q.arg(ArgsText({
		range.rowFrom,
		range.rowTill,
		range.columnFrom,
		range.columnTill,
	}));
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"true"_q : u"false"_q;
}

[[nodiscard]] QString CellText(const TableGridCellReference &cell) {
	return u"%1.%2:%3-%4/%5-%6"_q
		.arg(cell.rowIndex)
		.arg(cell.cellIndex)
		.arg(cell.rowFrom)
		.arg(cell.rowTill)
		.arg(cell.columnFrom)
		.arg(cell.columnTill);
}

[[nodiscard]] QString CellsText(
		const std::vector<TableGridCellReference> &cells) {
	auto parts = QStringList();
	for (const auto &cell : cells) {
		parts.push_back(CellText(cell));
	}
	return u"[%1]"_q.arg(parts.join(QChar(' ')));
}

[[nodiscard]] QString OccupancyText(const TableGridOccupancy &occupancy) {
	auto result = u"["_q;
	for (const auto &row : occupancy) {
		result += QChar('(');
		for (const auto mark : row) {
			result += QChar(mark ? '1' : '0');
		}
		result += QChar(')');
	}
	return result + QChar(']');
}

[[nodiscard]] QString GridText(const TableGrid &grid) {
	return u"rows=%1 columns=%2 cells=%3 occupancy=%4"_q.arg(
		QString::number(grid.rowCount),
		QString::number(grid.columnCount),
		CellsText(grid.cells),
		OccupancyText(grid.occupancy));
}

[[nodiscard]] QString Expect(
		const QString &what,
		const QString &got,
		const QString &expected) {
	return (got == expected)
		? QString()
		: u"%1: got %2, expected %3"_q.arg(what, got, expected);
}

[[nodiscard]] QString ExpectNumber(
		const QString &what,
		int got,
		int expected) {
	return Expect(what, QString::number(got), QString::number(expected));
}

[[nodiscard]] QString ExpectFlag(
		const QString &what,
		bool got,
		bool expected) {
	return Expect(what, FlagText(got), FlagText(expected));
}

[[nodiscard]] QString AllFailures(const std::vector<QString> &results) {
	auto failures = QStringList();
	for (const auto &result : results) {
		if (!result.isEmpty()) {
			failures.push_back(result);
		}
	}
	return failures.join(u"; "_q);
}

[[nodiscard]] QString ExpectGrid(
		const QString &what,
		const TableGrid &grid,
		const char *expected) {
	return Expect(what, GridText(grid), QString::fromLatin1(expected));
}

[[nodiscard]] QString ExpectUnbounded(
		const RichPage::Block &table,
		const char *expected) {
	return ExpectGrid(u"unbounded"_q, BuildTableGrid(table), expected);
}

[[nodiscard]] QString ExpectBounded(
		const RichPage::Block &table,
		const MarkdownPrepareTableRenderLimits &limits,
		const char *expected) {
	return ExpectGrid(
		u"limits(%1)"_q.arg(ArgsText({
			limits.maxRows,
			limits.maxColumns,
			limits.maxCells,
		})),
		BuildTableGrid(table, limits),
		expected);
}

[[nodiscard]] QString CheckNormalize() {
	const auto cases = std::vector<NormalizeCase>{
		{ kIntMin, 1 },
		{ -7, 1 },
		{ -1, 1 },
		{ 0, 1 },
		{ 1, 1 },
		{ 2, 2 },
		{ kIntMax, kIntMax },
	};
	auto results = std::vector<QString>();
	for (const auto &[span, expected] : cases) {
		results.push_back(ExpectNumber(
			u"NormalizeTableSpan(%1)"_q.arg(span),
			NormalizeTableSpan(span),
			expected));
	}
	return AllFailures(results);
}

[[nodiscard]] QString ExpectClamps(
		const QString &function,
		int (*clamp)(int, int, int),
		const std::vector<ClampCase> &cases) {
	auto results = std::vector<QString>();
	for (const auto &[raw, start, count, expected] : cases) {
		results.push_back(ExpectNumber(
			u"%1(%2)"_q.arg(function, ArgsText({ raw, start, count })),
			clamp(raw, start, count),
			expected));
	}
	return AllFailures(results);
}

[[nodiscard]] QString CheckClampRowspan() {
	const auto cases = std::vector<ClampCase>{
		{ 1, 0, 3, 1 },
		{ 3, 0, 3, 3 },
		{ 4, 1, 3, 2 },
		{ kIntMax, 1, 3, 2 },
		{ 0, 2, 3, 1 },
		{ -4, 2, 3, 1 },
		{ kIntMin, 0, 3, 1 },
		{ 2, 3, 3, 0 },
		{ 2, -1, 3, 0 },
		{ 2, 0, 0, 0 },
		{ 2, 0, -1, 0 },
	};
	return ExpectClamps(u"ClampTableRowspan"_q, ClampTableRowspan, cases);
}

[[nodiscard]] QString CheckClampColspan() {
	const auto cases = std::vector<ClampCase>{
		{ 1, 0, 4, 1 },
		{ 4, 0, 4, 4 },
		{ 3, 2, 4, 2 },
		{ kIntMax, 1, 4, 3 },
		{ 0, 3, 4, 1 },
		{ -2, 3, 4, 1 },
		{ 1, 4, 4, 0 },
		{ 1, -1, 4, 0 },
		{ 1, 0, 0, 0 },
		{ 1, 0, -2, 0 },
	};
	return ExpectClamps(u"ClampTableColspan"_q, ClampTableColspan, cases);
}

[[nodiscard]] QString ExpectCanOccupy(
		const TableGridOccupancy &occupancy,
		const std::vector<OccupyCase> &cases) {
	auto results = std::vector<QString>();
	for (const auto &[area, expected] : cases) {
		results.push_back(ExpectFlag(
			u"CanOccupyTableSlots(%1, %2)"_q.arg(
				OccupancyText(occupancy),
				AreaText(area)),
			CanOccupyTableSlots(
				occupancy,
				area.row,
				area.column,
				area.rowspan,
				area.colspan),
			expected));
	}
	return AllFailures(results);
}

[[nodiscard]] QString CheckCanOccupy() {
	const auto partialCases = std::vector<OccupyCase>{
		{ { 0, 0, 1, 1 }, true },
		{ { 0, 0, 1, 2 }, false },
		{ { 0, 1, 1, 1 }, false },
		{ { 0, 2, 1, 5 }, true },
		{ { 1, 0, 2, 4 }, true },
		{ { 2, 0, 5, 1 }, true },
		{ { 3, 0, 1, 1 }, false },
		{ { -1, 0, 1, 1 }, false },
		{ { 0, -1, 1, 1 }, false },
		{ { 0, 0, 0, 1 }, false },
		{ { 0, 0, 1, 0 }, false },
	};
	const auto lowerCases = std::vector<OccupyCase>{
		{ { 0, 0, 2, 1 }, false },
		{ { 0, 0, 1, 1 }, true },
		{ { 1, 1, 1, 1 }, true },
	};
	return AllFailures({
		ExpectCanOccupy(
			Occupancy({ { 0, 1, 0 }, { 0, 0 }, {} }),
			partialCases),
		ExpectCanOccupy(Occupancy({ { 0 }, { 1 } }), lowerCases),
	});
}

[[nodiscard]] QString ExpectFirstColumn(
		const TableGridOccupancy &occupancy,
		const std::vector<ColumnCase> &cases) {
	auto results = std::vector<QString>();
	for (const auto &[row, rowspan, colspan, maxColumns, expected] : cases) {
		results.push_back(ExpectNumber(
			u"FirstAvailableTableColumn(%1, %2)"_q.arg(
				OccupancyText(occupancy),
				ArgsText({ row, rowspan, colspan, maxColumns })),
			FirstAvailableTableColumn(
				occupancy,
				row,
				rowspan,
				colspan,
				maxColumns),
			expected));
	}
	return AllFailures(results);
}

[[nodiscard]] QString CheckFirstAvailable() {
	const auto steppedCases = std::vector<ColumnCase>{
		{ 0, 1, 1, 4, 2 },
		{ 1, 1, 1, 4, 1 },
		{ 0, 2, 1, 4, 2 },
		{ 0, 2, 2, 4, 2 },
		{ 0, 2, 3, 4, 2 },
		{ 1, 5, 1, 4, 1 },
		{ 0, 1, 1, 2, -1 },
		{ 2, 1, 1, 4, -1 },
		{ -1, 1, 1, 4, -1 },
		{ 0, 0, 1, 4, -1 },
		{ 0, 1, 0, 4, -1 },
		{ 0, 1, 1, 0, -1 },
	};
	const auto edgeCases = std::vector<ColumnCase>{
		{ 0, 1, 3, 3, 1 },
	};
	return AllFailures({
		ExpectFirstColumn(
			Occupancy({ { 1, 1, 0, 0 }, { 1, 0, 0, 0 } }),
			steppedCases),
		ExpectFirstColumn(Occupancy({ { 1, 0, 0, 1 } }), edgeCases),
	});
}

[[nodiscard]] QString CheckMarkAndColumnCount() {
	const auto steps = std::vector<MarkStep>{
		{ { 0, 1, 2, 2 }, "[(011)(011)()]" },
		{ { 2, 0, 5, 1 }, kMarkedTwice },
		{ { 0, 0, 0, 1 }, kMarkedTwice },
		{ { 0, 0, 1, 0 }, kMarkedTwice },
		{ { 3, 0, 1, 1 }, kMarkedTwice },
		{ { -1, 0, 1, 1 }, kMarkedTwice },
		{ { 0, -1, 1, 1 }, kMarkedTwice },
		{ { 0, 0, 1, 1 }, "[(111)(011)(1)]" },
	};
	auto occupancy = Occupancy({ {}, {}, {} });
	auto results = std::vector<QString>();
	for (const auto &[area, expected] : steps) {
		MarkTableSlots(
			&occupancy,
			area.row,
			area.column,
			area.rowspan,
			area.colspan);
		results.push_back(Expect(
			u"after MarkTableSlots(%1)"_q.arg(AreaText(area)),
			OccupancyText(occupancy),
			QString::fromLatin1(expected)));
	}
	results.push_back(ExpectNumber(
		u"TableGridColumnCount(marked)"_q,
		TableGridColumnCount(occupancy),
		3));
	results.push_back(ExpectNumber(
		u"TableGridColumnCount(no rows)"_q,
		TableGridColumnCount(TableGridOccupancy()),
		0));
	results.push_back(ExpectNumber(
		u"TableGridColumnCount(unmarked row sizes)"_q,
		TableGridColumnCount(Occupancy({ {}, { 0, 0, 0, 0 }, { 1 } })),
		4));
	return AllFailures(results);
}

[[nodiscard]] QString CheckWidestSourceRow() {
	const auto cases = std::vector<WidthCase>{
		{ "no rows", Table({}), 0 },
		{ "widest last row", Table({ { kCell, kCell }, {}, { { 3, 1 } } }), 3 },
		{
			"normalized spans",
			Table({ { { 0, 1 }, { -2, 1 }, { 2, 1 } } }),
			4,
		},
		{ "rowspan ignored", Table({ { { 1, 3 } }, { kCell, kCell } }), 2 },
	};
	auto results = std::vector<QString>();
	for (const auto &[label, table, expected] : cases) {
		results.push_back(ExpectNumber(
			u"TableMaxColumns(%1)"_q.arg(QString::fromLatin1(label)),
			TableMaxColumns(table),
			expected));
	}
	return AllFailures(results);
}

[[nodiscard]] QString CheckHalfOpenEdges() {
	const auto cell = TableGridCellReference{
		.rowIndex = 0,
		.cellIndex = 0,
		.rowFrom = 1,
		.rowTill = 3,
		.columnFrom = 2,
		.columnTill = 4,
	};
	const auto cases = std::vector<IntersectCase>{
		{ Range(0, 1, 2, 4), false },
		{ Range(3, 4, 2, 4), false },
		{ Range(1, 3, 0, 2), false },
		{ Range(1, 3, 4, 6), false },
		{ Range(0, 2, 2, 4), true },
		{ Range(2, 4, 2, 4), true },
		{ Range(1, 3, 0, 3), true },
		{ Range(1, 3, 3, 6), true },
		{ Range(2, 3, 3, 4), true },
		{ Range(0, 5, 0, 6), true },
	};
	auto results = std::vector<QString>();
	for (const auto &[range, expected] : cases) {
		results.push_back(ExpectFlag(
			u"TableGridCellIntersectsRange(%1, %2)"_q.arg(
				CellText(cell),
				RangeText(range)),
			TableGridCellIntersectsRange(cell, range),
			expected));
	}
	return AllFailures(results);
}

[[nodiscard]] QString CheckSelectedCells() {
	const auto grid = BuildTableGrid(Collisions());
	const auto cases = std::vector<SelectCase>{
		{ Range(0, 1, 1, 3), "[0.1:0-1/1-2 0.2:0-1/2-3]" },
		{ Range(1, 2, 0, 1), "[0.0:0-2/0-1]" },
		{ Range(1, 2, 1, 3), "[1.0:1-2/1-2 1.1:1-2/2-3]" },
		{
			Range(0, 2, 0, 3),
			"[0.0:0-2/0-1 0.1:0-1/1-2 0.2:0-1/2-3 1.0:1-2/1-2 1.1:1-2/2-3]",
		},
		{ Range(2, 3, 0, 3), "[]" },
		{ Range(0, 2, 3, 4), "[]" },
	};
	auto results = std::vector<QString>();
	for (const auto &[range, expected] : cases) {
		results.push_back(Expect(
			u"SelectedTableGridCells(%1)"_q.arg(RangeText(range)),
			CellsText(SelectedTableGridCells(grid, range)),
			QString::fromLatin1(expected)));
	}
	return AllFailures(results);
}

[[nodiscard]] std::vector<Check> Checks() {
	return {
		{ "span.normalize", CheckNormalize },
		{ "span.clamp-rowspan", CheckClampRowspan },
		{ "span.clamp-colspan", CheckClampColspan },
		{ "occupancy.can-occupy", CheckCanOccupy },
		{ "occupancy.first-available", CheckFirstAvailable },
		{ "occupancy.mark-and-column-count", CheckMarkAndColumnCount },
		{ "columns.widest-source-row", CheckWidestSourceRow },
		{ "unbounded.empty-and-blank-rows", [] {
			return AllFailures({
				ExpectUnbounded(Table({}), kEmpty),
				ExpectUnbounded(Table({ {}, {} }), kBlankTwo),
			});
		} },
		{ "bounded.empty-and-blank-rows", [] {
			return AllFailures({
				ExpectBounded(Table({}), Limits(10, 10, 100), kEmpty),
				ExpectBounded(Table({ {}, {} }), Limits(10, 10, 100), kBlankTwo),
			});
		} },
		{ "unbounded.ragged-rows", [] {
			return ExpectUnbounded(Ragged(), kRagged);
		} },
		{ "bounded.ragged-rows-row-limit", [] {
			return ExpectBounded(Ragged(), Limits(2, 3, 100), kRaggedTwoRows);
		} },
		{ "unbounded.zero-negative-spans", [] {
			return ExpectUnbounded(ZeroSpans(), kZeroSpans);
		} },
		{ "bounded.zero-negative-spans", [] {
			return ExpectBounded(ZeroSpans(), Limits(2, 3, 6), kZeroSpans);
		} },
		{ "unbounded.rowspan-collisions", [] {
			return ExpectUnbounded(Collisions(), kCollisions);
		} },
		{ "bounded.rowspan-collisions", [] {
			return ExpectBounded(Collisions(), Limits(2, 3, 6), kCollisions);
		} },
		{ "unbounded.bottom-rowspan-clipped", [] {
			return ExpectUnbounded(Bottom(), kBottomUnbounded);
		} },
		{ "bounded.row-limit-clips-rowspan", [] {
			return ExpectBounded(Bottom(), Limits(2, 2, 100), kFullTwoByTwo);
		} },
		{ "bounded.exact-budget-then-skip", [] {
			return ExpectBounded(Bottom(), Limits(3, 2, 5), kBottomBudget);
		} },
		{ "bounded.right-edge-clipped", [] {
			return AllFailures({
				ExpectBounded(RightEdge(), Limits(1, 4, 4), kRightEdgeBounded),
				ExpectBounded(Wide(), Limits(1, 3, 3), kWideBounded),
			});
		} },
		{ "unbounded.width-from-widest-row", [] {
			return AllFailures({
				ExpectUnbounded(RightEdge(), kRightEdgeUnbounded),
				ExpectUnbounded(Wide(), kWideUnbounded),
			});
		} },
		{ "divergence.rowspan-overflow", [] {
			return AllFailures({
				ExpectUnbounded(Overflow(), kOverflowUnbounded),
				ExpectBounded(Overflow(), Limits(2, 3, 6), kOverflowBounded),
				ExpectBounded(Overflow(), Limits(2, 2, 100), kOverflowUnbounded),
			});
		} },
		{ "bounded.cell-budget", [] {
			return AllFailures({
				ExpectBounded(TwoByTwo(), Limits(2, 2, 4), kFullTwoByTwo),
				ExpectBounded(TwoByTwo(), Limits(2, 2, 3), kBudgetExceeded),
				ExpectBounded(TwoByTwo(), Limits(2, 2, 0), kBlankTwo),
			});
		} },
		{ "bounded.oversized-cell-skipped", [] {
			return AllFailures({
				ExpectBounded(Oversized(), Limits(1, 4, 3), kOversizedSkipped),
				ExpectBounded(OversizedAlone(), Limits(2, 4, 3), kOversizedAlone),
			});
		} },
		{ "unbounded.no-cell-budget", [] {
			return ExpectUnbounded(Oversized(), kNoBudgetUnbounded);
		} },
		{ "bounded.non-positive-limits", [] {
			return AllFailures({
				ExpectBounded(Small(), Limits(0, 4, 100), kEmpty),
				ExpectBounded(Small(), Limits(-3, 4, 100), kEmpty),
				ExpectBounded(Small(), Limits(2, 0, 100), kBlankTwo),
				ExpectBounded(Small(), Limits(2, -1, 100), kBlankTwo),
				ExpectBounded(Small(), Limits(2, 2, -5), kBlankTwo),
			});
		} },
		{ "bounded.matches-unbounded-within-limits", [] {
			return ExpectBounded(Ragged(), Limits(4, 3, 12), kRagged);
		} },
		{ "selection.half-open-edges", CheckHalfOpenEdges },
		{ "selection.cells-in-grid-order", CheckSelectedCells },
	};
}

} // namespace

int main() {
	const auto checks = Checks();
	auto passed = 0;
	for (const auto &[name, run] : checks) {
		const auto failure = run();
		if (failure.isEmpty()) {
			++passed;
			std::printf("PASS %s\n", name);
			continue;
		}
		std::printf("FAIL %s: %s\n", name, failure.toUtf8().constData());
	}
	const auto total = int(checks.size());
	std::printf("%d of %d checks passed.\n", passed, total);
	return (passed == total) ? 0 : 1;
}
