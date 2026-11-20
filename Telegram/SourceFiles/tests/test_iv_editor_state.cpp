/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/platform/base_platform_file_utilities.h"
#include "iv/editor/iv_editor_page_table_grid.h"
#include "iv/editor/iv_editor_state.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "lang/lang_keys.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_font.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtGui/QGuiApplication>

#include <array>
#include <cstdio>
#include <vector>

// WHY: the real one in lang_instance.cpp needs Core::App(); use generated English.
namespace Lang::details {

QString Current(ushort key) {
	return GetOriginalValue(key);
}

} // namespace Lang::details

namespace {

using namespace Iv::Editor;
using Iv::RichMessageLimitError;
using Iv::RichMessageLimits;
using Iv::RichPage;
using Iv::Markdown::InlineTextObjectButtonData;
using Iv::Markdown::InlineTextObjectFormulaData;
using Iv::Markdown::InlineTextObjectKind;
using Iv::Markdown::PreparedBlock;
using Iv::Markdown::PreparedBlockKind;
using Iv::Markdown::PreparedEditBlockContainerKind;
using Iv::Markdown::PreparedEditBlockContainerPath;
using Iv::Markdown::PreparedEditBlockDropTarget;
using Iv::Markdown::PreparedEditDropTarget;
using Iv::Markdown::PreparedEditListItemDropTarget;
using Iv::Markdown::PreparedEditSelection;
using Iv::Markdown::PreparedEditSelectionKind;
using Iv::Markdown::PreparedEditTableCellRange;
using Iv::Markdown::PreparedEditTextDropTarget;
using Iv::Markdown::PrepareTableRenderLimitsForRichMessage;
using Iv::Markdown::SerializeInlineTextObjectEntity;

using Action = State::BoundaryTarget::Action;
using Block = RichPage::Block;
using BlockKind = RichPage::BlockKind;
using FieldMode = State::FieldMode;
using Formatting = State::TextFormattingAction;
using ListKind = RichPage::ListKind;
using Result = State::ApplyResult;
using TextSpan = State::TextNodeSpan;

constexpr auto kFlat4AtC = R"(2=text:2"C" ; live P0 P1 P2 P3 nodes=4)";
constexpr auto kListedAtI1 = R"(2=item:1#1"i1" ; live P0 L1 P2 nodes=5)";
constexpr auto kTabledAtR0C0 = R"(2=cell:1#0,0"r0c0" ; live P0 T1 P2 nodes=9)";
constexpr auto kListed4AtI1 = R"(2=item:1#1"i1" ; live P0 L1 P2 nodes=6)";
constexpr auto kTwoListsAtA0 = R"(0=item:0#0"a0" ; live L0 P1 L2 nodes=5)";
constexpr auto kTemporaryAtEmpty = R"(1=text:1"" ; live P0 P1 P2 nodes=3)";

struct Cell {
	QString text;
	int colspan = 1;
	int rowspan = 1;
};

using CellRows = std::vector<std::vector<Cell>>;

struct Fixture {
	const char *name = nullptr;
	RichPage (*page)() = nullptr;
	RichMessageLimits limits;
	bool temporaryParagraph = false;
	const char *text = nullptr;
	const char *state = nullptr;
	const char *grid = nullptr;
};

struct Start {
	const char *id = nullptr;
	Fixture fixture;
	int active = 0;
	PreparedEditSelection selection;
};

struct RemovalCase {
	Start start;
	bool forward = true;
	std::optional<int> returned;
	RichPage page;
	const char *state = nullptr;
	const char *leaves = nullptr;
	const char *spans = nullptr;
	const char *grid = nullptr;
};

struct Outcome {
	Result result = Result::Failed;
	State::BoundaryTarget destination;
};

struct DropCase {
	Start start;
	PreparedEditDropTarget target;
	Outcome outcome;
	std::optional<RichMessageLimitError> limit;
	RichPage page;
	const char *state = nullptr;
	const char *leaves = nullptr;
	bool temporary = false;
};

struct Observed {
	State::Snapshot snapshot;
	const RichPage *page = nullptr;
	int ordinal = -1;
	TextWithEntities text;
	QString summary;
	QString leaves;
};

struct Report {
	int cases = 0;
	QStringList failures;
};

struct Check {
	const char *name = nullptr;
	Report (*run)() = nullptr;
};

class Expect final {
public:
	Expect(QStringList &failures, const char *id);

	void same(
		const char *label,
		bool equal,
		QString got,
		const QString &expected);
	void text(const char *label, const QString &got, const QString &expected);
	void number(const char *label, int got, int expected);
	void page(const char *label, const RichPage &got, const RichPage &expected);

private:
	QStringList &_failures;
	QString _id;

};

[[nodiscard]] QString Latin(const char *text) {
	return QString::fromLatin1(text);
}

[[nodiscard]] QString QuotedText(const QString &text) {
	return '"' + text + '"';
}

[[nodiscard]] QString QuotedText(const RichPage::RichText &text) {
	return QuotedText(text.text.text);
}

[[nodiscard]] QString BlocksText(const std::vector<Block> &blocks);

[[nodiscard]] QString ListText(const Block &block) {
	auto result = (block.listKind == ListKind::Ordered) ? u"LO"_q : u"L"_q;
	if (const auto start = block.orderedList.start) {
		result += u"#%1"_q.arg(*start);
	}
	auto items = QStringList();
	for (const auto &item : block.listItems) {
		auto text = QuotedText(item.text);
		if (!item.blocks.empty()) {
			text += '{' + BlocksText(item.blocks) + '}';
		}
		items.push_back(text);
	}
	return result + '[' + items.join(' ') + ']';
}

[[nodiscard]] QString TableText(const Block &block) {
	auto rows = QStringList();
	for (const auto &row : block.tableRows) {
		auto cells = QStringList();
		for (const auto &cell : row.cells) {
			auto text = QuotedText(cell.text);
			if (cell.colspan != 1) {
				text += u"*c%1"_q.arg(cell.colspan);
			}
			if (cell.rowspan != 1) {
				text += u"*r%1"_q.arg(cell.rowspan);
			}
			cells.push_back(text);
		}
		rows.push_back(cells.join(' '));
	}
	return 'T' + QuotedText(block.text) + '{' + rows.join('|') + '}';
}

[[nodiscard]] QString BlockText(const Block &block) {
	switch (block.kind) {
	case BlockKind::Paragraph: return 'P' + QuotedText(block.text);
	case BlockKind::Unsupported: return u"U"_q;
	case BlockKind::Divider: return u"V"_q;
	case BlockKind::List: return ListText(block);
	case BlockKind::Table: return TableText(block);
	case BlockKind::Quote:
		return 'Q'
			+ QuotedText(block.text)
			+ '['
			+ BlocksText(block.blocks)
			+ u"]/"_q
			+ QuotedText(block.caption);
	default: return u"K%1"_q.arg(int(block.kind));
	}
}

QString BlocksText(const std::vector<Block> &blocks) {
	auto result = QStringList();
	for (const auto &block : blocks) {
		result.push_back(BlockText(block));
	}
	return result.join(' ');
}

[[nodiscard]] QString PageText(const RichPage &page) {
	return BlocksText(page.blocks);
}

template <typename Container>
[[nodiscard]] QString ContainerText(const Container &container) {
	using StepKind = decltype(container.steps.front().kind);
	auto result = QString();
	for (const auto &step : container.steps) {
		result += (step.kind == StepKind::ListItemChildren)
			? u"%1.%2/"_q.arg(step.blockIndex).arg(step.listItemIndex)
			: u"%1/"_q.arg(step.blockIndex);
	}
	return result;
}

template <typename Leaf>
[[nodiscard]] QString LeafText(const Leaf &leaf) {
	using Kind = decltype(leaf.kind);
	const auto path = ContainerText(leaf.block.container)
		+ QString::number(leaf.block.index);
	switch (leaf.kind) {
	case Kind::BlockText: return u"text:"_q + path;
	case Kind::BlockCaption: return u"caption:"_q + path;
	case Kind::MathFormula: return u"math:"_q + path;
	case Kind::ListItemText:
		return u"item:%1#%2"_q.arg(path).arg(leaf.listItemIndex);
	case Kind::TableCellText:
		return u"cell:%1#%2,%3"_q
			.arg(path)
			.arg(leaf.tableRowIndex)
			.arg(leaf.tableCellIndex);
	}
	return u"?"_q;
}

[[nodiscard]] QString OptionalLeafText(const std::optional<LeafPath> &leaf) {
	return leaf ? LeafText(*leaf) : u"none"_q;
}

[[nodiscard]] QString PreparedLetter(PreparedBlockKind kind) {
	switch (kind) {
	case PreparedBlockKind::Paragraph: return u"P"_q;
	case PreparedBlockKind::List: return u"L"_q;
	case PreparedBlockKind::Quote: return u"Q"_q;
	case PreparedBlockKind::Table: return u"T"_q;
	case PreparedBlockKind::Rule: return u"V"_q;
	default: return u"?"_q;
	}
}

[[nodiscard]] QString PreparedSummary(const State &state) {
	const auto &prepared = state.prepared();
	auto result = (prepared.richPage.get() == &state.richPage())
		? u"live"_q
		: u"stale"_q;
	for (const auto &block : prepared.blocks.blocks) {
		result += ' ' + PreparedLetter(block.kind) + (block.editBlock
			? QString::number(block.editBlock->path.index)
			: u"-"_q);
	}
	return result + u" nodes=%1"_q.arg(state.textNodeCount());
}

void AppendLeaves(
		QStringList &leaves,
		const std::vector<PreparedBlock> &blocks) {
	for (const auto &block : blocks) {
		if (block.editLeaf) {
			leaves.push_back(
				LeafText(*block.editLeaf) + QuotedText(block.text.text));
		}
		AppendLeaves(leaves, block.children);
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				if (cell.editLeaf) {
					leaves.push_back(
						LeafText(*cell.editLeaf) + QuotedText(cell.text.text));
				}
			}
		}
	}
}

[[nodiscard]] QString PreparedLeaves(const State &state) {
	auto result = QStringList();
	AppendLeaves(result, state.prepared().blocks.blocks);
	return result.join(' ');
}

[[nodiscard]] QString PreparedSpans(const State &state) {
	auto rows = QStringList();
	for (const auto &block : state.prepared().blocks.blocks) {
		for (const auto &row : block.tableRows) {
			auto cells = QStringList();
			for (const auto &cell : row.cells) {
				cells.push_back(u"%1x%2"_q.arg(cell.colspan).arg(cell.rowspan));
			}
			rows.push_back(cells.join(' '));
		}
	}
	return rows.join('|');
}

[[nodiscard]] QString GridText(
		const RichPage &page,
		const RichMessageLimits &limits) {
	const auto renderLimits = PrepareTableRenderLimitsForRichMessage(limits);
	auto cells = QStringList();
	for (const auto &block : page.blocks) {
		if (block.kind != BlockKind::Table) {
			continue;
		}
		const auto grid = BuildTableGrid(block, renderLimits);
		for (const auto &reference : grid.cells) {
			const auto &cell = block.tableRows[reference.rowIndex].cells[
				reference.cellIndex];
			cells.push_back(u"%1@r%2-%3c%4-%5"_q
				.arg(QuotedText(cell.text))
				.arg(reference.rowFrom)
				.arg(reference.rowTill)
				.arg(reference.columnFrom)
				.arg(reference.columnTill));
		}
	}
	return cells.join(' ');
}

[[nodiscard]] QString StateText(const State &state) {
	return u"%1=%2%3 ; %4"_q.arg(
		QString::number(state.activeTextOrdinal()),
		OptionalLeafText(state.activeLeafPath()),
		QuotedText(state.activeText().text),
		PreparedSummary(state));
}

[[nodiscard]] QString ReturnedText(std::optional<int> returned) {
	return returned ? u"ret=%1"_q.arg(*returned) : u"ret=none"_q;
}

[[nodiscard]] QString ResultText(Result result) {
	constexpr auto kNames = std::array{ "failed", "unchanged", "changed" };
	return Latin(kNames[int(result)]);
}

[[nodiscard]] QString ActionText(Action action) {
	constexpr auto kNames = std::array{
		"none",
		"text",
		"selection",
		"remove-owner",
	};
	return Latin(kNames[int(action)]);
}

[[nodiscard]] QString LimitText(std::optional<RichMessageLimitError> error) {
	constexpr auto kNames = std::array{
		"length",
		"blocks",
		"depth",
		"media",
		"table-columns",
		"buttons",
	};
	return error ? Latin(kNames[int(*error)]) : u"none"_q;
}

[[nodiscard]] QString SelectionText(const PreparedEditSelection &selection) {
	const auto &blocks = selection.blocks;
	return u"kind=%1 blocks=%2[%3,%4)"_q
		.arg(int(selection.kind))
		.arg(ContainerText(blocks.container))
		.arg(blocks.from)
		.arg(blocks.till);
}

Expect::Expect(QStringList &failures, const char *id)
: _failures(failures)
, _id(Latin(id)) {
}

void Expect::same(
		const char *label,
		bool equal,
		QString got,
		const QString &expected) {
	if (equal) {
		return;
	}
	if (got == expected) {
		got += u" (other fields differ)"_q;
	}
	_failures.push_back(u"%1 %2: got %3, expected %4"_q.arg(
		_id,
		Latin(label),
		got,
		expected));
}

void Expect::text(
		const char *label,
		const QString &got,
		const QString &expected) {
	same(label, got == expected, got, expected);
}

void Expect::number(const char *label, int got, int expected) {
	text(label, QString::number(got), QString::number(expected));
}

void Expect::page(
		const char *label,
		const RichPage &got,
		const RichPage &expected) {
	same(label, got == expected, PageText(got), PageText(expected));
}

[[nodiscard]] RichPage::RichText Text(const QString &text) {
	return { .text = tr::marked(text) };
}

[[nodiscard]] Block Bare(BlockKind kind) {
	auto result = Block();
	result.kind = kind;
	return result;
}

[[nodiscard]] Block Paragraph(const QString &text) {
	auto result = Bare(BlockKind::Paragraph);
	result.text = Text(text);
	return result;
}

[[nodiscard]] std::vector<Block> Paragraphs(
		std::initializer_list<QString> texts) {
	auto result = std::vector<Block>();
	for (const auto &text : texts) {
		result.push_back(Paragraph(text));
	}
	return result;
}

[[nodiscard]] RichPage::ListItem Item(
		const QString &text,
		std::vector<Block> children = {}) {
	auto result = RichPage::ListItem();
	result.text = Text(text);
	result.blocks = std::move(children);
	return result;
}

[[nodiscard]] Block List(
		std::vector<RichPage::ListItem> items,
		ListKind kind = ListKind::Bullet,
		std::optional<int> start = std::nullopt) {
	auto result = Bare(BlockKind::List);
	result.listKind = kind;
	result.orderedList.start = start;
	result.listItems = std::move(items);
	return result;
}

[[nodiscard]] Block List(std::initializer_list<QString> texts) {
	auto items = std::vector<RichPage::ListItem>();
	for (const auto &text : texts) {
		items.push_back(Item(text));
	}
	return List(std::move(items));
}

[[nodiscard]] Block Quote(
		std::vector<Block> children,
		const QString &caption,
		const QString &text = QString()) {
	auto result = Bare(BlockKind::Quote);
	result.text = Text(text);
	result.caption = Text(caption);
	result.blocks = std::move(children);
	return result;
}

[[nodiscard]] Block Table(const QString &title, const CellRows &rows) {
	auto result = Bare(BlockKind::Table);
	result.text = Text(title);
	for (const auto &cells : rows) {
		auto &row = result.tableRows.emplace_back();
		for (const auto &cell : cells) {
			row.cells.push_back({
				.text = Text(cell.text),
				.colspan = cell.colspan,
				.rowspan = cell.rowspan,
			});
		}
	}
	return result;
}

[[nodiscard]] CellRows Grid(
		std::initializer_list<int> rows,
		std::initializer_list<int> columns) {
	auto result = CellRows();
	for (const auto row : rows) {
		auto &cells = result.emplace_back();
		for (const auto column : columns) {
			cells.push_back({ .text = u"r%1c%2"_q.arg(row).arg(column) });
		}
	}
	return result;
}

[[nodiscard]] RichPage Page(std::vector<Block> blocks) {
	auto result = RichPage();
	result.blocks = std::move(blocks);
	return result;
}

[[nodiscard]] RichPage Flat(std::initializer_list<QString> texts) {
	return Page(Paragraphs(texts));
}

[[nodiscard]] RichPage Framed(Block middle) {
	return Page({ Paragraph(u"A"_q), std::move(middle), Paragraph(u"C"_q) });
}

[[nodiscard]] RichPage Framed(const QString &title, const CellRows &rows) {
	return Framed(Table(title, rows));
}

[[nodiscard]] RichPage WithDecoratedCell(RichPage page, int row, int cell) {
	auto &decorated = page.blocks.front().tableRows[row].cells[cell];
	decorated.text.text = tr::bold(decorated.text.text.text);
	decorated.header = true;
	decorated.alignment = RichPage::TableAlignment::Center;
	decorated.verticalAlignment = RichPage::TableVerticalAlignment::Middle;
	return page;
}

constexpr auto kFlat4 = Fixture{
	.name = "flat4",
	.page = [] { return Flat({ u"A"_q, u"B"_q, u"C"_q, u"D"_q }); },
	.text = R"(P"A" P"B" P"C" P"D")",
	.state = R"(0=text:0"A" ; live P0 P1 P2 P3 nodes=4)",
};
constexpr auto kFlat5 = Fixture{
	.name = "flat5",
	.page = [] { return Flat({ u"A"_q, u"B"_q, u"C"_q, u"D"_q, u"E"_q }); },
	.text = R"(P"A" P"B" P"C" P"D" P"E")",
	.state = R"(0=text:0"A" ; live P0 P1 P2 P3 P4 nodes=5)",
};
constexpr auto kQuoted = Fixture{
	.name = "quoted",
	.page = [] {
		return Framed(Quote(
			Paragraphs({ u"q0"_q, u"q1"_q, u"q2"_q }),
			u"cap"_q));
	},
	.text = R"(P"A" Q""[P"q0" P"q1" P"q2"]/"cap" P"C")",
	.state = R"(0=text:0"A" ; live P0 Q1 P2 nodes=6)",
};
constexpr auto kListed = Fixture{
	.name = "listed",
	.page = [] { return Framed(List({ u"i0"_q, u"i1"_q, u"i2"_q })); },
	.text = R"(P"A" L["i0" "i1" "i2"] P"C")",
	.state = R"(0=text:0"A" ; live P0 L1 P2 nodes=5)",
};
constexpr auto kListed4 = Fixture{
	.name = "listed4",
	.page = [] {
		return Framed(List({ u"i0"_q, u"i1"_q, u"i2"_q, u"i3"_q }));
	},
	.text = R"(P"A" L["i0" "i1" "i2" "i3"] P"C")",
	.state = R"(0=text:0"A" ; live P0 L1 P2 nodes=6)",
};
constexpr auto kNested = Fixture{
	.name = "nested",
	.page = [] {
		return Framed(List({
			Item(u"i0"_q, Paragraphs({ u"x0"_q, u"x1"_q })),
			Item(u"i1"_q),
		}));
	},
	.text = R"(P"A" L["i0"{P"x0" P"x1"} "i1"] P"C")",
	.state = R"(0=text:0"A" ; live P0 L1 P2 nodes=6)",
};
constexpr auto kTabled = Fixture{
	.name = "tabled",
	.page = [] { return Framed(u"T"_q, Grid({ 0, 1, 2 }, { 0, 1 })); },
	.text = R"(P"A" T"T"{"r0c0" "r0c1"|"r1c0" "r1c1"|"r2c0" "r2c1"} P"C")",
	.state = R"(0=text:0"A" ; live P0 T1 P2 nodes=9)",
};
constexpr auto kTabled3 = Fixture{
	.name = "tabled3",
	.page = [] { return Framed(u"T"_q, Grid({ 0, 1, 2 }, { 0, 1, 2 })); },
	.text = R"(P"A" T"T"{"r0c0" "r0c1" "r0c2"|"r1c0" "r1c1" "r1c2")"
		R"(|"r2c0" "r2c1" "r2c2"} P"C")",
	.state = R"(0=text:0"A" ; live P0 T1 P2 nodes=12)",
};
constexpr auto kUntitled = Fixture{
	.name = "untitled",
	.page = [] { return Framed(QString(), Grid({ 0, 1 }, { 0, 1 })); },
	.text = R"(P"A" T""{"r0c0" "r0c1"|"r1c0" "r1c1"} P"C")",
	.state = R"(0=text:0"A" ; live P0 T1 P2 nodes=7)",
};
constexpr auto kColMerged = Fixture{
	.name = "colMerged",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"m"_q, 2 }, { u"x"_q } },
			{ { u"a"_q }, { u"b"_q }, { u"c"_q } },
		}) });
	},
	.text = R"(T"T"{"m"*c2 "x"|"a" "b" "c"})",
	.state = R"(0=text:0"T" ; live T0 nodes=6)",
};
constexpr auto kRowMerged = Fixture{
	.name = "rowMerged",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"a"_q }, { u"m"_q, 1, 2 }, { u"b"_q } },
			{ { u"c"_q }, { u"d"_q } },
		}) });
	},
	.text = R"(T"T"{"a" "m"*r2 "b"|"c" "d"})",
	.state = R"(0=text:0"T" ; live T0 nodes=6)",
	.grid = R"("a"@r0-1c0-1 "m"@r0-2c1-2 "b"@r0-1c2-3)"
		R"( "c"@r1-2c0-1 "d"@r1-2c2-3)",
};
constexpr auto kRowSpanRows = Fixture{
	.name = "rowSpanRows",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"a"_q, 1, 2 }, { u"b"_q } },
			{ { u"c"_q } },
			{ { u"d"_q }, { u"e"_q } },
		}) });
	},
	.text = R"(T"T"{"a"*r2 "b"|"c"|"d" "e"})",
	.state = R"(0=text:0"T" ; live T0 nodes=6)",
	.grid = R"("a"@r0-2c0-1 "b"@r0-1c1-2 "c"@r1-2c1-2)"
		R"( "d"@r2-3c0-1 "e"@r2-3c1-2)",
};
constexpr auto kRowSpanThree = Fixture{
	.name = "rowSpanThree",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"a"_q, 1, 3 }, { u"b"_q } },
			{ { u"c"_q } },
			{ { u"d"_q } },
			{ { u"e"_q }, { u"f"_q } },
		}) });
	},
	.text = R"(T"T"{"a"*r3 "b"|"c"|"d"|"e" "f"})",
	.state = R"(0=text:0"T" ; live T0 nodes=7)",
	.grid = R"("a"@r0-3c0-1 "b"@r0-1c1-2 "c"@r1-2c1-2 "d"@r2-3c1-2)"
		R"( "e"@r3-4c0-1 "f"@r3-4c1-2)",
};
constexpr auto kEmptyTarget = Fixture{
	.name = "emptyTarget",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"a"_q, 2, 3 }, { u"x"_q } },
			{ { u"b"_q, 1, 2 } },
			{},
		}) });
	},
	.text = R"(T"T"{"a"*c2*r3 "x"|"b"*r2|})",
	.state = R"(0=text:0"T" ; live T0 nodes=4)",
	.grid = R"("a"@r0-3c0-2 "x"@r0-1c2-3 "b"@r1-3c2-3)",
};
constexpr auto kTrailingSpans = Fixture{
	.name = "trailingSpans",
	.page = [] {
		return WithDecoratedCell(
			Page({ Table(u"T"_q, {
				{ { u"p"_q, 1, 3 }, { u"q"_q }, { u"r"_q }, { u"s"_q } },
				{ { u"x"_q }, { u"a"_q, 1, 2 }, { u"b"_q, 1, 2 } },
				{ { u"c"_q } },
			}) }),
			1,
			1);
	},
	.text = R"(T"T"{"p"*r3 "q" "r" "s"|"x" "a"*r2 "b"*r2|"c"})",
	.state = R"(0=text:0"T" ; live T0 nodes=9)",
	.grid = R"("p"@r0-3c0-1 "q"@r0-1c1-2 "r"@r0-1c2-3 "s"@r0-1c3-4)"
		R"( "x"@r1-2c1-2 "a"@r1-3c2-3 "b"@r1-3c3-4 "c"@r2-3c1-2)",
};
constexpr auto kSpanGap = Fixture{
	.name = "spanGap",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"x"_q }, { u"a"_q, 1, 2 } },
			{},
		}) });
	},
	.text = R"(T"T"{"x" "a"*r2|})",
	.state = R"(0=text:0"T" ; live T0 nodes=3)",
	.grid = R"("x"@r0-1c0-1 "a"@r0-2c1-2)",
};
constexpr auto kUnorderedTarget = Fixture{
	.name = "unorderedTarget",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"x"_q }, { u"m"_q, 1, 2 }, { u"y"_q }, { u"z"_q } },
			{ { u"w"_q, 2 }, { u"v"_q } },
		}) });
	},
	.text = R"(T"T"{"x" "m"*r2 "y" "z"|"w"*c2 "v"})",
	.state = R"(0=text:0"T" ; live T0 nodes=7)",
	.grid = R"("x"@r0-1c0-1 "m"@r0-2c1-2 "y"@r0-1c2-3 "z"@r0-1c3-4)"
		R"( "w"@r1-2c2-4 "v"@r1-2c0-1)",
};
constexpr auto kLongSpan = Fixture{
	.name = "longSpan",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"a"_q, 1, 99 }, { u"b"_q } },
			{ { u"c"_q } },
		}) });
	},
	.text = R"(T"T"{"a"*r99 "b"|"c"})",
	.state = R"(0=text:0"T" ; live T0 nodes=4)",
	.grid = R"("a"@r0-2c0-1 "b"@r0-1c1-2 "c"@r1-2c1-2)",
};
constexpr auto kUnplacedSpan = Fixture{
	.name = "unplacedSpan",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"x"_q }, { u"a"_q, 1, 2 } },
			{ { u"c"_q } },
		}) });
	},
	.limits = { .maxTableCols = 1 },
	.text = R"(T"T"{"x" "a"*r2|"c"})",
	.state = R"(0=text:0"T" ; live T0 nodes=4)",
	.grid = R"("x"@r0-1c0-1 "c"@r1-2c0-1)",
};
constexpr auto kOmittedSpan = Fixture{
	.name = "omittedSpan",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"x"_q, 20 }, { u"a"_q, 1, 3 } },
			{},
			{ { u"d"_q } },
		}) });
	},
	.text = R"(T"T"{"x"*c20 "a"*r3||"d"})",
	.state = R"(0=text:0"T" ; live T0 nodes=4)",
	.grid = R"("x"@r0-1c0-20 "d"@r2-3c0-1)",
};
constexpr auto kOmittedSpanOneColumn = Fixture{
	.name = "omittedSpanOneColumn",
	.page = [] {
		return Page({ Table(u"T"_q, {
			{ { u"x"_q }, { u"a"_q, 1, 3 } },
			{},
			{ { u"d"_q } },
		}) });
	},
	.limits = { .maxTableCols = 1 },
	.text = R"(T"T"{"x" "a"*r3||"d"})",
	.state = R"(0=text:0"T" ; live T0 nodes=4)",
	.grid = R"("x"@r0-1c0-1 "d"@r2-3c0-1)",
};
constexpr auto kTwoLists = Fixture{
	.name = "twoLists",
	.page = [] {
		return Page({
			List({ u"a0"_q, u"a1"_q }),
			Paragraph(u"M"_q),
			List({ u"b0"_q, u"b1"_q }),
		});
	},
	.text = R"(L["a0" "a1"] P"M" L["b0" "b1"])",
	.state = kTwoListsAtA0,
};
constexpr auto kListFirst = Fixture{
	.name = "listFirst",
	.page = [] {
		return Page({
			List({
				Item(u"i0"_q),
				Item(u"i1"_q, Paragraphs({ u"x1"_q })),
				Item(u"i2"_q),
			}),
			Paragraph(u"C"_q),
		});
	},
	.text = R"(L["i0" "i1"{P"x1"} "i2"] P"C")",
	.state = R"(0=item:0#0"i0" ; live L0 P1 nodes=5)",
};
constexpr auto kMixedLists = Fixture{
	.name = "mixedLists",
	.page = [] {
		return Page({
			List({ u"a0"_q, u"a1"_q }),
			Paragraph(u"M"_q),
			List(
				{ Item(u"b0"_q), Item(u"b1"_q), Item(u"b2"_q) },
				ListKind::Ordered),
		});
	},
	.text = R"(L["a0" "a1"] P"M" LO["b0" "b1" "b2"])",
	.state = R"(0=item:0#0"a0" ; live L0 P1 L2 nodes=6)",
};
constexpr auto kWithU = Fixture{
	.name = "withU",
	.page = [] { return Framed(Bare(BlockKind::Unsupported)); },
	.text = R"(P"A" U P"C")",
	.state = R"(0=text:0"A" ; live P0 P2 nodes=2)",
};
constexpr auto kQuoteDepth = Fixture{
	.name = "quoteDepth",
	.page = [] { return Framed(Quote(Paragraphs({ u"q0"_q }), u"cap"_q)); },
	.limits = { .maxDepth = 1 },
	.text = R"(P"A" Q""[P"q0"]/"cap" P"C")",
	.state = R"(0=text:0"A" ; live P0 Q1 P2 nodes=4)",
};
constexpr auto kEmptyQuote = Fixture{
	.name = "emptyQuote",
	.page = [] { return Framed(Quote({}, u"cap"_q, u"qt"_q)); },
	.limits = { .maxDepth = 1 },
	.text = R"(P"A" Q"qt"[]/"cap" P"C")",
	.state = R"(0=text:0"A" ; live P0 Q1 P2 nodes=4)",
};
constexpr auto kWithTdp = Fixture{
	.name = "withTdp",
	.page = [] { return Flat({ u"A"_q, QString(), u"C"_q }); },
	.temporaryParagraph = true,
	.text = R"(P"A" P"" P"C")",
	.state = kTemporaryAtEmpty,
};
constexpr auto kWithDivider = Fixture{
	.name = "withDivider",
	.page = [] { return Framed(Bare(BlockKind::Divider)); },
	.text = R"(P"A" V P"C")",
	.state = R"(0=text:0"A" ; live P0 V1 P2 nodes=2)",
};
constexpr auto kFlat4ThreeBlocks = Fixture{
	.name = "flat4",
	.page = kFlat4.page,
	.limits = { .maxBlocks = 3 },
};

[[nodiscard]] PreparedEditBlockContainerPath Root() {
	return {};
}

[[nodiscard]] PreparedEditBlockContainerPath Step(
		PreparedEditBlockContainerKind kind,
		int block,
		int item) {
	auto result = PreparedEditBlockContainerPath();
	result.steps.push_back({
		.kind = kind,
		.blockIndex = block,
		.listItemIndex = item,
	});
	return result;
}

[[nodiscard]] PreparedEditBlockContainerPath Children(int block) {
	return Step(PreparedEditBlockContainerKind::BlockChildren, block, -1);
}

[[nodiscard]] PreparedEditBlockContainerPath ItemChildren(int block, int item) {
	return Step(PreparedEditBlockContainerKind::ListItemChildren, block, item);
}

[[nodiscard]] PreparedEditSelection Blocks(
		PreparedEditBlockContainerPath container,
		int from,
		int till) {
	auto result = PreparedEditSelection();
	result.kind = PreparedEditSelectionKind::Blocks;
	result.blocks = {
		.container = std::move(container),
		.from = from,
		.till = till,
	};
	return result;
}

[[nodiscard]] PreparedEditSelection Items(int block, int from, int till) {
	auto result = PreparedEditSelection();
	result.kind = PreparedEditSelectionKind::ListItems;
	result.listItems = {
		.block = { .index = block },
		.from = from,
		.till = till,
	};
	return result;
}

[[nodiscard]] PreparedEditSelection Rows(int block, int from, int till) {
	auto result = PreparedEditSelection();
	result.kind = PreparedEditSelectionKind::TableRows;
	result.tableRows = {
		.block = { .index = block },
		.from = from,
		.till = till,
	};
	return result;
}

[[nodiscard]] PreparedEditSelection Cells(
		int block,
		int rowFrom,
		int rowTill,
		int columnFrom,
		int columnTill) {
	auto result = PreparedEditSelection();
	result.kind = PreparedEditSelectionKind::TableCells;
	result.tableCells = {
		.block = { .index = block },
		.rowFrom = rowFrom,
		.rowTill = rowTill,
		.columnFrom = columnFrom,
		.columnTill = columnTill,
	};
	return result;
}

[[nodiscard]] PreparedEditDropTarget BlockTarget(
		PreparedEditBlockContainerPath container,
		int insertIndex) {
	return PreparedEditBlockDropTarget{
		.container = std::move(container),
		.insertIndex = insertIndex,
	};
}

[[nodiscard]] PreparedEditDropTarget ListTarget(int block, int insertIndex) {
	return PreparedEditListItemDropTarget{
		.block = { .index = block },
		.insertIndex = insertIndex,
	};
}

[[nodiscard]] Outcome Failed() {
	return { .result = Result::Failed };
}

[[nodiscard]] Outcome Unchanged() {
	return { .result = Result::Unchanged };
}

[[nodiscard]] Outcome ChangedText(int ordinal) {
	return {
		.result = Result::Changed,
		.destination = { .action = Action::Text, .textOrdinal = ordinal },
	};
}

[[nodiscard]] Outcome ChangedSelection(PreparedEditSelection selection) {
	return {
		.result = Result::Changed,
		.destination = {
			.action = Action::StructuralSelection,
			.structuralSelection = std::move(selection),
		},
	};
}

[[nodiscard]] LeafPath TemporaryLeaf() {
	return { .kind = LeafKind::BlockText, .block = { .index = 1 } };
}

[[nodiscard]] State MakeState(const Fixture &fixture) {
	return State(
		std::make_shared<RichPage>(fixture.page()),
		nullptr,
		fixture.limits);
}

[[nodiscard]] bool Activate(
		Expect &expect,
		State &state,
		const Fixture &fixture,
		int active) {
	if (!fixture.temporaryParagraph) {
		const auto activated = state.setActiveTextByOrdinal(active);
		expect.same(
			"setup",
			activated,
			u"no ordinal %1"_q.arg(active),
			u"ordinal %1"_q.arg(active));
		return activated;
	}
	auto snapshot = state.snapshot();
	snapshot.activeLeaf = TemporaryLeaf();
	snapshot.temporaryDownParagraph = TemporaryLeaf();
	state.restoreSnapshot(std::move(snapshot));
	const auto temporary = state.snapshot().temporaryDownParagraph;
	const auto restored = (temporary == TemporaryLeaf());
	expect.same(
		"setup temporary",
		restored,
		OptionalLeafText(temporary),
		LeafText(TemporaryLeaf()));
	return restored;
}

[[nodiscard]] Observed Observe(const State &state) {
	return {
		.snapshot = state.snapshot(),
		.page = &state.richPage(),
		.ordinal = state.activeTextOrdinal(),
		.text = state.activeText(),
		.summary = PreparedSummary(state),
		.leaves = PreparedLeaves(state),
	};
}

void ExpectAfter(
		Expect &expect,
		const State &state,
		const RichPage &page,
		const char *summary,
		const char *leaves,
		bool temporary) {
	expect.page("page", state.richPage(), page);
	expect.text("state", StateText(state), Latin(summary));
	if (leaves) {
		expect.text("prepared leaves", PreparedLeaves(state), Latin(leaves));
	}
	const auto got = state.snapshot().temporaryDownParagraph;
	const auto expected = temporary
		? std::make_optional(TemporaryLeaf())
		: std::nullopt;
	expect.same(
		"temporary",
		got == expected,
		OptionalLeafText(got),
		OptionalLeafText(expected));
}

void ExpectEqualState(
		Expect &expect,
		const State &state,
		const Observed &before) {
	const auto leaf = state.activeLeafPath();
	const auto text = state.activeText();
	expect.page("page vs before", state.richPage(), before.snapshot.richPage);
	expect.same(
		"leaf vs before",
		leaf == before.snapshot.activeLeaf,
		OptionalLeafText(leaf),
		OptionalLeafText(before.snapshot.activeLeaf));
	expect.number(
		"ordinal vs before",
		state.activeTextOrdinal(),
		before.ordinal);
	expect.same(
		"text vs before",
		text == before.text,
		text.text,
		before.text.text);
	expect.text("summary vs before", PreparedSummary(state), before.summary);
	expect.text("leaves vs before", PreparedLeaves(state), before.leaves);
}

void ExpectPageObject(
		Expect &expect,
		const State &state,
		const Observed &before,
		bool replaced) {
	const auto kept = (&state.richPage() == before.page);
	expect.same(
		"page object",
		kept != replaced,
		kept ? u"kept"_q : u"replaced"_q,
		replaced ? u"replaced"_q : u"kept"_q);
}

void CheckRemoval(Expect &expect, State &state, const RemovalCase &test) {
	const auto &selection = test.start.selection;
	const auto before = Observe(state);
	const auto accepted = state.canRemoveStructuralSelection(selection);
	const auto returned = state.removeStructuralSelection(
		selection,
		test.forward);
	expect.same(
		"can remove",
		accepted == test.returned.has_value(),
		accepted ? u"yes"_q : u"no"_q,
		test.returned ? u"yes"_q : u"no"_q);
	expect.text(
		"returned",
		ReturnedText(returned),
		ReturnedText(test.returned));
	ExpectAfter(expect, state, test.page, test.state, test.leaves, false);
	if (test.spans) {
		expect.text("prepared spans", PreparedSpans(state), Latin(test.spans));
	}
	if (test.grid) {
		expect.text(
			"grid",
			GridText(state.richPage(), test.start.fixture.limits),
			Latin(test.grid));
	}
	if (!test.returned) {
		ExpectPageObject(expect, state, before, false);
		ExpectEqualState(expect, state, before);
	}
}

void CheckMenuRemoval(Expect &expect, State &state, const RemovalCase &test) {
	const auto &start = test.start;
	auto twin = MakeState(start.fixture);
	if (!Activate(expect, twin, start.fixture, start.active)) {
		return;
	}
	CheckRemoval(expect, twin, test);
	const auto &rows = start.selection.tableRows;
	auto range = PreparedEditTableCellRange{
		.block = rows.block,
		.rowFrom = rows.from,
		.rowTill = rows.till,
		.columnFrom = 0,
		.columnTill = 1,
	};
	range.columnTill = state.tableSelectionInfo(range).totalColumns;
	const auto removed = state.removeTableRows(range);
	expect.same(
		"menu removed",
		removed,
		removed ? u"yes"_q : u"no"_q,
		u"yes"_q);
	expect.page("menu page", state.richPage(), twin.richPage());
	expect.text("menu state", StateText(state), StateText(twin));
}

void ExpectDestination(
		Expect &expect,
		const State &state,
		const State::BoundaryTarget &got,
		const State::BoundaryTarget &expected) {
	expect.text("action", ActionText(got.action), ActionText(expected.action));
	expect.number("destination ordinal", got.textOrdinal, expected.textOrdinal);
	expect.same(
		"destination selection",
		got.structuralSelection == expected.structuralSelection,
		SelectionText(got.structuralSelection),
		SelectionText(expected.structuralSelection));
	if (expected.action == Action::Text) {
		expect.number(
			"active ordinal",
			state.activeTextOrdinal(),
			expected.textOrdinal);
	}
}

void CheckDrop(Expect &expect, State &state, const DropCase &test) {
	const auto &expected = test.outcome;
	const auto before = Observe(state);
	const auto dropped = state.moveStructuralSelectionToDropTarget(
		test.start.selection,
		test.target);
	expect.text(
		"result",
		ResultText(dropped.result),
		ResultText(expected.result));
	ExpectDestination(expect, state, dropped.destination, expected.destination);
	expect.text(
		"limit error",
		LimitText(state.lastLimitError()),
		LimitText(test.limit));
	ExpectAfter(
		expect,
		state,
		test.page,
		test.state,
		test.leaves,
		test.temporary);
	const auto changed = (expected.result == Result::Changed);
	ExpectPageObject(expect, state, before, changed);
	if (changed) {
		return;
	}
	const auto temporary = state.snapshot().temporaryDownParagraph;
	ExpectEqualState(expect, state, before);
	expect.same(
		"temporary vs before",
		temporary == before.snapshot.temporaryDownParagraph,
		OptionalLeafText(temporary),
		OptionalLeafText(before.snapshot.temporaryDownParagraph));
}

void ExpectRestored(
		Expect &expect,
		State &state,
		const Observed &before,
		const Fixture &fixture,
		const char *summary) {
	const auto &snapshot = before.snapshot;
	state.restoreSnapshot(snapshot);
	const auto restored = state.snapshot();
	expect.page("restored page", restored.richPage, snapshot.richPage);
	expect.same(
		"restored leaf",
		restored.activeLeaf == snapshot.activeLeaf,
		OptionalLeafText(restored.activeLeaf),
		OptionalLeafText(snapshot.activeLeaf));
	expect.same(
		"restored temporary",
		restored.temporaryDownParagraph == snapshot.temporaryDownParagraph,
		OptionalLeafText(restored.temporaryDownParagraph),
		OptionalLeafText(snapshot.temporaryDownParagraph));
	ExpectAfter(expect, state, fixture.page(), summary, nullptr, false);
	ExpectEqualState(expect, state, before);
}

template <typename Case>
[[nodiscard]] const char *RowId(const Case &test) {
	if constexpr (requires(const Case &row) { row.start.id; }) {
		return test.start.id;
	} else {
		return test.id;
	}
}

template <typename Case, typename Check>
[[nodiscard]] Report RunRows(const std::vector<Case> &cases, Check check) {
	auto report = Report();
	for (const auto &test : cases) {
		auto expect = Expect(report.failures, RowId(test));
		++report.cases;
		check(expect, test);
	}
	return report;
}

template <typename Case>
[[nodiscard]] Report RunCases(
		const std::vector<Case> &cases,
		void (*check)(Expect &, State &, const Case &)) {
	return RunRows(cases, [&](Expect &expect, const Case &test) {
		const auto &start = test.start;
		auto state = MakeState(start.fixture);
		if (Activate(expect, state, start.fixture, start.active)) {
			check(expect, state, test);
		}
	});
}

[[nodiscard]] Report RunRemovals(const std::vector<RemovalCase> &cases) {
	return RunCases(cases, CheckRemoval);
}

[[nodiscard]] Report RunDrops(const std::vector<DropCase> &cases) {
	return RunCases(cases, CheckDrop);
}

[[nodiscard]] Report SetupFixtures() {
	const auto fixtures = std::array{
		kFlat4,
		kFlat5,
		kQuoted,
		kListed,
		kListed4,
		kNested,
		kTabled,
		kTabled3,
		kUntitled,
		kColMerged,
		kRowMerged,
		kRowSpanRows,
		kRowSpanThree,
		kEmptyTarget,
		kTrailingSpans,
		kSpanGap,
		kUnorderedTarget,
		kLongSpan,
		kUnplacedSpan,
		kOmittedSpan,
		kOmittedSpanOneColumn,
		kTwoLists,
		kListFirst,
		kMixedLists,
		kWithU,
		kQuoteDepth,
		kEmptyQuote,
		kWithTdp,
		kWithDivider,
	};
	auto report = Report();
	for (const auto &fixture : fixtures) {
		auto expect = Expect(report.failures, fixture.name);
		auto state = MakeState(fixture);
		++report.cases;
		if (!Activate(expect, state, fixture, 0)) {
			continue;
		}
		expect.text("text", PageText(state.richPage()), Latin(fixture.text));
		ExpectAfter(
			expect,
			state,
			fixture.page(),
			fixture.state,
			nullptr,
			fixture.temporaryParagraph);
		if (fixture.grid) {
			expect.text(
				"grid",
				GridText(state.richPage(), fixture.limits),
				Latin(fixture.grid));
		}
	}
	return report;
}

[[nodiscard]] Report RemovalBlocksRangeAndDirection() {
	return RunRemovals({
		{
			.start = { "R01", kFlat4, 0, Blocks(Root(), 1, 3) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"D"_q }),
			.state = R"(1=text:1"D" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R02", kFlat4, 0, Blocks(Root(), 1, 3) },
			.forward = false,
			.returned = 0,
			.page = Flat({ u"A"_q, u"D"_q }),
			.state = R"(0=text:0"A" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R03", kFlat4, 0, Blocks(Root(), 0, 1) },
			.returned = 0,
			.page = Flat({ u"B"_q, u"C"_q, u"D"_q }),
			.state = R"(0=text:0"B" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R04", kFlat4, 0, Blocks(Root(), 3, 4) },
			.forward = false,
			.returned = 2,
			.page = Flat({ u"A"_q, u"B"_q, u"C"_q }),
			.state = R"(2=text:2"C" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R05", kFlat4, 0, Blocks(Root(), 0, 4) },
			.returned = 0,
			.page = Flat({ QString() }),
			.state = R"(0=text:0"" ; live P0 nodes=1)",
		},
		{
			.start = { "R06", kFlat4, 2, Blocks(Root(), 0, 4) },
			.forward = false,
			.returned = 0,
			.page = Flat({ QString() }),
			.state = R"(0=text:0"" ; live P0 nodes=1)",
		},
	});
}

[[nodiscard]] Report RemovalBlocksActiveLeafKept() {
	return RunRemovals({
		{
			.start = { "R07", kFlat4, 1, Blocks(Root(), 2, 4) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"B"_q }),
			.state = R"(1=text:1"B" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R08", kFlat4, 0, Blocks(Root(), 3, 4) },
			.returned = 0,
			.page = Flat({ u"A"_q, u"B"_q, u"C"_q }),
			.state = R"(0=text:0"A" ; live P0 P1 P2 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalBlocksActiveLeafRemoved() {
	return RunRemovals({
		{
			.start = { "R09", kFlat4, 2, Blocks(Root(), 2, 4) },
			.returned = 0,
			.page = Flat({ u"A"_q, u"B"_q }),
			.state = R"(0=text:0"A" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R10", kFlat4, 3, Blocks(Root(), 0, 1) },
			.forward = false,
			.returned = 2,
			.page = Flat({ u"B"_q, u"C"_q, u"D"_q }),
			.state = R"(2=text:2"D" ; live P0 P1 P2 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalBlocksLeadingPlannedFocus() {
	const auto lastThree = Flat({ u"C"_q, u"D"_q, u"E"_q });
	return RunRemovals({
		{
			.start = { "R11", kFlat4, 0, Blocks(Root(), 0, 1) },
			.forward = false,
			.returned = 0,
			.page = Flat({ u"B"_q, u"C"_q, u"D"_q }),
			.state = R"(0=text:0"B" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R12", kFlat5, 2, Blocks(Root(), 0, 2) },
			.forward = false,
			.returned = 0,
			.page = lastThree,
			.state = R"(0=text:0"C" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R13", kFlat5, 0, Blocks(Root(), 0, 2) },
			.forward = false,
			.returned = 0,
			.page = lastThree,
			.state = R"(0=text:0"C" ; live P0 P1 P2 nodes=3)",
		},
		{
			.start = { "R14", kFlat5, 4, Blocks(Root(), 0, 2) },
			.forward = false,
			.returned = 2,
			.page = lastThree,
			.state = R"(2=text:2"E" ; live P0 P1 P2 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalQuoteChildren() {
	const auto quoted = [](std::initializer_list<QString> texts) {
		return Framed(Quote(Paragraphs(texts), u"cap"_q));
	};
	return RunRemovals({
		{
			.start = { "R15", kQuoted, 0, Blocks(Children(1), 1, 2) },
			.returned = 2,
			.page = quoted({ u"q0"_q, u"q2"_q }),
			.state = R"(2=text:1/1"q2" ; live P0 Q1 P2 nodes=5)",
			.leaves = R"(text:0"A" text:1/0"q0" text:1/1"q2" caption:1"cap")"
				R"( text:2"C")",
		},
		{
			.start = { "R16", kQuoted, 0, Blocks(Children(1), 1, 2) },
			.forward = false,
			.returned = 1,
			.page = quoted({ u"q0"_q, u"q2"_q }),
			.state = R"(1=text:1/0"q0" ; live P0 Q1 P2 nodes=5)",
		},
		{
			.start = { "R17", kQuoted, 1, Blocks(Children(1), 0, 1) },
			.forward = false,
			.returned = 1,
			.page = quoted({ u"q1"_q, u"q2"_q }),
			.state = R"(1=text:1/0"q1" ; live P0 Q1 P2 nodes=5)",
		},
		{
			.start = { "R18", kQuoted, 0, Blocks(Children(1), 2, 3) },
			.returned = 1,
			.page = quoted({ u"q0"_q, u"q1"_q }),
			.state = R"(1=text:1/0"q0" ; live P0 Q1 P2 nodes=5)",
		},
		{
			.start = { "R19", kQuoted, 0, Blocks(Children(1), 0, 3) },
			.returned = 1,
			.page = quoted({}),
			.state = R"(1=text:1"" ; live P0 Q1 P2 nodes=4)",
		},
		{
			.start = { "R20", kQuoted, 0, Blocks(Children(1), 0, 3) },
			.forward = false,
			.returned = 1,
			.page = quoted({}),
			.state = R"(1=text:1"" ; live P0 Q1 P2 nodes=4)",
		},
		{
			.start = { "R21", kQuoted, 2, Blocks(Root(), 1, 2) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(1=text:1"C" ; live P0 P1 nodes=2)",
		},
	});
}

[[nodiscard]] Report RemovalListItems() {
	return RunRemovals({
		{
			.start = { "R22", kListed, 0, Items(1, 1, 2) },
			.returned = 2,
			.page = Framed(List({ u"i0"_q, u"i2"_q })),
			.state = R"(2=item:1#1"i2" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R23", kListed, 0, Items(1, 1, 2) },
			.forward = false,
			.returned = 1,
			.page = Framed(List({ u"i0"_q, u"i2"_q })),
			.state = R"(1=item:1#0"i0" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R24", kListed, 1, Items(1, 0, 1) },
			.returned = 1,
			.page = Framed(List({ u"i1"_q, u"i2"_q })),
			.state = R"(1=item:1#0"i1" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R25", kListed, 1, Items(1, 0, 1) },
			.forward = false,
			.returned = 0,
			.page = Framed(List({ u"i1"_q, u"i2"_q })),
			.state = R"(0=text:0"A" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R26", kListed, 3, Items(1, 2, 3) },
			.returned = 3,
			.page = Framed(List({ u"i0"_q, u"i1"_q })),
			.state = R"(3=text:2"C" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R27", kListed, 3, Items(1, 2, 3) },
			.forward = false,
			.returned = 2,
			.page = Framed(List({ u"i0"_q, u"i1"_q })),
			.state = R"(2=item:1#1"i1" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R28", kListed, 2, Items(1, 0, 3) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(1=text:1"C" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R29", kListed, 2, Items(1, 0, 3) },
			.forward = false,
			.returned = 0,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(0=text:0"A" ; live P0 P1 nodes=2)",
		},
	});
}

[[nodiscard]] Report RemovalListItemsLeadingPlannedFocus() {
	const auto lastItems = Page({
		List({
			Item(u"i1"_q, Paragraphs({ u"x1"_q })),
			Item(u"i2"_q),
		}),
		Paragraph(u"C"_q),
	});
	const auto withoutList = Page({
		Paragraph(u"M"_q),
		List({ u"b0"_q, u"b1"_q }),
	});
	return RunRemovals({
		{
			.start = { "R68", kListFirst, 3, Items(0, 0, 1) },
			.forward = false,
			.returned = 2,
			.page = lastItems,
			.state = R"(2=item:0#1"i2" ; live L0 P1 nodes=4)",
		},
		{
			.start = { "R69", kListFirst, 0, Items(0, 0, 1) },
			.forward = false,
			.returned = 0,
			.page = lastItems,
			.state = R"(0=item:0#0"i1" ; live L0 P1 nodes=4)",
		},
		{
			.start = { "R70", kListFirst, 2, Items(0, 0, 1) },
			.forward = false,
			.returned = 1,
			.page = lastItems,
			.state = R"(1=text:0.0/0"x1" ; live L0 P1 nodes=4)",
		},
		{
			.start = { "R71", kTwoLists, 2, Items(0, 0, 2) },
			.forward = false,
			.returned = 0,
			.page = withoutList,
			.state = R"(0=text:0"M" ; live P0 L1 nodes=3)",
		},
		{
			.start = { "R72", kTwoLists, 1, Items(0, 0, 2) },
			.forward = false,
			.returned = 0,
			.page = withoutList,
			.state = R"(0=text:0"M" ; live P0 L1 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalListItemChildren() {
	return RunRemovals({
		{
			.start = { "R30", kNested, 0, Blocks(ItemChildren(1, 0), 0, 1) },
			.returned = 2,
			.page = Framed(List({
				Item(u"i0"_q, Paragraphs({ u"x1"_q })),
				Item(u"i1"_q),
			})),
			.state = R"(2=text:1.0/0"x1" ; live P0 L1 P2 nodes=5)",
		},
		{
			.start = { "R31", kNested, 0, Blocks(ItemChildren(1, 0), 0, 2) },
			.forward = false,
			.returned = 1,
			.page = Framed(List({ u"i0"_q, u"i1"_q })),
			.state = R"(1=item:1#0"i0" ; live P0 L1 P2 nodes=4)",
		},
		{
			.start = { "R32", kNested, 0, Items(1, 0, 1) },
			.returned = 1,
			.page = Framed(List({ u"i1"_q })),
			.state = R"(1=item:1#0"i1" ; live P0 L1 P2 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalTableRows() {
	const auto withoutMiddle = Framed(u"T"_q, Grid({ 0, 2 }, { 0, 1 }));
	return RunRemovals({
		{
			.start = { "R33", kTabled, 0, Rows(1, 1, 2) },
			.returned = 4,
			.page = withoutMiddle,
			.state = R"(4=cell:1#1,0"r2c0" ; live P0 T1 P2 nodes=7)",
		},
		{
			.start = { "R34", kTabled, 0, Rows(1, 1, 2) },
			.forward = false,
			.returned = 3,
			.page = withoutMiddle,
			.state = R"(3=cell:1#0,1"r0c1" ; live P0 T1 P2 nodes=7)",
		},
		{
			.start = { "R35", kTabled, 0, Rows(1, 0, 1) },
			.forward = false,
			.returned = 2,
			.page = Framed(u"T"_q, Grid({ 1, 2 }, { 0, 1 })),
			.state = R"(2=cell:1#0,0"r1c0" ; live P0 T1 P2 nodes=7)",
		},
		{
			.start = { "R36", kTabled, 0, Rows(1, 2, 3) },
			.returned = 5,
			.page = Framed(u"T"_q, Grid({ 0, 1 }, { 0, 1 })),
			.state = R"(5=cell:1#1,1"r1c1" ; live P0 T1 P2 nodes=7)",
		},
		{
			.start = { "R37", kTabled, 0, Rows(1, 0, 3) },
			.returned = 1,
			.page = Framed(u"T"_q, {}),
			.state = R"(1=text:1"T" ; live P0 T1 P2 nodes=3)",
		},
		{
			.start = { "R38", kUntitled, 0, Rows(1, 0, 2) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(1=text:1"C" ; live P0 P1 nodes=2)",
		},
		{
			.start = { "R39", kTabled, 4, Rows(1, 1, 2) },
			.returned = 4,
			.page = withoutMiddle,
			.state = R"(4=cell:1#1,0"r2c0" ; live P0 T1 P2 nodes=7)",
		},
	});
}

[[nodiscard]] RemovalCase RowSpanMovedRemoval(const char *id) {
	return {
		.start = { id, kRowSpanRows, 0, Rows(0, 0, 1) },
		.returned = 1,
		.page = Page({ Table(u"T"_q, {
			{ { u"a"_q }, { u"c"_q } },
			{ { u"d"_q }, { u"e"_q } },
		}) }),
		.state = R"(1=cell:0#0,0"a" ; live T0 nodes=5)",
		.spans = "1x1 1x1|1x1 1x1",
		.grid = R"("a"@r0-1c0-1 "c"@r0-1c1-2 "d"@r1-2c0-1 "e"@r1-2c1-2)",
	};
}

[[nodiscard]] RemovalCase RowSpanShortenedRemoval(const char *id) {
	return {
		.start = { id, kRowSpanRows, 0, Rows(0, 1, 2) },
		.returned = 3,
		.page = Page({ Table(u"T"_q, {
			{ { u"a"_q }, { u"b"_q } },
			{ { u"d"_q }, { u"e"_q } },
		}) }),
		.state = R"(3=cell:0#1,0"d" ; live T0 nodes=5)",
		.spans = "1x1 1x1|1x1 1x1",
		.grid = R"("a"@r0-1c0-1 "b"@r0-1c1-2 "d"@r1-2c0-1 "e"@r1-2c1-2)",
	};
}

[[nodiscard]] RemovalCase OmittedSpanRemoval(const char *id) {
	return {
		.start = { id, kOmittedSpan, 0, Rows(0, 0, 1) },
		.returned = 1,
		.page = Page({ Table(u"T"_q, {
			{ { QString() }, { u"a"_q, 1, 2 } },
			{ { u"d"_q } },
		}) }),
		.state = R"(1=cell:0#0,0"" ; live T0 nodes=4)",
		.spans = "1x1 1x2|1x1",
		.grid = R"(""@r0-1c0-1 "a"@r0-2c1-2 "d"@r1-2c0-1)",
	};
}

[[nodiscard]] Report RemovalTableRowsMerged() {
	return RunRemovals({
		RowSpanMovedRemoval("R40"),
		RowSpanShortenedRemoval("R73"),
		{
			.start = { "R74", kRowSpanThree, 0, Rows(0, 1, 2) },
			.returned = 3,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q, 1, 2 }, { u"b"_q } },
				{ { u"d"_q } },
				{ { u"e"_q }, { u"f"_q } },
			}) }),
			.state = R"(3=cell:0#1,0"d" ; live T0 nodes=6)",
			.spans = "1x2 1x1|1x1|1x1 1x1",
			.grid = R"("a"@r0-2c0-1 "b"@r0-1c1-2 "d"@r1-2c1-2)"
				R"( "e"@r2-3c0-1 "f"@r2-3c1-2)",
		},
		{
			.start = { "R75", kRowSpanThree, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q, 1, 2 }, { u"c"_q } },
				{ { u"d"_q } },
				{ { u"e"_q }, { u"f"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"a" ; live T0 nodes=6)",
			.spans = "1x2 1x1|1x1|1x1 1x1",
			.grid = R"("a"@r0-2c0-1 "c"@r0-1c1-2 "d"@r1-2c1-2)"
				R"( "e"@r2-3c0-1 "f"@r2-3c1-2)",
		},
		{
			.start = { "R76", kRowMerged, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"c"_q }, { u"m"_q }, { u"d"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"c" ; live T0 nodes=4)",
			.spans = "1x1 1x1 1x1",
			.grid = R"("c"@r0-1c0-1 "m"@r0-1c1-2 "d"@r0-1c2-3)",
		},
		{
			.start = { "R77", kRowSpanThree, 0, Rows(0, 1, 3) },
			.returned = 3,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q }, { u"b"_q } },
				{ { u"e"_q }, { u"f"_q } },
			}) }),
			.state = R"(3=cell:0#1,0"e" ; live T0 nodes=5)",
			.spans = "1x1 1x1|1x1 1x1",
			.grid = R"("a"@r0-1c0-1 "b"@r0-1c1-2 "e"@r1-2c0-1 "f"@r1-2c1-2)",
		},
		{
			.start = { "R80", kEmptyTarget, 0, Rows(0, 0, 2) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q, 2 }, { u"b"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"a" ; live T0 nodes=3)",
			.spans = "2x1 1x1",
			.grid = R"("a"@r0-1c0-2 "b"@r0-1c2-3)",
		},
		{
			.start = { "R81", kTrailingSpans, 0, Rows(0, 1, 2) },
			.returned = 5,
			.page = WithDecoratedCell(
				Page({ Table(u"T"_q, {
					{ { u"p"_q, 1, 2 }, { u"q"_q }, { u"r"_q }, { u"s"_q } },
					{ { u"c"_q }, { u"a"_q }, { u"b"_q } },
				}) }),
				1,
				1),
			.state = R"(5=cell:0#1,0"c" ; live T0 nodes=8)",
			.spans = "1x2 1x1 1x1 1x1|1x1 1x1 1x1",
			.grid = R"("p"@r0-2c0-1 "q"@r0-1c1-2 "r"@r0-1c2-3 "s"@r0-1c3-4)"
				R"( "c"@r1-2c1-2 "a"@r1-2c2-3 "b"@r1-2c3-4)",
		},
		{
			.start = { "R82", kSpanGap, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { QString() }, { u"a"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"" ; live T0 nodes=3)",
			.spans = "1x1 1x1",
			.grid = R"(""@r0-1c0-1 "a"@r0-1c1-2)",
		},
		{
			.start = { "R83", kUnorderedTarget, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"v"_q }, { u"m"_q }, { u"w"_q, 2 } },
			}) }),
			.state = R"(1=cell:0#0,0"v" ; live T0 nodes=4)",
			.spans = "1x1 1x1 2x1",
			.grid = R"("v"@r0-1c0-1 "m"@r0-1c1-2 "w"@r0-1c2-4)",
		},
		{
			.start = { "R84", kLongSpan, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q, 1, 98 }, { u"c"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"a" ; live T0 nodes=3)",
			.spans = "1x1 1x1",
			.grid = R"("a"@r0-1c0-1 "c"@r0-1c1-2)",
		},
		{
			.start = { "R85", kUnplacedSpan, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { u"c"_q }, { u"a"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"c" ; live T0 nodes=3)",
			.spans = "1x1",
			.grid = R"("c"@r0-1c0-1)",
		},
		OmittedSpanRemoval("R86"),
		{
			.start = { "R87", kOmittedSpanOneColumn, 0, Rows(0, 0, 1) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, {
				{ { QString() }, { u"a"_q, 1, 2 } },
				{ { u"d"_q } },
			}) }),
			.state = R"(1=cell:0#0,0"" ; live T0 nodes=4)",
			.spans = "1x1|1x1",
			.grid = R"(""@r0-1c0-1 "d"@r1-2c0-1)",
		},
	});
}

[[nodiscard]] Report RemovalTableRowsMenu() {
	const auto cases = std::vector<RemovalCase>{
		RowSpanMovedRemoval("R78"),
		RowSpanShortenedRemoval("R79"),
		OmittedSpanRemoval("R88"),
	};
	return RunCases(cases, CheckMenuRemoval);
}

[[nodiscard]] Report RemovalTableColumns() {
	const auto withoutMiddle = Framed(u"T"_q, Grid({ 0, 1, 2 }, { 0, 2 }));
	const auto withoutLast = Framed(u"T"_q, Grid({ 0, 1, 2 }, { 0, 1 }));
	const auto withoutFirst = Framed(u"T"_q, Grid({ 0, 1, 2 }, { 1, 2 }));
	return RunRemovals({
		{
			.start = { "R41", kTabled3, 0, Cells(1, 0, 3, 1, 2) },
			.returned = 3,
			.page = withoutMiddle,
			.state = R"(3=cell:1#0,1"r0c2" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R42", kTabled3, 0, Cells(1, 0, 3, 1, 2) },
			.forward = false,
			.returned = 3,
			.page = withoutMiddle,
			.state = R"(3=cell:1#0,1"r0c2" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R43", kTabled3, 0, Cells(1, 0, 3, 2, 3) },
			.returned = 2,
			.page = withoutLast,
			.state = R"(2=cell:1#0,0"r0c0" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R44", kTabled3, 0, Cells(1, 0, 3, 2, 3) },
			.forward = false,
			.returned = 7,
			.page = withoutLast,
			.state = R"(7=cell:1#2,1"r2c1" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R45", kTabled3, 0, Cells(1, 0, 3, 0, 1) },
			.forward = false,
			.returned = 2,
			.page = withoutFirst,
			.state = R"(2=cell:1#0,0"r0c1" ; live P0 T1 P2 nodes=9)",
		},
		{
			.start = { "R46", kTabled3, 3, Cells(1, 0, 3, 0, 1) },
			.returned = 2,
			.page = withoutFirst,
			.state = R"(2=cell:1#0,0"r0c1" ; live P0 T1 P2 nodes=9)",
		},
	});
}

[[nodiscard]] Report RemovalTableColumnsWhole() {
	return RunRemovals({
		{
			.start = { "R47", kTabled3, 0, Cells(1, 0, 3, 0, 3) },
			.returned = 2,
			.page = Framed(u"T"_q, { { { QString() } } }),
			.state = R"(2=cell:1#0,0"" ; live P0 T1 P2 nodes=4)",
		},
		{
			.start = { "R48", kUntitled, 0, Cells(1, 0, 2, 0, 2) },
			.returned = 1,
			.page = Flat({ u"A"_q, u"C"_q }),
			.state = R"(1=text:1"C" ; live P0 P1 nodes=2)",
		},
	});
}

[[nodiscard]] Report RemovalTableColumnsMerged() {
	return RunRemovals({
		{
			.start = { "R49", kColMerged, 0, Cells(0, 0, 2, 1, 2) },
			.returned = 2,
			.page = Page({ Table(u"T"_q, {
				{ { u"m"_q }, { u"x"_q } },
				{ { u"a"_q }, { u"c"_q } },
			}) }),
			.state = R"(2=cell:0#0,1"x" ; live T0 nodes=5)",
			.leaves = R"(text:0"T" cell:0#0,0"m" cell:0#0,1"x")"
				R"( cell:0#1,0"a" cell:0#1,1"c")",
			.spans = "1x1 1x1|1x1 1x1",
		},
		{
			.start = { "R50", kRowMerged, 0, Cells(0, 0, 2, 1, 2) },
			.returned = 2,
			.page = Page({ Table(u"T"_q, {
				{ { u"a"_q }, { u"b"_q } },
				{ { u"c"_q }, { u"d"_q } },
			}) }),
			.state = R"(2=cell:0#0,1"b" ; live T0 nodes=5)",
		},
		{
			.start = { "R51", kColMerged, 0, Cells(0, 0, 2, 0, 2) },
			.returned = 1,
			.page = Page({ Table(u"T"_q, { { { u"x"_q } }, { { u"c"_q } } }) }),
			.state = R"(1=cell:0#0,0"x" ; live T0 nodes=3)",
		},
	});
}

[[nodiscard]] Report RemovalRejected() {
	return RunRemovals({
		{
			.start = { "R52", kFlat4, 2, Blocks(Root(), 2, 5) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R53", kFlat4, 2, Blocks(Root(), -1, 1) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R54", kFlat4, 2, Blocks(Root(), 2, 2) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R55", kFlat4, 2, Blocks(Children(0), 0, 1) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R56", kFlat4, 2, Blocks(Children(7), 0, 1) },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "R57", kWithU, 1, Blocks(Root(), 0, 2) },
			.page = kWithU.page(),
			.state = R"(1=text:2"C" ; live P0 P2 nodes=2)",
		},
		{
			.start = { "R58", kListed, 2, Items(0, 0, 1) },
			.page = kListed.page(),
			.state = kListedAtI1,
		},
		{
			.start = { "R59", kListed, 2, Items(1, 1, 4) },
			.page = kListed.page(),
			.state = kListedAtI1,
		},
		{
			.start = { "R60", kListed, 2, Rows(1, 0, 1) },
			.page = kListed.page(),
			.state = kListedAtI1,
		},
		{
			.start = { "R61", kTabled, 2, Rows(1, 2, 4) },
			.page = kTabled.page(),
			.state = kTabledAtR0C0,
		},
		{
			.start = { "R62", kTabled, 2, Cells(1, 0, 3, 1, 3) },
			.page = kTabled.page(),
			.state = kTabledAtR0C0,
		},
		{
			.start = { "R63", kTabled3, 2, Cells(1, 0, 1, 1, 2) },
			.page = kTabled3.page(),
			.state = R"(2=cell:1#0,0"r0c0" ; live P0 T1 P2 nodes=12)",
		},
		{
			.start = { "R64", kFlat4, 2, PreparedEditSelection() },
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
	});
}

[[nodiscard]] Report RemovalRejectedStaleSelection() {
	const auto first = RemovalCase{
		.start = { "R65 first", kFlat4, 2, Blocks(Root(), 3, 4) },
		.returned = 2,
		.page = Flat({ u"A"_q, u"B"_q, u"C"_q }),
		.state = R"(2=text:2"C" ; live P0 P1 P2 nodes=3)",
	};
	auto again = first;
	again.start.id = "R65";
	again.returned = std::nullopt;
	auto report = Report{ .cases = 1 };
	auto expectFirst = Expect(report.failures, first.start.id);
	auto expectAgain = Expect(report.failures, again.start.id);
	auto state = MakeState(kFlat4);
	if (Activate(expectFirst, state, kFlat4, first.start.active)) {
		CheckRemoval(expectFirst, state, first);
		CheckRemoval(expectAgain, state, again);
	}
	return report;
}

[[nodiscard]] Report RemovalRejectedClearsTemporaryParagraph() {
	return RunRemovals({
		{
			.start = { "R66", kWithTdp, 0, Blocks(Root(), 2, 5) },
			.page = kWithTdp.page(),
			.state = kTemporaryAtEmpty,
		},
	});
}

[[nodiscard]] Report SnapshotRestoreAfterRemoval() {
	const auto removal = RemovalCase{
		.start = { "R67", kQuoted, 3, Blocks(Children(1), 0, 2) },
		.returned = 1,
		.page = Framed(Quote(Paragraphs({ u"q2"_q }), u"cap"_q)),
		.state = R"(1=text:1/0"q2" ; live P0 Q1 P2 nodes=4)",
	};
	auto report = Report{ .cases = 1 };
	auto expect = Expect(report.failures, removal.start.id);
	auto state = MakeState(kQuoted);
	if (Activate(expect, state, kQuoted, removal.start.active)) {
		const auto before = Observe(state);
		CheckRemoval(expect, state, removal);
		ExpectRestored(
			expect,
			state,
			before,
			kQuoted,
			R"(3=text:1/2"q2" ; live P0 Q1 P2 nodes=6)");
	}
	return report;
}

[[nodiscard]] DropCase ForwardSingleDrop(const char *id, int active) {
	return {
		.start = { id, kFlat4, active, Blocks(Root(), 0, 1) },
		.target = BlockTarget(Root(), 3),
		.outcome = ChangedText(2),
		.page = Flat({ u"B"_q, u"C"_q, u"A"_q, u"D"_q }),
		.state = R"(2=text:2"A" ; live P0 P1 P2 P3 nodes=4)",
		.leaves = R"(text:0"B" text:1"C" text:2"A" text:3"D")",
	};
}

[[nodiscard]] Report DropBlocksSameContainer() {
	return RunDrops({
		ForwardSingleDrop("D01", 0),
		{
			.start = { "D02", kFlat5, 0, Blocks(Root(), 0, 2) },
			.target = BlockTarget(Root(), 4),
			.outcome = ChangedText(2),
			.page = Flat({ u"C"_q, u"D"_q, u"A"_q, u"B"_q, u"E"_q }),
			.state = R"(2=text:2"A" ; live P0 P1 P2 P3 P4 nodes=5)",
		},
		{
			.start = { "D03", kFlat4, 0, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Root(), 4),
			.outcome = ChangedText(3),
			.page = Flat({ u"B"_q, u"C"_q, u"D"_q, u"A"_q }),
			.state = R"(3=text:3"A" ; live P0 P1 P2 P3 nodes=4)",
		},
		{
			.start = { "D04", kFlat4, 0, Blocks(Root(), 2, 4) },
			.target = BlockTarget(Root(), 0),
			.outcome = ChangedText(0),
			.page = Flat({ u"C"_q, u"D"_q, u"A"_q, u"B"_q }),
			.state = R"(0=text:0"C" ; live P0 P1 P2 P3 nodes=4)",
		},
		{
			.start = { "D05", kFlat4, 0, Blocks(Root(), 3, 4) },
			.target = BlockTarget(Root(), 1),
			.outcome = ChangedText(1),
			.page = Flat({ u"A"_q, u"D"_q, u"B"_q, u"C"_q }),
			.state = R"(1=text:1"D" ; live P0 P1 P2 P3 nodes=4)",
		},
		{
			.start = { "D48", kWithDivider, 0, Blocks(Root(), 1, 2) },
			.target = BlockTarget(Root(), 3),
			.outcome = ChangedSelection(Blocks(Root(), 2, 3)),
			.page = Page({
				Paragraph(u"A"_q),
				Paragraph(u"C"_q),
				Bare(BlockKind::Divider),
			}),
			.state = R"(1=text:1"C" ; live P0 P1 V2 nodes=2)",
		},
	});
}

[[nodiscard]] Report DropBlocksAdjacent() {
	return RunDrops({
		{
			.start = { "D06", kFlat4, 2, Blocks(Root(), 1, 2) },
			.target = BlockTarget(Root(), 1),
			.outcome = Unchanged(),
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "D07", kFlat4, 2, Blocks(Root(), 1, 2) },
			.target = BlockTarget(Root(), 2),
			.outcome = Unchanged(),
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "D08", kFlat4, 2, Blocks(Root(), 1, 3) },
			.target = BlockTarget(Root(), 3),
			.outcome = Unchanged(),
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
	});
}

[[nodiscard]] Report DropBlocksTargetInsideSource() {
	return RunDrops({
		{
			.start = { "D09", kQuoted, 0, Blocks(Root(), 1, 2) },
			.target = BlockTarget(Children(1), 0),
			.outcome = Unchanged(),
			.page = kQuoted.page(),
			.state = kQuoted.state,
		},
	});
}

[[nodiscard]] Report DropBlocksCrossContainer() {
	const auto quote = [](std::initializer_list<QString> texts) {
		return Quote(Paragraphs(texts), u"cap"_q);
	};
	return RunDrops({
		{
			.start = { "D10", kQuoted, 0, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Children(1), 1),
			.outcome = ChangedText(1),
			.page = Page({
				quote({ u"q0"_q, u"A"_q, u"q1"_q, u"q2"_q }),
				Paragraph(u"C"_q),
			}),
			.state = R"(1=text:0/1"A" ; live Q0 P1 nodes=6)",
		},
		{
			.start = { "D11", kQuoted, 0, Blocks(Children(1), 0, 1) },
			.target = BlockTarget(Root(), 3),
			.outcome = ChangedText(5),
			.page = Page({
				Paragraph(u"A"_q),
				quote({ u"q1"_q, u"q2"_q }),
				Paragraph(u"C"_q),
				Paragraph(u"q0"_q),
			}),
			.state = R"(5=text:3"q0" ; live P0 Q1 P2 P3 nodes=6)",
		},
		{
			.start = { "D12", kQuoted, 0, Blocks(Children(1), 2, 3) },
			.target = BlockTarget(Root(), 0),
			.outcome = ChangedText(0),
			.page = Page({
				Paragraph(u"q2"_q),
				Paragraph(u"A"_q),
				quote({ u"q0"_q, u"q1"_q }),
				Paragraph(u"C"_q),
			}),
			.state = R"(0=text:0"q2" ; live P0 P1 Q2 P3 nodes=6)",
		},
		{
			.start = { "D13", kNested, 0, Blocks(Root(), 2, 3) },
			.target = BlockTarget(ItemChildren(1, 0), 2),
			.outcome = ChangedText(4),
			.page = Page({
				Paragraph(u"A"_q),
				List({
					Item(
						QString(),
						Paragraphs({ u"i0"_q, u"x0"_q, u"x1"_q, u"C"_q })),
					Item(u"i1"_q),
				}),
			}),
			.state = R"(4=text:1.0/3"C" ; live P0 L1 nodes=6)",
			.leaves = R"(text:0"A" text:1.0/0"i0" text:1.0/1"x0")"
				R"( text:1.0/2"x1" text:1.0/3"C" item:1#1"i1")",
		},
		{
			.start = { "D14", kNested, 0, Blocks(Root(), 0, 1) },
			.target = BlockTarget(ItemChildren(1, 1), 0),
			.outcome = ChangedText(4),
			.page = Page({
				List({
					Item(u"i0"_q, Paragraphs({ u"x0"_q, u"x1"_q })),
					Item(QString(), Paragraphs({ u"i1"_q, u"A"_q })),
				}),
				Paragraph(u"C"_q),
			}),
			.state = R"(4=text:0.1/1"A" ; live L0 P1 nodes=6)",
		},
		{
			.start = { "D15", kNested, 0, Blocks(ItemChildren(1, 0), 0, 1) },
			.target = BlockTarget(Root(), 3),
			.outcome = ChangedText(5),
			.page = Page({
				Paragraph(u"A"_q),
				List({
					Item(u"i0"_q, Paragraphs({ u"x1"_q })),
					Item(u"i1"_q),
				}),
				Paragraph(u"C"_q),
				Paragraph(u"x0"_q),
			}),
			.state = R"(5=text:3"x0" ; live P0 L1 P2 P3 nodes=6)",
		},
	});
}

[[nodiscard]] Report DropListItemsSameList() {
	const auto reordered = Framed(List({ u"i2"_q, u"i3"_q, u"i0"_q, u"i1"_q }));
	return RunDrops({
		{
			.start = { "D16", kListed4, 0, Items(1, 0, 1) },
			.target = ListTarget(1, 3),
			.outcome = ChangedText(3),
			.page = Framed(List({ u"i1"_q, u"i2"_q, u"i0"_q, u"i3"_q })),
			.state = R"(3=item:1#2"i0" ; live P0 L1 P2 nodes=6)",
		},
		{
			.start = { "D17", kListed4, 0, Items(1, 0, 2) },
			.target = ListTarget(1, 4),
			.outcome = ChangedText(3),
			.page = reordered,
			.state = R"(3=item:1#2"i0" ; live P0 L1 P2 nodes=6)",
		},
		{
			.start = { "D18", kListed4, 0, Items(1, 2, 4) },
			.target = ListTarget(1, 0),
			.outcome = ChangedText(1),
			.page = reordered,
			.state = R"(1=item:1#0"i2" ; live P0 L1 P2 nodes=6)",
		},
	});
}

[[nodiscard]] Report DropListItemsAdjacent() {
	return RunDrops({
		{
			.start = { "D19", kListed4, 2, Items(1, 1, 2) },
			.target = ListTarget(1, 1),
			.outcome = Unchanged(),
			.page = kListed4.page(),
			.state = kListed4AtI1,
		},
		{
			.start = { "D20", kListed4, 2, Items(1, 1, 2) },
			.target = ListTarget(1, 2),
			.outcome = Unchanged(),
			.page = kListed4.page(),
			.state = kListed4AtI1,
		},
		{
			.start = { "D21", kListed4, 2, Items(1, 0, 4) },
			.target = BlockTarget(Root(), 2),
			.outcome = Unchanged(),
			.page = kListed4.page(),
			.state = kListed4AtI1,
		},
	});
}

[[nodiscard]] Report DropListItemsToBlocks() {
	const auto whole = List({ u"i0"_q, u"i1"_q, u"i2"_q, u"i3"_q });
	return RunDrops({
		{
			.start = { "D22", kListed4, 0, Items(1, 1, 2) },
			.target = BlockTarget(Root(), 3),
			.outcome = ChangedText(5),
			.page = Page({
				Paragraph(u"A"_q),
				List({ u"i0"_q, u"i2"_q, u"i3"_q }),
				Paragraph(u"C"_q),
				List({ u"i1"_q }),
			}),
			.state = R"(5=item:3#0"i1" ; live P0 L1 P2 L3 nodes=6)",
		},
		{
			.start = { "D23", kListed4, 0, Items(1, 1, 3) },
			.target = BlockTarget(Root(), 0),
			.outcome = ChangedText(0),
			.page = Page({
				List({ u"i1"_q, u"i2"_q }),
				Paragraph(u"A"_q),
				List({ u"i0"_q, u"i3"_q }),
				Paragraph(u"C"_q),
			}),
			.state = R"(0=item:0#0"i1" ; live L0 P1 L2 P3 nodes=6)",
		},
		{
			.start = { "D24", kListed4, 0, Items(1, 0, 4) },
			.target = BlockTarget(Root(), 0),
			.outcome = ChangedText(0),
			.page = Page({ whole, Paragraph(u"A"_q), Paragraph(u"C"_q) }),
			.state = R"(0=item:0#0"i0" ; live L0 P1 P2 nodes=6)",
		},
		{
			.start = { "D25", kListed4, 0, Items(1, 0, 4) },
			.target = BlockTarget(Root(), 3),
			.outcome = ChangedText(2),
			.page = Page({ Paragraph(u"A"_q), Paragraph(u"C"_q), whole }),
			.state = R"(2=item:2#0"i0" ; live P0 P1 L2 nodes=6)",
		},
	});
}

[[nodiscard]] Report DropListItemsOtherList() {
	const auto ordered = [](
			std::initializer_list<QString> texts,
			std::optional<int> start) {
		auto items = std::vector<RichPage::ListItem>();
		for (const auto &text : texts) {
			items.push_back(Item(text));
		}
		return List(std::move(items), ListKind::Ordered, start);
	};
	return RunDrops({
		{
			.start = { "D26", kTwoLists, 0, Items(0, 0, 1) },
			.target = ListTarget(2, 1),
			.outcome = ChangedText(3),
			.page = Page({
				List({ u"a1"_q }),
				Paragraph(u"M"_q),
				List({ u"b0"_q, u"a0"_q, u"b1"_q }),
			}),
			.state = R"(3=item:2#1"a0" ; live L0 P1 L2 nodes=5)",
		},
		{
			.start = { "D27", kTwoLists, 0, Items(0, 0, 2) },
			.target = ListTarget(2, 1),
			.outcome = ChangedText(2),
			.page = Page({
				Paragraph(u"M"_q),
				List({ u"b0"_q, u"a0"_q, u"a1"_q, u"b1"_q }),
			}),
			.state = R"(2=item:1#1"a0" ; live P0 L1 nodes=5)",
		},
		{
			.start = { "D28", kMixedLists, 0, Items(0, 0, 1) },
			.target = ListTarget(2, 1),
			.outcome = ChangedText(3),
			.page = Page({
				List({ u"a1"_q }),
				Paragraph(u"M"_q),
				ordered({ u"b0"_q }, 1),
				List({ u"a0"_q }),
				ordered({ u"b1"_q, u"b2"_q }, 2),
			}),
			.state = R"(3=item:3#0"a0" ; live L0 P1 L2 L3 L4 nodes=6)",
			.leaves = R"(item:0#0"a1" text:1"M" item:2#0"b0" item:3#0"a0")"
				R"( item:4#0"b1" item:4#1"b2")",
		},
		{
			.start = { "D29", kMixedLists, 0, Items(0, 0, 1) },
			.target = ListTarget(2, 0),
			.outcome = ChangedText(2),
			.page = Page({
				List({ u"a1"_q }),
				Paragraph(u"M"_q),
				List({ u"a0"_q }),
				ordered({ u"b0"_q, u"b1"_q, u"b2"_q }, std::nullopt),
			}),
			.state = R"(2=item:2#0"a0" ; live L0 P1 L2 L3 nodes=6)",
		},
	});
}

[[nodiscard]] Report DropListItemsNested() {
	return RunDrops({
		{
			.start = { "D30", kNested, 0, Items(1, 0, 1) },
			.target = BlockTarget(ItemChildren(1, 0), 1),
			.outcome = Unchanged(),
			.page = kNested.page(),
			.state = kNested.state,
		},
		{
			.start = { "D31", kNested, 0, Items(1, 1, 2) },
			.target = BlockTarget(ItemChildren(1, 0), 1),
			.outcome = ChangedText(3),
			.page = Framed(List({
				Item(QString(), {
					Paragraph(u"i0"_q),
					Paragraph(u"x0"_q),
					List({ u"i1"_q }),
					Paragraph(u"x1"_q),
				}),
			})),
			.state = R"(3=item:1.0/2#0"i1" ; live P0 L1 P2 nodes=6)",
		},
	});
}

[[nodiscard]] Report DropRejected() {
	return RunDrops({
		{
			.start = { "D32", kFlat4, 2, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Root(), -1),
			.outcome = Failed(),
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "D33", kFlat4, 2, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Root(), 5),
			.outcome = Failed(),
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "D34", kFlat4, 2, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Children(2), 0),
			.outcome = Failed(),
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "D35", kFlat4, 2, Blocks(Root(), 0, 1) },
			.target = PreparedEditTextDropTarget(),
			.outcome = Failed(),
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "D36", kTabled, 2, Rows(1, 0, 1) },
			.target = BlockTarget(Root(), 0),
			.outcome = Failed(),
			.page = kTabled.page(),
			.state = kTabledAtR0C0,
		},
		{
			.start = { "D37", kTabled3, 2, Cells(1, 0, 3, 0, 1) },
			.target = BlockTarget(Root(), 0),
			.outcome = Failed(),
			.page = kTabled3.page(),
			.state = R"(2=cell:1#0,0"r0c0" ; live P0 T1 P2 nodes=12)",
		},
		{
			.start = { "D38", kListed, 2, Blocks(Root(), 0, 1) },
			.target = ListTarget(1, 1),
			.outcome = Failed(),
			.page = kListed.page(),
			.state = kListedAtI1,
		},
		{
			.start = { "D39", kFlat4, 2, Blocks(Root(), 3, 9) },
			.target = BlockTarget(Root(), 0),
			.outcome = Failed(),
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
		{
			.start = { "D40", kTwoLists, 0, Items(0, 0, 1) },
			.target = ListTarget(2, 5),
			.outcome = Failed(),
			.page = kTwoLists.page(),
			.state = kTwoListsAtA0,
		},
		{
			.start = { "D41", kTwoLists, 0, Items(0, 0, 1) },
			.target = ListTarget(1, 0),
			.outcome = Failed(),
			.page = kTwoLists.page(),
			.state = kTwoListsAtA0,
		},
	});
}

[[nodiscard]] Report DropRejectedLimits() {
	return RunDrops({
		{
			.start = { "D42", kQuoteDepth, 2, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Root(), 3),
			.outcome = Failed(),
			.limit = RichMessageLimitError::Depth,
			.page = kQuoteDepth.page(),
			.state = R"(2=caption:1"cap" ; live P0 Q1 P2 nodes=4)",
		},
		{
			.start = { "D43", kEmptyQuote, 2, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Children(1), 0),
			.outcome = Failed(),
			.limit = RichMessageLimitError::Depth,
			.page = kEmptyQuote.page(),
			.state = R"(2=caption:1"cap" ; live P0 Q1 P2 nodes=4)",
		},
		{
			.start = { "D44", kFlat4ThreeBlocks, 2, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Root(), 3),
			.outcome = Failed(),
			.limit = RichMessageLimitError::Blocks,
			.page = kFlat4.page(),
			.state = kFlat4AtC,
		},
	});
}

[[nodiscard]] Report DropRejectedKeepsTemporaryParagraph() {
	return RunDrops({
		{
			.start = { "D45", kWithTdp, 0, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Root(), 4),
			.outcome = Failed(),
			.page = kWithTdp.page(),
			.state = kTemporaryAtEmpty,
			.temporary = true,
		},
		{
			.start = { "D46", kWithTdp, 0, Blocks(Root(), 0, 1) },
			.target = BlockTarget(Root(), 1),
			.outcome = Unchanged(),
			.page = kWithTdp.page(),
			.state = kTemporaryAtEmpty,
			.temporary = true,
		},
	});
}

[[nodiscard]] Report SnapshotRestoreAfterDrop() {
	const auto drop = ForwardSingleDrop("D47", 1);
	auto report = Report{ .cases = 1 };
	auto expect = Expect(report.failures, drop.start.id);
	auto state = MakeState(kFlat4);
	if (Activate(expect, state, kFlat4, drop.start.active)) {
		const auto before = Observe(state);
		CheckDrop(expect, state, drop);
		ExpectRestored(
			expect,
			state,
			before,
			kFlat4,
			R"(1=text:1"B" ; live P0 P1 P2 P3 nodes=4)");
	}
	return report;
}

struct ConversionCase {
	const char *id = nullptr;
	TextWithEntities input;
	TextWithTags tags;
	TextWithEntities back;
	std::vector<RichTextEditorOffsetReplacement> replacements;
};

struct FormattingCase {
	const char *id = nullptr;
	TextWithEntities text;
	Formatting action = Formatting::Bold;
	int from = 0;
	int till = 0;
	Result result = Result::Failed;
	TextWithEntities after;
};

struct ConflictCase {
	const char *id = nullptr;
	ConversionCase conversion;
	FormattingCase formatting;
};

[[nodiscard]] QString RangeText(int offset, int length) {
	return u"@%1+%2"_q.arg(offset).arg(length);
}

[[nodiscard]] QString TagsText(const TextWithTags &text) {
	auto result = QuotedText(text.text);
	for (const auto &tag : text.tags) {
		result += ' ' + tag.id + RangeText(tag.offset, tag.length);
	}
	return result;
}

[[nodiscard]] QString EntitiesText(const TextWithEntities &text) {
	auto result = QuotedText(text.text);
	for (const auto &entity : text.entities) {
		result += u" T"_q
			+ QString::number(int(entity.type()))
			+ RangeText(entity.offset(), entity.length());
		if (!entity.data().isEmpty()) {
			result += '=' + entity.data();
		}
	}
	return result;
}

void ExpectRich(
		Expect &expect,
		const char *label,
		const TextWithEntities &got,
		const TextWithEntities &expected) {
	expect.same(
		label,
		got == expected,
		EntitiesText(got),
		EntitiesText(expected));
}

[[nodiscard]] QString ReplacementsText(
		const std::vector<RichTextEditorOffsetReplacement> &replacements) {
	auto result = QStringList();
	for (const auto &replacement : replacements) {
		result.push_back(u"%1+%2>%3"_q
			.arg(replacement.richOffset)
			.arg(replacement.richLength)
			.arg(replacement.editorLength));
	}
	return result.join(' ');
}

[[nodiscard]] std::optional<TextWithTags> WithCounterMarks(
		TextWithTags text) {
	static const auto counter = QRegularExpression(
		QRegularExpression::anchoredPattern(
			u"custom-emoji://.*\\?([0-9]+)"_q));
	for (auto &tag : text.tags) {
		auto parts = tag.id.split(QChar('\\'));
		for (auto &part : parts) {
			if (!part.startsWith(u"custom-emoji://"_q)) {
				continue;
			}
			const auto match = counter.match(part);
			if (!match.hasMatch()) {
				return std::nullopt;
			}
			part.replace(
				match.capturedStart(1),
				match.capturedLength(1),
				QChar('N'));
		}
		tag.id = parts.join(QChar('\\'));
	}
	return text;
}

[[nodiscard]] TextWithEntities Rich(
		QString text,
		EntitiesInText entities = {}) {
	auto result = tr::marked(std::move(text));
	result.entities = std::move(entities);
	return result;
}

[[nodiscard]] TextWithTags Tagged(
		QString text,
		TextWithTags::Tags tags = {}) {
	return { std::move(text), std::move(tags) };
}

[[nodiscard]] RichPage Single(TextWithEntities text) {
	auto block = Bare(BlockKind::Paragraph);
	block.text.text = std::move(text);
	return Page({ std::move(block) });
}

[[nodiscard]] QString ButtonPayload() {
	return SerializeInlineTextObjectEntity({
		.kind = InlineTextObjectKind::Button,
		.data = InlineTextObjectButtonData{ .label = tr::marked(u"go"_q) },
	});
}

[[nodiscard]] QString FormulaPayload() {
	return SerializeInlineTextObjectEntity({
		.kind = InlineTextObjectKind::Formula,
		.data = InlineTextObjectFormulaData{
			.copySource = u"$x^2$"_q,
			.trimmedTex = u"x^2"_q,
		},
	});
}

void CheckConversion(Expect &expect, const ConversionCase &test) {
	const auto converted = ConvertRichTextToEditorTags(test.input);
	const auto tags = WithCounterMarks(converted.text);
	expect.same(
		"tags",
		tags && (*tags == test.tags),
		tags
			? TagsText(*tags)
			: (TagsText(converted.text) + u" (no decimal counter)"_q),
		TagsText(test.tags));
	expect.text(
		"replacements",
		ReplacementsText(converted.replacements),
		ReplacementsText(test.replacements));
	ExpectRich(
		expect,
		"back",
		ConvertEditorTagsToRichText(converted.text),
		test.back);
}

void CheckFormatting(Expect &expect, const FormattingCase &test) {
	auto state = State(
		std::make_shared<RichPage>(Single(test.text)),
		nullptr);
	const auto span = State::TextNodeSpan{
		.leaf = { .kind = LeafKind::BlockText, .block = { .index = 0 } },
		.from = test.from,
		.till = test.till,
	};
	const auto result = state.applyFormattingToTextSpans(
		{ span },
		test.action,
		true);
	expect.text("result", ResultText(result), ResultText(test.result));
	const auto &blocks = state.richPage().blocks;
	const auto text = blocks.empty()
		? TextWithEntities()
		: blocks.front().text.text;
	ExpectRich(expect, "text", text, test.after);
	expect.page("page", state.richPage(), Single(test.after));
}

void CheckConflict(Expect &expect, const ConflictCase &test) {
	CheckConversion(expect, test.conversion);
	CheckFormatting(expect, test.formatting);
}

[[nodiscard]] Report TagsConvert() {
	using Type = EntityType;
	const auto bold = u"**"_q;
	const auto sup = u"iv-superscript"_q;
	const auto boldSup = u"**\\iv-superscript"_q;
	const auto url = u"https://example.com"_q;
	const auto urlSup = u"https://example.com\\iv-superscript"_q;
	const auto button = ButtonPayload();
	const auto boldButton = u"**\\custom-emoji://"_q + button + u"?N"_q;
	const auto emoji = u"a\U0001F600b"_q;
	const auto trailing = u"a\uFFFC"_q;
	const auto inside = u"a\uFFFCb"_q;
	const auto rows = std::vector<ConversionCase>{
		{
			.id = "C01",
			.input = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 2 },
				{ Type::Superscript, 3, 2 },
			}),
			.tags = Tagged(u"abcdef"_q, { { 0, 2, bold }, { 3, 2, sup } }),
			.back = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 2 },
				{ Type::Superscript, 3, 2 },
			}),
		},
		{
			.id = "C02",
			.input = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 4 },
				{ Type::Superscript, 2, 4 },
			}),
			.tags = Tagged(u"abcdef"_q, {
				{ 0, 2, bold },
				{ 2, 2, boldSup },
				{ 4, 2, sup },
			}),
			.back = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 4 },
				{ Type::Superscript, 2, 2 },
				{ Type::Superscript, 4, 2 },
			}),
		},
		{
			.id = "C03",
			.input = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 6 },
				{ Type::Superscript, 2, 2 },
			}),
			.tags = Tagged(u"abcdef"_q, {
				{ 0, 2, bold },
				{ 2, 2, boldSup },
				{ 4, 2, bold },
			}),
			.back = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 6 },
				{ Type::Superscript, 2, 2 },
			}),
		},
		{
			.id = "C04",
			.input = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 3 },
				{ Type::Superscript, 3, 3 },
			}),
			.tags = Tagged(u"abcdef"_q, { { 0, 3, bold }, { 3, 3, sup } }),
			.back = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 3 },
				{ Type::Superscript, 3, 3 },
			}),
		},
		{
			.id = "C05",
			.input = Rich(u"abc"_q, {
				{ Type::Bold, 0, 1 },
				{ Type::Superscript, 2, 3 },
			}),
			.tags = Tagged(u"abc"_q, { { 0, 1, bold }, { 2, 3, sup } }),
			.back = Rich(u"abc"_q, { { Type::Bold, 0, 1 } }),
		},
		{
			.id = "C06",
			.input = Rich(u"abc"_q, {
				{ Type::Bold, 1, 0 },
				{ Type::Superscript, 2, 0 },
			}),
			.tags = Tagged(u"abc"_q),
			.back = Rich(u"abc"_q),
		},
		{
			.id = "C07",
			.input = Rich(QString()),
			.tags = Tagged(QString()),
			.back = Rich(QString()),
		},
		{
			.id = "C08",
			.input = Rich(emoji, {
				{ Type::Bold, 1, 2 },
				{ Type::Superscript, 1, 3 },
			}),
			.tags = Tagged(emoji, { { 1, 2, boldSup }, { 3, 1, sup } }),
			.back = Rich(emoji, {
				{ Type::Bold, 1, 2 },
				{ Type::Superscript, 1, 2 },
				{ Type::Superscript, 3, 1 },
			}),
		},
		{
			.id = "C09",
			.input = Rich(emoji, { { Type::Superscript, 2, 2 } }),
			.tags = Tagged(emoji, { { 2, 2, sup } }),
			.back = Rich(emoji, { { Type::Superscript, 2, 2 } }),
		},
		{
			.id = "C10",
			.input = Rich(u"abcdef"_q, {
				{ Type::CustomUrl, 0, 4, url },
				{ Type::Superscript, 2, 4 },
			}),
			.tags = Tagged(u"abcdef"_q, {
				{ 0, 2, url },
				{ 2, 2, urlSup },
				{ 4, 2, sup },
			}),
			.back = Rich(u"abcdef"_q, {
				{ Type::CustomUrl, 0, 4, url },
				{ Type::Superscript, 2, 2 },
				{ Type::Superscript, 4, 2 },
			}),
		},
		{
			.id = "C11",
			.input = Rich(trailing, {
				{ Type::Bold, 0, 2 },
				{ Type::CustomEmoji, 1, 1, button },
			}),
			.tags = Tagged(trailing, { { 0, 1, bold }, { 1, 1, boldButton } }),
			.back = Rich(trailing, {
				{ Type::Bold, 0, 2 },
				{ Type::CustomEmoji, 1, 1, button },
			}),
		},
		{
			.id = "C12",
			.input = Rich(inside, {
				{ Type::Bold, 0, 3 },
				{ Type::CustomEmoji, 1, 1, button },
			}),
			.tags = Tagged(inside, {
				{ 0, 1, bold },
				{ 1, 1, boldButton },
				{ 2, 1, bold },
			}),
			.back = Rich(inside, {
				{ Type::Bold, 0, 3 },
				{ Type::CustomEmoji, 1, 1, button },
			}),
		},
		{
			.id = "C13",
			.input = Rich(u"abc"_q, { { Type::Superscript, -1, 2 } }),
			.tags = Tagged(u"abc"_q, { { 0, 2, sup } }),
			.back = Rich(u"abc"_q, { { Type::Superscript, 0, 2 } }),
		},
	};
	return RunRows(rows, CheckConversion);
}

[[nodiscard]] Report TagsState() {
	using Type = EntityType;
	const auto url = u"https://example.com"_q;
	const auto emoji = u"a\U0001F600b"_q;
	const auto rows = std::vector<FormattingCase>{
		{
			.id = "S01",
			.text = Rich(u"abcdef"_q, { { Type::Bold, 0, 2 } }),
			.from = 2,
			.till = 4,
			.result = Result::Changed,
			.after = Rich(u"abcdef"_q, {
				{ Type::Bold, 0, 2 },
				{ Type::Bold, 2, 2 },
			}),
		},
		{
			.id = "S02",
			.text = Rich(u"abcdef"_q, { { Type::Italic, 0, 1 } }),
			.from = 3,
			.till = 5,
			.result = Result::Changed,
			.after = Rich(u"abcdef"_q, {
				{ Type::Italic, 0, 1 },
				{ Type::Bold, 3, 2 },
			}),
		},
		{
			.id = "S03",
			.text = Rich(u"abcdef"_q, { { Type::CustomUrl, 0, 4, url } }),
			.from = 2,
			.till = 6,
			.result = Result::Changed,
			.after = Rich(u"abcdef"_q, {
				{ Type::CustomUrl, 0, 2, url },
				{ Type::CustomUrl, 2, 2, url },
				{ Type::Bold, 2, 2 },
				{ Type::Bold, 4, 2 },
			}),
		},
		{
			.id = "S04",
			.text = Rich(u"abcdef"_q, { { Type::Superscript, 0, 6 } }),
			.action = Formatting::Subscript,
			.from = 2,
			.till = 4,
			.result = Result::Changed,
			.after = Rich(u"abcdef"_q, {
				{ Type::Superscript, 0, 2 },
				{ Type::Subscript, 2, 2 },
				{ Type::Superscript, 4, 2 },
			}),
		},
		{
			.id = "S05",
			.text = Rich(u"abc"_q),
			.from = 2,
			.till = 10,
			.result = Result::Changed,
			.after = Rich(u"abc"_q, { { Type::Bold, 2, 1 } }),
		},
		{
			.id = "S06",
			.text = Rich(u"abc"_q),
			.from = 1,
			.till = 1,
			.result = Result::Unchanged,
			.after = Rich(u"abc"_q),
		},
		{
			.id = "S07",
			.text = Rich(QString()),
			.till = 1,
			.result = Result::Unchanged,
			.after = Rich(QString()),
		},
		{
			.id = "S08",
			.text = Rich(emoji),
			.from = 1,
			.till = 3,
			.result = Result::Changed,
			.after = Rich(emoji, { { Type::Bold, 1, 2 } }),
		},
		{
			.id = "S09",
			.text = Rich(emoji),
			.till = 2,
			.result = Result::Changed,
			.after = Rich(emoji, { { Type::Bold, 0, 2 } }),
		},
	};
	return RunRows(rows, CheckFormatting);
}

[[nodiscard]] Report TagsConflicts() {
	using Type = EntityType;
	const auto bold = u"**"_q;
	const auto formula = FormulaPayload();
	const auto object = u"a\uFFFCb"_q;
	const auto source = u"a$x^2$b"_q;
	const auto rows = std::vector<ConflictCase>{
		{
			.id = "P01",
			.conversion = {
				.input = Rich(u"abcdef"_q, {
					{ Type::Subscript, 0, 4 },
					{ Type::Superscript, 2, 4 },
				}),
				.tags = Tagged(u"abcdef"_q, {
					{ 0, 2, u"iv-subscript"_q },
					{ 2, 2, u"iv-subscript\\iv-superscript"_q },
					{ 4, 2, u"iv-superscript"_q },
				}),
				.back = Rich(u"abcdef"_q, {
					{ Type::Subscript, 0, 2 },
					{ Type::Subscript, 2, 2 },
					{ Type::Superscript, 2, 2 },
					{ Type::Superscript, 4, 2 },
				}),
			},
			.formatting = {
				.text = Rich(u"abcdef"_q, { { Type::Subscript, 0, 4 } }),
				.action = Formatting::Superscript,
				.from = 2,
				.till = 6,
				.result = Result::Changed,
				.after = Rich(u"abcdef"_q, {
					{ Type::Subscript, 0, 2 },
					{ Type::Superscript, 2, 4 },
				}),
			},
		},
		{
			.id = "P02",
			.conversion = {
				.input = Rich(object, {
					{ Type::Bold, 0, 3 },
					{ Type::CustomEmoji, 1, 1, formula },
				}),
				.tags = Tagged(source, {
					{ 0, 1, bold },
					{ 1, 5, u"iv-math"_q },
					{ 6, 1, bold },
				}),
				.back = Rich(object, {
					{ Type::Bold, 0, 1 },
					{ Type::CustomEmoji, 1, 1, formula },
					{ Type::Bold, 2, 1 },
				}),
				.replacements = {
					{ .richOffset = 1, .richLength = 1, .editorLength = 5 },
				},
			},
			.formatting = {
				.text = Rich(object, { { Type::CustomEmoji, 1, 1, formula } }),
				.till = 3,
				.result = Result::Changed,
				.after = Rich(source, { { Type::Bold, 0, 7 } }),
			},
		},
	};
	return RunRows(rows, CheckConflict);
}

constexpr auto kMath = Fixture{
	.name = "math",
	.page = [] {
		auto math = Bare(BlockKind::Math);
		math.formula = u"x^2"_q;
		return Framed(std::move(math));
	},
};

struct LeafStart {
	Fixture fixture;
	int active = 0;
	const char *leaf = nullptr;
	const char *text = nullptr;
};

struct LookupCase {
	const char *id = nullptr;
	LeafStart start;
	FieldMode mode = FieldMode::Rich;
	TextWithEntities edit;
	RichPage page;
};

struct StaleCase {
	const char *id = nullptr;
	LeafStart start;
	void (*shorten)(RichPage &) = nullptr;
	std::vector<TextSpan> spans;
	Result result = Result::Unchanged;
	RichPage page;
};

[[nodiscard]] QString ModeText(FieldMode mode) {
	return (mode == FieldMode::Raw) ? u"raw"_q : u"rich"_q;
}

[[nodiscard]] QString SpansText(const std::vector<TextSpan> &spans) {
	auto result = QStringList();
	for (const auto &span : spans) {
		result.push_back(LeafText(span.leaf)
			+ u"[%1,%2)"_q.arg(span.from).arg(span.till));
	}
	return result.join(' ');
}

[[nodiscard]] TextSpan FirstUnit(LeafPath leaf) {
	return { .leaf = std::move(leaf), .till = 1 };
}

[[nodiscard]] std::optional<LeafPath> ReadActiveLeaf(
		Expect &expect,
		State &state,
		const LeafStart &start) {
	if (!Activate(expect, state, start.fixture, start.active)) {
		return std::nullopt;
	}
	const auto path = state.activeLeafPath();
	const auto text = Latin(start.text);
	expect.text("leaf", OptionalLeafText(path), Latin(start.leaf));
	ExpectRich(expect, "read", state.activeText(), Rich(text));
	expect.text("raw read", state.activeRawText(), text);
	return path;
}

void CheckLookup(Expect &expect, const LookupCase &test) {
	const auto &start = test.start;
	auto state = MakeState(start.fixture);
	const auto leaf = ReadActiveLeaf(expect, state, start);
	if (!leaf) {
		return;
	}
	expect.text(
		"mode",
		ModeText(state.activeFieldMode()),
		ModeText(test.mode));
	if (test.mode == FieldMode::Raw) {
		const auto whole = int(Latin(start.text).size());
		const auto formatted = state.applyFormattingToTextSpans(
			{ TextSpan{ .leaf = *leaf, .till = whole } },
			Formatting::Bold,
			true);
		expect.text(
			"format result",
			ResultText(formatted),
			ResultText(Result::Unchanged));
		expect.page("format page", state.richPage(), start.fixture.page());
	}
	const auto result = state.applyActiveText(test.edit);
	expect.text("result", ResultText(result), ResultText(Result::Changed));
	ExpectRich(expect, "fresh read", state.activeText(), test.edit);
	expect.text("fresh raw read", state.activeRawText(), test.edit.text);
	expect.page("page", state.richPage(), test.page);
}

void CheckSpans(Expect &expect, const StaleCase &test) {
	const auto &fixture = test.start.fixture;
	const auto page = std::make_shared<RichPage>(fixture.page());
	auto state = State(page, nullptr, fixture.limits);
	auto spans = test.spans;
	if (test.shorten) {
		const auto leaf = ReadActiveLeaf(expect, state, test.start);
		if (!leaf) {
			return;
		}
		test.shorten(*page);
		ExpectRich(expect, "stale read", state.activeText(), {});
		expect.text("stale raw read", state.activeRawText(), QString());
		state.resyncAfterExternalRichPageMutation();
		spans.insert(spans.begin(), FirstUnit(*leaf));
	}
	const auto result = state.applyFormattingToTextSpans(
		spans,
		Formatting::Bold,
		true);
	expect.text("result", ResultText(result), ResultText(test.result));
	expect.page("page", state.richPage(), test.page);
}

[[nodiscard]] Report LeafLookup() {
	using Type = EntityType;
	const auto edit = Rich(u"edit"_q, { { Type::Bold, 0, 4 } });
	auto text = kFlat4.page();
	text.blocks[1].text.text = edit;
	auto caption = kQuoted.page();
	caption.blocks[1].caption.text = edit;
	auto item = kListed.page();
	item.blocks[1].listItems[1].text.text = edit;
	auto cell = kTabled.page();
	cell.blocks[1].tableRows[0].cells[1].text.text = edit;
	auto formula = kMath.page();
	formula.blocks[1].formula = u"y^2"_q;
	const auto rows = std::vector<LookupCase>{
		{
			.id = "R01",
			.start = { kFlat4, 1, "text:1", "B" },
			.edit = edit,
			.page = text,
		},
		{
			.id = "R02",
			.start = { kQuoted, 4, "caption:1", "cap" },
			.edit = edit,
			.page = caption,
		},
		{
			.id = "R03",
			.start = { kListed, 2, "item:1#1", "i1" },
			.edit = edit,
			.page = item,
		},
		{
			.id = "R04",
			.start = { kTabled, 3, "cell:1#0,1", "r0c1" },
			.edit = edit,
			.page = cell,
		},
		{
			.id = "R05",
			.start = { kMath, 1, "math:1", "x^2" },
			.mode = FieldMode::Raw,
			.edit = Rich(u"y^2"_q),
			.page = formula,
		},
	};
	auto report = RunRows(rows, CheckLookup);
	auto expect = Expect(report.failures, "R06");
	++report.cases;
	const auto state = MakeState(kQuoted);
	const auto source = state.preparedLeafSourceForOrdinal(3);
	expect.same("source", source.has_value(), u"none"_q, u"ordinal 3"_q);
	if (source) {
		const auto spans = state.resolveTextSpansForPreparedLeafRange(
			*source,
			1,
			4);
		expect.text(
			"spans",
			SpansText(spans),
			u"text:1/2[1,2) caption:1[0,2)"_q);
	}
	return report;
}

[[nodiscard]] Report LeafInvalid() {
	using Type = EntityType;
	const auto dropLastBlock = [](RichPage &page) {
		page.blocks.pop_back();
	};
	auto bolded = Flat({ u"A"_q, u"B"_q, u"C"_q });
	bolded.blocks[0].text.text = Rich(u"A"_q, { { Type::Bold, 0, 1 } });
	const auto rows = std::vector<StaleCase>{
		{
			.id = "V01",
			.start = { kFlat4, 3, "text:3", "D" },
			.shorten = dropLastBlock,
			.page = Flat({ u"A"_q, u"B"_q, u"C"_q }),
		},
		{
			.id = "V02",
			.start = { kListed, 3, "item:1#2", "i2" },
			.shorten = [](RichPage &page) {
				page.blocks[1].listItems.pop_back();
			},
			.page = Framed(List({ u"i0"_q, u"i1"_q })),
		},
		{
			.id = "V03",
			.start = { kTabled, 6, "cell:1#2,0", "r2c0" },
			.shorten = [](RichPage &page) {
				page.blocks[1].tableRows.pop_back();
			},
			.page = Framed(u"T"_q, Grid({ 0, 1 }, { 0, 1 })),
		},
		{
			.id = "V04",
			.start = { kTabled, 3, "cell:1#0,1", "r0c1" },
			.shorten = [](RichPage &page) {
				for (auto &row : page.blocks[1].tableRows) {
					row.cells.pop_back();
				}
			},
			.page = Framed(u"T"_q, Grid({ 0, 1, 2 }, { 0 })),
		},
		{
			.id = "V05",
			.start = { kFlat4, 3, "text:3", "D" },
			.shorten = dropLastBlock,
			.spans = { FirstUnit({
				.kind = LeafKind::BlockText,
				.block = { .index = 0 },
			}) },
			.result = Result::Changed,
			.page = bolded,
		},
		{
			.id = "V06",
			.start = { kListed },
			.spans = { FirstUnit({
				.kind = LeafKind::ListItemText,
				.block = { .index = 1 },
				.listItemIndex = -1,
			}) },
			.page = kListed.page(),
		},
		{
			.id = "V07",
			.start = { kTabled },
			.spans = { FirstUnit({
				.kind = LeafKind::TableCellText,
				.block = { .index = 1 },
				.tableRowIndex = -1,
				.tableCellIndex = 0,
			}) },
			.page = kTabled.page(),
		},
		{
			.id = "V08",
			.start = { kTabled },
			.spans = { FirstUnit({
				.kind = LeafKind::TableCellText,
				.block = { .index = 1 },
				.tableRowIndex = 0,
				.tableCellIndex = -1,
			}) },
			.page = kTabled.page(),
		},
	};
	return RunRows(rows, CheckSpans);
}

[[nodiscard]] int RunChecks() {
	const auto checks = std::vector<Check>{
		{ "setup.fixtures", SetupFixtures },
		{ "removal.blocks.range-and-direction", RemovalBlocksRangeAndDirection },
		{ "removal.blocks.active-leaf-kept", RemovalBlocksActiveLeafKept },
		{ "removal.blocks.active-leaf-removed", RemovalBlocksActiveLeafRemoved },
		{
			"removal.blocks.leading-planned-focus",
			RemovalBlocksLeadingPlannedFocus,
		},
		{ "removal.quote-children", RemovalQuoteChildren },
		{ "removal.list-items", RemovalListItems },
		{
			"removal.list-items.leading-planned-focus",
			RemovalListItemsLeadingPlannedFocus,
		},
		{ "removal.list-item-children", RemovalListItemChildren },
		{ "removal.table-rows", RemovalTableRows },
		{ "removal.table-rows-merged", RemovalTableRowsMerged },
		{ "removal.table-rows-menu", RemovalTableRowsMenu },
		{ "removal.table-columns", RemovalTableColumns },
		{ "removal.table-columns-whole", RemovalTableColumnsWhole },
		{ "removal.table-columns-merged", RemovalTableColumnsMerged },
		{ "removal.rejected", RemovalRejected },
		{
			"removal.rejected-stale-selection",
			RemovalRejectedStaleSelection,
		},
		{
			"removal.rejected-clears-temporary-paragraph",
			RemovalRejectedClearsTemporaryParagraph,
		},
		{ "snapshot.restore-after-removal", SnapshotRestoreAfterRemoval },
		{ "drop.blocks.same-container", DropBlocksSameContainer },
		{ "drop.blocks.adjacent", DropBlocksAdjacent },
		{ "drop.blocks.target-inside-source", DropBlocksTargetInsideSource },
		{ "drop.blocks.cross-container", DropBlocksCrossContainer },
		{ "drop.list-items.same-list", DropListItemsSameList },
		{ "drop.list-items.adjacent", DropListItemsAdjacent },
		{ "drop.list-items.to-blocks", DropListItemsToBlocks },
		{ "drop.list-items.other-list", DropListItemsOtherList },
		{ "drop.list-items.nested", DropListItemsNested },
		{ "drop.rejected", DropRejected },
		{ "drop.rejected-limits", DropRejectedLimits },
		{
			"drop.rejected-keeps-temporary-paragraph",
			DropRejectedKeepsTemporaryParagraph,
		},
		{ "snapshot.restore-after-drop", SnapshotRestoreAfterDrop },
		{ "tags.convert", TagsConvert },
		{ "tags.state", TagsState },
		{ "tags.conflicts", TagsConflicts },
		{ "leaf.lookup", LeafLookup },
		{ "leaf.invalid", LeafInvalid },
	};
	const auto total = int(checks.size());
	auto passed = 0;
	auto cases = 0;
	for (const auto &[name, run] : checks) {
		const auto report = run();
		cases += report.cases;
		if (!report.failures.isEmpty()) {
			const auto failures = report.failures.join(u"; "_q).toUtf8();
			std::printf("FAIL %s: %s\n", name, failures.constData());
			continue;
		}
		++passed;
		std::printf("PASS %s\n", name);
	}
	std::printf(
		"%d of %d checks passed, %d fixture cases.\n",
		passed,
		total,
		cases);
	return (passed == total) ? 0 : 1;
}

[[nodiscard]] bool HasUiResources() {
#ifdef Q_OS_MACOS
	const auto folder = base::Platform::BundledResourcesPath();
	const auto names = std::array{
		u"/lib_ui.rcc"_q,
		u"/external_microtex_bundled.rcc"_q,
	};
	for (const auto &name : names) {
		const auto path = folder + name;
		if (!QFile::exists(path)) {
			std::printf(
				"FAIL setup.resources: missing %s\n",
				path.toUtf8().constData());
			return false;
		}
	}
#endif // Q_OS_MACOS
	return true;
}

} // namespace

int main(int argc, char *argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	auto app = QGuiApplication(argc, argv);
	if (!HasUiResources()) {
		return 2;
	}
	style::SetDevicePixelRatio(1);
	style::internal::StartFonts();
	style::StartManager(style::kScaleDefault);
	return RunChecks();
}
