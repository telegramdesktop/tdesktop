/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_layout_tests.h"

#include "iv/markdown/iv_markdown_article_layout_structure.h"

namespace Iv::Markdown::Tests {
namespace {

using BlockKind = RichPage::BlockKind;

[[nodiscard]] RichPage::TableCell HeaderCell(
		const QString &text,
		int colspan = 1) {
	auto result = MakeCell(text, colspan);
	result.header = true;
	return result;
}

[[nodiscard]] RichPage::Block ExactFitTable() {
	return {
		.kind = BlockKind::Table,
		.bordered = true,
		.tableRows = {
			{
				.cells = {
					MakeCell(u"Alpha column"_q),
					MakeCell(u"Beta column"_q),
					MakeCell(u"Gamma column"_q),
				},
			},
		},
	};
}

[[nodiscard]] RichPage::Block QuotedTable() {
	return MakeBlock(BlockKind::Quote, QString(), { ExactFitTable() });
}

[[nodiscard]] QStringList SpansTopology(const QString &table) {
	return {
		table,
		u"0 row0 header cells=2"_q,
		u"0 row0 cell0 column=0 span=1x1 header"_q,
		u"0 row0 cell1 column=1 span=2x1 header"_q,
		u"0 row1 cells=3"_q,
		u"0 row1 cell0 column=0 span=1x2 middle"_q,
		u"0 row1 cell1 column=1 span=1x1"_q,
		u"0 row1 cell2 column=2 span=1x1"_q,
		u"0 row2 cells=2"_q,
		u"0 row2 cell0 column=1 span=1x1"_q,
		u"0 row2 cell1 column=2 span=1x1"_q,
	};
}

void ExpectSpansTable(
		Expect &expect,
		bool bordered,
		int width,
		const QStringList &expected) {
	const auto page = SpansTablePage(bordered, !bordered);
	const auto what = bordered ? u"spans table"_q : u"compact table"_q;
	const auto table = bordered
		? u"0 Table bordered columns=3 children=0"_q
		: u"0 Table compact columns=3 children=0"_q;
	if (ExpectTopology(expect, what, page, SpansTopology(table))) {
		ExpectFreshAndRetained(expect, what, page, width, expected);
	}
}

[[nodiscard]] QString TableSpansWide() {
	const auto expected = QStringList{
		u"0 Table outer=(18,0,564,167) content=(18,0,564,167)"
			u" text=(18,0,564,24) table=(18,31,564,136)"
			u" visibleTable=(18,31,564,136) textWidth=564"
			u" columns=[148,110,302]"_q,
		u"0 row0 outer=(19,32,562,44)"_q,
		u"0 row0 cell0 outer=(19,32,148,44) text=(27,42,132,24)"
			u" textWidth=132"_q,
		u"0 row0 cell1 outer=(168,32,413,44) text=(176,42,397,24)"
			u" textWidth=397"_q,
		u"0 row1 outer=(19,77,562,44)"_q,
		u"0 row1 cell0 outer=(19,77,148,89) text=(27,109,132,24)"
			u" textWidth=132"_q,
		u"0 row1 cell1 outer=(168,77,110,44) text=(176,87,94,24)"
			u" textWidth=94"_q,
		u"0 row1 cell2 outer=(279,77,302,44) text=(287,87,286,24)"
			u" textWidth=286"_q,
		u"0 row2 outer=(19,122,562,44)"_q,
		u"0 row2 cell0 outer=(168,122,110,44) text=(176,132,94,24)"
			u" textWidth=94"_q,
		u"0 row2 cell1 outer=(279,122,302,44) text=(287,132,286,24)"
			u" textWidth=286"_q,
		u"bottom=167 maxRight=564 overflow=0"_q,
	};
	auto expect = Expect();
	ExpectSpansTable(expect, true, 600, expected);
	return expect.result();
}

[[nodiscard]] QString TableSpansNarrow() {
	const auto expected = QStringList{
		u"0 Table outer=(18,0,164,231) content=(18,0,164,231)"
			u" text=(18,0,164,24) table=(18,31,200,184)"
			u" visibleTable=(18,31,164,184) scrollViewport=(18,31,164,184)"
			u" scrollLogical=(18,31,200,184) scrollTrack=(18,219,164,12)"
			u" tableTrack=(18,219,164,12) textWidth=164 scrollMax=36"
			u" overflowed columns=[69,31,96]"_q,
		u"0 row0 outer=(19,32,198,44)"_q,
		u"0 row0 cell0 outer=(19,32,69,44) text=(27,42,53,24) textWidth=53"_q,
		u"0 row0 cell1 outer=(89,32,128,44) text=(97,42,112,24)"
			u" textWidth=112"_q,
		u"0 row1 outer=(19,77,198,92)"_q,
		u"0 row1 cell0 outer=(19,77,69,137) text=(27,133,53,24) textWidth=53"_q,
		u"0 row1 cell1 outer=(89,77,31,92) text=(97,87,15,24) textWidth=15"_q,
		u"0 row1 cell2 outer=(121,77,96,92) text=(129,87,80,72) textWidth=80"_q,
		u"0 row2 outer=(19,170,198,44)"_q,
		u"0 row2 cell0 outer=(89,170,31,44) text=(97,180,15,24) textWidth=15"_q,
		u"0 row2 cell1 outer=(121,170,96,44) text=(129,180,80,24)"
			u" textWidth=80"_q,
		u"bottom=231 maxRight=164 overflow=36"_q,
	};
	auto expect = Expect();
	ExpectSpansTable(expect, true, 200, expected);
	return expect.result();
}

[[nodiscard]] QString TableBorderlessCompact() {
	auto expect = Expect();
	ExpectSpansTable(expect, false, 600, BorderlessCompactGeometry());
	return expect.result();
}

[[nodiscard]] QString TableExactFit() {
	const auto fit = QStringList{
		u"0 Table outer=(18,0,289,70) content=(18,0,289,70)"
			u" table=(18,0,289,70) visibleTable=(18,0,289,70)"
			u" columns=[96,93,96]"_q,
		u"0 row0 outer=(19,1,287,68)"_q,
		u"0 row0 cell0 outer=(19,1,96,68) text=(27,11,80,48) textWidth=80"_q,
		u"0 row0 cell1 outer=(116,1,93,68) text=(124,11,77,24) textWidth=77"_q,
		u"0 row0 cell2 outer=(210,1,96,68) text=(218,11,80,48) textWidth=80"_q,
		u"bottom=70 maxRight=289 overflow=0"_q,
	};
	const auto below = QStringList{
		u"0 Table outer=(18,0,288,86) content=(18,0,288,86)"
			u" table=(18,0,289,70) visibleTable=(18,0,288,70)"
			u" scrollViewport=(18,0,288,70) scrollLogical=(18,0,289,70)"
			u" scrollTrack=(18,74,288,12) tableTrack=(18,74,288,12)"
			u" scrollMax=1 overflowed columns=[96,93,96]"_q,
		u"0 row0 outer=(19,1,287,68)"_q,
		u"0 row0 cell0 outer=(19,1,96,68) text=(27,11,80,48) textWidth=80"_q,
		u"0 row0 cell1 outer=(116,1,93,68) text=(124,11,77,24) textWidth=77"_q,
		u"0 row0 cell2 outer=(210,1,96,68) text=(218,11,80,48) textWidth=80"_q,
		u"bottom=86 maxRight=288 overflow=1"_q,
	};
	const auto minimum = QStringList{
		u"0 Table outer=(18,0,1,86) content=(18,0,1,86) table=(18,0,289,70)"
			u" visibleTable=(18,0,1,70) scrollViewport=(18,0,1,70)"
			u" scrollLogical=(18,0,289,70) scrollTrack=(18,74,1,12)"
			u" tableTrack=(18,74,1,12) scrollMax=288 overflowed"
			u" columns=[96,93,96]"_q,
		u"0 row0 outer=(19,1,287,68)"_q,
		u"0 row0 cell0 outer=(19,1,96,68) text=(27,11,80,48) textWidth=80"_q,
		u"0 row0 cell1 outer=(116,1,93,68) text=(124,11,77,24) textWidth=77"_q,
		u"0 row0 cell2 outer=(210,1,96,68) text=(218,11,80,48) textWidth=80"_q,
		u"bottom=86 maxRight=1 overflow=288"_q,
	};
	const auto topology = QStringList{
		u"0 Table bordered columns=3 children=0"_q,
		u"0 row0 cells=3"_q,
		u"0 row0 cell0 column=0 span=1x1"_q,
		u"0 row0 cell1 column=1 span=1x1"_q,
		u"0 row0 cell2 column=2 span=1x1"_q,
	};
	auto expect = Expect();
	const auto page = MakePage({ ExactFitTable() });
	const auto what = u"exact fit"_q;
	if (ExpectTopology(expect, what, page, topology)) {
		ExpectFreshAndRetained(expect, what, page, 325, fit);
		ExpectFreshAndRetained(expect, what, page, 324, below);
		ExpectFreshAndRetained(expect, what, page, 1, minimum);
	}
	return expect.result();
}

[[nodiscard]] RichPage NestedOwnerPage() {
	return MakePage({
		{
			.kind = BlockKind::Details,
			.text = MakeText(u"Nested owner"_q),
			.open = true,
			.blocks = { QuotedTable() },
		},
		ExactFitTable(),
		MakeBlock(BlockKind::Paragraph, u"After"_q),
	});
}

void ExpectNestedOwner(Expect &expect, const QStringList &expected) {
	const auto page = NestedOwnerPage();
	const auto what = u"nested owner"_q;
	const auto run = LayoutFresh(expect, what, page, 300);
	if (!run
		|| !expect.number(what + u" roots"_q, int(run->blocks.size()), 3)
		|| !expect.number(
			what + u" details children"_q,
			int(run->blocks[0].children.size()),
			1)) {
		return;
	}
	const auto ancestor = run->blocks[0].horizontalScrollMax;
	const auto owner = run->blocks[0].children[0].horizontalScrollMax;
	const auto root = run->blocks[1].horizontalScrollMax;
	expect.number(what + u" ancestor range"_q, ancestor, 0);
	expect.flag(what + u" descendant range positive"_q, owner > 0, true);
	expect.flag(
		what + u" second root range smaller"_q,
		(root > 0) && (root < owner),
		true);
	expect.number(
		what + u" last root range"_q,
		run->blocks[2].horizontalScrollMax,
		0);
	ExpectFreshAndRetained(expect, what, page, 300, expected);
}

[[nodiscard]] QString OverflowOwners() {
	const auto fits = QStringList{
		u"0 Quote outer=(18,0,364,58) content=(28,6,334,46) textWidth=334"_q,
		u"0.0 Table outer=(28,6,334,46) content=(28,6,334,46)"
			u" table=(28,6,334,46) visibleTable=(28,6,334,46)"
			u" columns=[108,101,121]"_q,
		u"0.0 row0 outer=(29,7,332,44)"_q,
		u"0.0 row0 cell0 outer=(29,7,108,44) text=(37,17,92,24) textWidth=92"_q,
		u"0.0 row0 cell1 outer=(138,7,101,44) text=(146,17,85,24)"
			u" textWidth=85"_q,
		u"0.0 row0 cell2 outer=(240,7,121,44) text=(248,17,105,24)"
			u" textWidth=105"_q,
		u"bottom=58 maxRight=364 overflow=0"_q,
	};
	const auto quoteOwns = QStringList{
		u"0 Quote outer=(18,0,264,98) content=(28,6,234,70)"
			u" scrollViewport=(28,6,234,70) scrollLogical=(28,6,289,70)"
			u" scrollTrack=(28,80,234,12) textWidth=234 scrollMax=55"_q,
		u"0.0 Table outer=(28,6,289,70) content=(28,6,289,70)"
			u" table=(28,6,289,70) visibleTable=(28,6,289,70)"
			u" columns=[96,93,96]"_q,
		u"0.0 row0 outer=(29,7,287,68)"_q,
		u"0.0 row0 cell0 outer=(29,7,96,68) text=(37,17,80,48) textWidth=80"_q,
		u"0.0 row0 cell1 outer=(126,7,93,68) text=(134,17,77,24)"
			u" textWidth=77"_q,
		u"0.0 row0 cell2 outer=(220,7,96,68) text=(228,17,80,48)"
			u" textWidth=80"_q,
		u"bottom=98 maxRight=264 overflow=55"_q,
	};
	const auto noOwner = QStringList{
		u"0 Quote outer=(18,0,184,82) content=(28,6,154,70) textWidth=154"_q,
		u"0.0 Table outer=(28,6,154,70) content=(28,6,154,70)"
			u" table=(28,6,289,70) visibleTable=(28,6,154,70) overflowed"
			u" columns=[96,93,96]"_q,
		u"0.0 row0 outer=(29,7,287,68)"_q,
		u"0.0 row0 cell0 outer=(29,7,96,68) text=(37,17,80,48) textWidth=80"_q,
		u"0.0 row0 cell1 outer=(126,7,93,68) text=(134,17,77,24)"
			u" textWidth=77"_q,
		u"0.0 row0 cell2 outer=(220,7,96,68) text=(228,17,80,48)"
			u" textWidth=80"_q,
		u"bottom=82 maxRight=184 overflow=0"_q,
	};
	const auto nested = QStringList{
		u"0 Details outer=(0,0,300,164) content=(18,54,264,98)"
			u" text=(44,12,238,18) header=(0,0,300,42) body=(0,42,300,122)"
			u" icon=(18,12,18,18) textWidth=238"_q,
		u"0.0 Quote outer=(18,54,264,98) content=(28,60,234,70)"
			u" scrollViewport=(28,60,234,70) scrollLogical=(28,60,289,70)"
			u" scrollTrack=(28,134,234,12) textWidth=234 scrollMax=55"_q,
		u"0.0.0 Table outer=(28,60,289,70) content=(28,60,289,70)"
			u" table=(28,60,289,70) visibleTable=(28,60,289,70)"
			u" columns=[96,93,96]"_q,
		u"0.0.0 row0 outer=(29,61,287,68)"_q,
		u"0.0.0 row0 cell0 outer=(29,61,96,68) text=(37,71,80,48)"
			u" textWidth=80"_q,
		u"0.0.0 row0 cell1 outer=(126,61,93,68) text=(134,71,77,24)"
			u" textWidth=77"_q,
		u"0.0.0 row0 cell2 outer=(220,61,96,68) text=(228,71,80,48)"
			u" textWidth=80"_q,
		u"1 Table outer=(18,176,264,86) content=(18,176,264,86)"
			u" table=(18,176,289,70) visibleTable=(18,176,264,70)"
			u" scrollViewport=(18,176,264,70) scrollLogical=(18,176,289,70)"
			u" scrollTrack=(18,250,264,12) tableTrack=(18,250,264,12)"
			u" scrollMax=25 overflowed columns=[96,93,96]"_q,
		u"1 row0 outer=(19,177,287,68)"_q,
		u"1 row0 cell0 outer=(19,177,96,68) text=(27,187,80,48) textWidth=80"_q,
		u"1 row0 cell1 outer=(116,177,93,68) text=(124,187,77,24)"
			u" textWidth=77"_q,
		u"1 row0 cell2 outer=(210,177,96,68) text=(218,187,80,48)"
			u" textWidth=80"_q,
		u"2 Paragraph outer=(18,274,264,24) content=(18,274,264,24)"
			u" text=(18,274,264,24) textWidth=264"_q,
		u"bottom=298 maxRight=300 overflow=55"_q,
	};
	const auto leaf = QStringList{
		u"0 Paragraph outer=(18,0,1,40) content=(18,0,1,24)"
			u" text=(18,0,96,24) scrollViewport=(18,0,1,24)"
			u" scrollLogical=(18,0,96,24) scrollTrack=(18,28,1,12)"
			u" textWidth=96 scrollMax=6 overflowed"_q,
		u"bottom=40 maxRight=1 overflow=6"_q,
	};
	auto expect = Expect();
	const auto quoted = MakePage({ QuotedTable() });
	const auto what = u"quoted table"_q;
	ExpectFreshAndRetained(expect, what, quoted, 400, fits);
	ExpectFreshAndRetained(expect, what, quoted, 300, quoteOwns);
	ExpectFreshAndRetained(expect, what, quoted, 220, noOwner);
	ExpectNestedOwner(expect, nested);
	ExpectFreshAndRetained(
		expect,
		u"leaf"_q,
		MakePage({ MakeBlock(BlockKind::Paragraph, u"x"_q) }),
		1,
		leaf);
	return expect.result();
}

} // namespace

QStringList BorderlessCompactGeometry() {
	return {
		u"0 Table outer=(18,0,564,133) content=(18,0,564,133)"
			u" text=(18,0,564,24) table=(18,31,564,102)"
			u" visibleTable=(18,31,564,102) textWidth=564"
			u" columns=[150,111,303]"_q,
		u"0 row0 outer=(18,31,564,34)"_q,
		u"0 row0 cell0 outer=(18,31,150,34) text=(26,36,134,24)"
			u" textWidth=134"_q,
		u"0 row0 cell1 outer=(168,31,414,34) text=(176,36,398,24)"
			u" textWidth=398"_q,
		u"0 row1 outer=(18,65,564,34)"_q,
		u"0 row1 cell0 outer=(18,65,150,68) text=(26,87,134,24)"
			u" textWidth=134"_q,
		u"0 row1 cell1 outer=(168,65,111,34) text=(176,70,95,24)"
			u" textWidth=95"_q,
		u"0 row1 cell2 outer=(279,65,303,34) text=(287,70,287,24)"
			u" textWidth=287"_q,
		u"0 row2 outer=(18,99,564,34)"_q,
		u"0 row2 cell0 outer=(168,99,111,34) text=(176,104,95,24)"
			u" textWidth=95"_q,
		u"0 row2 cell1 outer=(279,99,303,34) text=(287,104,287,24)"
			u" textWidth=287"_q,
		u"bottom=133 maxRight=564 overflow=0"_q,
	};
}

RichPage SpansTablePage(bool bordered, bool compact) {
	auto north = MakeCell(u"North"_q, 1, 2);
	north.verticalAlignment = RichPage::TableVerticalAlignment::Middle;
	return MakePage({
		{
			.kind = BlockKind::Table,
			.text = MakeText(u"Quarterly figures"_q),
			.bordered = bordered,
			.compact = compact,
			.tableRows = {
				{
					.cells = {
						HeaderCell(u"Region"_q),
						HeaderCell(u"Totals"_q, 2),
					},
				},
				{
					.cells = {
						north,
						MakeCell(u"12"_q),
						MakeCell(u"A noticeably longer cell that wraps"_q),
					},
				},
				{ .cells = { MakeCell(u"14"_q), MakeCell(u"short"_q) } },
			},
		},
	});
}

std::vector<Check> LayoutTableChecks() {
	return {
		{ u"layout.table-spans-wide"_q, TableSpansWide },
		{ u"layout.table-spans-narrow"_q, TableSpansNarrow },
		{ u"layout.table-borderless-compact"_q, TableBorderlessCompact },
		{ u"layout.table-exact-fit"_q, TableExactFit },
		{ u"layout.overflow-owners"_q, OverflowOwners },
	};
}

} // namespace Iv::Markdown::Tests
