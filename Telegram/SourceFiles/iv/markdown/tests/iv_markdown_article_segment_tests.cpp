/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_tests.h"

#include "base/flat_set.h"
#include "iv/markdown/iv_markdown_article_selection.h"
#include "lang/lang_text_entity.h"

#include <array>

namespace Iv::Markdown::Tests {
namespace {

enum class LeafRole {
	None,
	Body,
	Label,
	Subtitle,
	Child,
	Cell,
};

struct ExpectedSegment {
	SelectableSegmentKind kind = SelectableSegmentKind::TextLeaf;
	LeafRole leaf = LeafRole::None;
	QRect outer;
	int length = 1;
	int parent = -1;
	int table = -1;
};

struct BlockKindRow {
	PreparedBlockKind kind = PreparedBlockKind::Paragraph;
	std::vector<ExpectedSegment> segments;
	int segmentIndex = -1;
	int secondaryIndex = -1;
	int tertiaryIndex = -1;
};

struct KindCopy {
	SelectableSegmentKind kind = SelectableSegmentKind::TextLeaf;
	QString text;
	EntitiesInText entities;
	TextForMimeDataTags tags;
};

struct SegmentRects {
	QRect outer;
	QRect text;
};

struct HitRectCase {
	int border = 0;
	QRect cell;
	QRect expected;
};

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wswitch"
[[nodiscard]] int BlockKindRowIndex(PreparedBlockKind kind) {
	using Kind = PreparedBlockKind;
	switch (kind) {
	case Kind::Paragraph: return 0;
	case Kind::Thinking: return 1;
	case Kind::Heading: return 2;
	case Kind::CodeBlock: return 3;
	case Kind::Rule: return 4;
	case Kind::ButtonRow: return 5;
	case Kind::List: return 6;
	case Kind::ListItem: return 7;
	case Kind::Quote: return 8;
	case Kind::DisplayMath: return 9;
	case Kind::Table: return 10;
	case Kind::Details: return 11;
	case Kind::Photo: return 12;
	case Kind::Video: return 13;
	case Kind::Document: return 14;
	case Kind::Map: return 15;
	case Kind::Channel: return 16;
	case Kind::GroupedMedia: return 17;
	case Kind::RelatedArticle: return 18;
	case Kind::EmbedPost: return 19;
	case Kind::Placeholder: return 20;
	}
	return -1;
}

[[nodiscard]] int SegmentKindRowIndex(SelectableSegmentKind kind) {
	using Kind = SelectableSegmentKind;
	switch (kind) {
	case Kind::TextLeaf: return 0;
	case Kind::CodeBlock: return 1;
	case Kind::DisplayMath: return 2;
	case Kind::Table: return 3;
	case Kind::Placeholder: return 5;
	case Kind::Photo: return 6;
	case Kind::Media: return 7;
	case Kind::ButtonRow: return 8;
	}
	return -1;
}
#pragma clang diagnostic pop

void ExpectSegmentKindRows(
		Expect &expect,
		const QString &what,
		const std::vector<int> &kinds) {
	using Kind = SelectableSegmentKind;
	for (auto value = 0; SegmentKindRowIndex(Kind(value)) >= 0; ++value) {
		const auto row = SegmentKindRowIndex(Kind(value));
		expect.number(
			what + u" kind %1 row"_q.arg(value),
			(row < int(kinds.size())) ? kinds[row] : -1,
			value);
	}
}

[[nodiscard]] std::vector<BlockKindRow> BlockKindRows() {
	using Kind = PreparedBlockKind;
	using Segment = SelectableSegmentKind;
	const auto body = ExpectedSegment{
		.leaf = LeafRole::Body,
		.outer = QRect(10, 60, 200, 20),
		.length = 4,
	};
	const auto caption = ExpectedSegment{
		.leaf = LeafRole::Body,
		.outer = QRect(10, 60, 200, 20),
		.length = 4,
		.parent = 0,
	};
	const auto child = ExpectedSegment{
		.leaf = LeafRole::Child,
		.outer = QRect(0, 90, 100, 10),
		.length = 5,
	};
	const auto code = ExpectedSegment{
		.kind = Segment::CodeBlock,
		.leaf = LeafRole::Body,
		.outer = QRect(0, 0, 300, 100),
		.length = 4,
	};
	const auto buttons = ExpectedSegment{
		.kind = Segment::ButtonRow,
		.outer = QRect(0, 0, 300, 100),
	};
	const auto math = ExpectedSegment{
		.kind = Segment::DisplayMath,
		.outer = QRect(5, 5, 50, 20),
	};
	const auto tableCaption = ExpectedSegment{
		.leaf = LeafRole::Body,
		.outer = QRect(10, 60, 200, 20),
		.length = 4,
		.parent = 1,
	};
	const auto table = ExpectedSegment{
		.kind = Segment::Table,
		.outer = QRect(0, 0, 300, 50),
	};
	const auto cell = ExpectedSegment{
		.leaf = LeafRole::Cell,
		.outer = QRect(0, 0, 100, 30),
		.length = 4,
		.table = 1,
	};
	const auto summary = ExpectedSegment{
		.leaf = LeafRole::Body,
		.outer = QRect(0, 0, 300, 30),
		.length = 4,
	};
	const auto photo = ExpectedSegment{
		.kind = Segment::Photo,
		.outer = QRect(0, 0, 300, 50),
	};
	const auto media = ExpectedSegment{
		.kind = Segment::Media,
		.outer = QRect(0, 0, 300, 50),
	};
	const auto author = ExpectedSegment{
		.leaf = LeafRole::Label,
		.outer = QRect(10, 5, 100, 20),
		.length = 6,
	};
	const auto date = ExpectedSegment{
		.leaf = LeafRole::Subtitle,
		.outer = QRect(120, 5, 100, 20),
		.length = 4,
	};
	const auto placeholder = ExpectedSegment{
		.kind = Segment::Placeholder,
		.outer = QRect(0, 0, 300, 50),
	};
	return {
		{ Kind::Paragraph, { body, child }, 0 },
		{ Kind::Thinking, { body, child }, 0 },
		{ Kind::Heading, { body, child }, 0 },
		{ Kind::CodeBlock, { code, child }, 0 },
		{ Kind::Rule, { child } },
		{ Kind::ButtonRow, { buttons, child }, 0 },
		{ Kind::List, { child } },
		{ Kind::ListItem, { child } },
		{ Kind::Quote, { child } },
		{ Kind::DisplayMath, { math, child }, 0 },
		{ Kind::Table, { tableCaption, table, cell, child }, 1, 0 },
		{ Kind::Details, { summary, child }, 0 },
		{ Kind::Photo, { photo, caption, child }, 0, 1 },
		{ Kind::Video, { media, caption, child }, 0, 1 },
		{ Kind::Document, { media, caption, child }, 0, 1 },
		{ Kind::Map, { media, caption, child }, 0, 1 },
		{ Kind::Channel, { media, caption, child }, 0, 1 },
		{ Kind::GroupedMedia, { media, caption, child }, 0, 1 },
		{ Kind::RelatedArticle, { media, caption, child }, 0, 1 },
		{ Kind::EmbedPost, { author, date, child, body }, 0, 1, 3 },
		{ Kind::Placeholder, { placeholder, caption, child }, 0, 1 },
	};
}

[[nodiscard]] std::vector<LaidOutBlock> BlockKindFixture(
		PreparedBlockKind kind) {
	auto result = std::vector<LaidOutBlock>(1);
	auto &block = result.front();
	block.kind = kind;
	SetLeaf(block.leaf, u"Body"_q);
	SetLeaf(block.labelLeaf, u"Author"_q);
	SetLeaf(block.subtitleLeaf, u"Date"_q);
	block.outer = QRect(0, 0, 300, 100);
	block.textRect = QRect(10, 60, 200, 20);
	block.headerRect = QRect(0, 0, 300, 30);
	block.labelRect = QRect(10, 5, 100, 20);
	block.subtitleRect = QRect(120, 5, 100, 20);
	block.mediaRect = QRect(0, 0, 300, 50);
	block.visibleFormulaRect = QRect(5, 5, 50, 20);
	block.visibleTableRect = QRect(0, 0, 300, 50);
	auto &child = block.children.emplace_back();
	SetLeaf(child.leaf, u"Child"_q);
	child.textRect = QRect(0, 90, 100, 10);
	if (kind == PreparedBlockKind::Table) {
		block.tableColumnWidths = { 100 };
		auto &cell = block.tableRows.emplace_back().cells.emplace_back();
		SetLeaf(cell.leaf, u"Cell"_q);
		cell.outer = QRect(0, 0, 100, 30);
	}
	return result;
}

[[nodiscard]] const Ui::Text::String *RoleLeaf(
		const LaidOutBlock &block,
		LeafRole role) {
	switch (role) {
	case LeafRole::None: return nullptr;
	case LeafRole::Body: return &block.leaf;
	case LeafRole::Label: return &block.labelLeaf;
	case LeafRole::Subtitle: return &block.subtitleLeaf;
	case LeafRole::Child: return &block.children.front().leaf;
	case LeafRole::Cell: return &block.tableRows.front().cells.front().leaf;
	}
	return nullptr;
}

void ExpectBlockKindSegments(
		Expect &expect,
		const QString &what,
		const LaidOutBlock &block,
		const std::vector<SelectableSegment> &segments,
		const BlockKindRow &row) {
	expect.number(
		what + u" segment index"_q,
		block.segmentIndex,
		row.segmentIndex);
	expect.number(
		what + u" secondary index"_q,
		block.secondarySegmentIndex,
		row.secondaryIndex);
	expect.number(
		what + u" tertiary index"_q,
		block.tertiarySegmentIndex,
		row.tertiaryIndex);
	const auto count = int(row.segments.size());
	if (!expect.number(what + u" count"_q, int(segments.size()), count)) {
		return;
	}
	for (auto i = 0; i != count; ++i) {
		const auto &segment = segments[i];
		const auto &expected = row.segments[i];
		const auto prefix = what + u" segment %1 "_q.arg(i);
		const auto owner = (expected.leaf == LeafRole::Child)
			? &block.children.front()
			: &block;
		const auto cell = (expected.leaf == LeafRole::Cell)
			? &block.tableRows.front().cells.front()
			: nullptr;
		expect.number(prefix + u"index"_q, segment.index, i);
		expect.number(
			prefix + u"kind"_q,
			int(segment.kind),
			int(expected.kind));
		expect.flag(
			prefix + u"leaf"_q,
			segment.leaf == RoleLeaf(block, expected.leaf),
			true);
		expect.flag(prefix + u"block"_q, segment.block == owner, true);
		expect.flag(prefix + u"cell"_q, segment.cell == cell, true);
		expect.rect(prefix + u"outer"_q, segment.outerRect, expected.outer);
		expect.number(
			prefix + u"length"_q,
			segment.length,
			expected.length);
		expect.number(
			prefix + u"parent"_q,
			segment.parentSegmentIndex,
			expected.parent);
		expect.number(
			prefix + u"table"_q,
			segment.tableSegmentIndex,
			expected.table);
	}
	if (block.kind == PreparedBlockKind::Table) {
		const auto &cell = block.tableRows.front().cells.front();
		expect.number(what + u" cell segment"_q, cell.segmentIndex, 2);
		expect.number(what + u" cell table"_q, cell.tableSegmentIndex, 1);
	}
}

[[nodiscard]] QString BlockKinds() {
	static_assert(int(PreparedBlockKind::Placeholder) == 20);
	const auto rows = BlockKindRows();
	auto expect = Expect();
	if (!expect.number(u"rows"_q, int(rows.size()), 21)) {
		return expect.result();
	}
	for (auto i = 0; i != int(rows.size()); ++i) {
		const auto &row = rows[i];
		const auto what = u"kind %1"_q.arg(i);
		expect.number(what + u" order"_q, int(row.kind), i);
		expect.number(
			what + u" declared row"_q,
			BlockKindRowIndex(row.kind),
			i);
		auto blocks = BlockKindFixture(row.kind);
		auto segments = std::vector<SelectableSegment>();
		CollectSelectableSegments(&blocks, &segments);
		ExpectBlockKindSegments(expect, what, blocks.front(), segments, row);
	}
	const auto past = int(rows.size());
	expect.number(
		u"kind %1 past inventory"_q.arg(past),
		BlockKindRowIndex(PreparedBlockKind(past)),
		-1);
	return expect.result();
}

[[nodiscard]] QString EmptyAndSkip() {
	auto blocks = std::vector<LaidOutBlock>(2);
	SetLeaf(blocks[1].leaf, u"Hello"_q);
	blocks[1].skipBlockLength = 2;
	auto segments = std::vector<SelectableSegment>();
	CollectSelectableSegments(&blocks, &segments);
	auto expect = Expect();
	expect.number(u"empty block index"_q, blocks[0].segmentIndex, -1);
	expect.number(u"skip block index"_q, blocks[1].segmentIndex, 0);
	if (expect.number(u"count"_q, int(segments.size()), 1)) {
		expect.number(u"length"_q, segments.front().length, 3);
	}
	return expect.result();
}

[[nodiscard]] Article CopyKindsArticle() {
	using Kind = PreparedBlockKind;
	auto result = Article();
	auto &blocks = result.blocks;
	blocks.resize(8);
	SetLeaf(blocks[0].leaf, u"Hello world"_q);
	auto &code = blocks[1];
	code.kind = Kind::CodeBlock;
	code.codeText = tr::marked(u"a\tb"_q);
	code.codeLanguage = u"cpp"_q;
	SetLeaf(code.leaf, CodeBlockDisplayText(code.codeText.text));
	blocks[2].kind = Kind::DisplayMath;
	blocks[2].copyText = u"x^2"_q;
	auto &table = blocks[3];
	table.kind = Kind::Table;
	table.tableColumnWidths = { 50 };
	auto &cell = table.tableRows.emplace_back().cells.emplace_back();
	SetLeaf(cell.leaf, u"Cell"_q);
	blocks[4].kind = Kind::Placeholder;
	blocks[4].labelText = u"Label"_q;
	blocks[5].kind = Kind::Photo;
	blocks[6].kind = Kind::Map;
	blocks[6].copyText = u"Map pin"_q;
	blocks[7].kind = Kind::ButtonRow;
	for (const auto &label : { u"One"_q, QString(), u"Two"_q }) {
		SetLeaf(blocks[7].buttons.emplace_back().label, label);
	}
	CollectSelectableSegments(&result.blocks, &result.segments);
	return result;
}

[[nodiscard]] QString CopyKinds() {
	static_assert(int(SelectableSegmentKind::ButtonRow) == 7);
	using Segment = SelectableSegmentKind;
	const auto pre = EntityInText(EntityType::Pre, 0, 3, u"cpp"_q);
	const auto math = TextForMimeDataTag{
		.offset = 0,
		.length = 3,
		.id = u"iv-math"_q,
	};
	const auto expected = std::vector<KindCopy>{
		{ Segment::TextLeaf, u"Hello world"_q },
		{ Segment::CodeBlock, u"a\tb"_q, { pre } },
		{ Segment::DisplayMath, u"x^2"_q, {}, { math } },
		{ Segment::Table, u"Cell"_q },
		{ Segment::TextLeaf, u"Cell"_q },
		{ Segment::Placeholder, u"Label"_q },
		{ Segment::Photo, u"Photo"_q },
		{ Segment::Media, u"Map pin"_q },
		{ Segment::ButtonRow, u"One Two"_q },
	};
	auto expectedKinds = std::vector<int>();
	for (const auto &row : expected) {
		expectedKinds.push_back(int(row.kind));
	}
	const auto article = CopyKindsArticle();
	const auto &segments = article.segments;
	auto expect = Expect();
	ExpectSegmentKindRows(expect, u"copy inventory"_q, expectedKinds);
	if (!expect.number(u"segments"_q, int(segments.size()), 9)) {
		return expect.result();
	}
	auto seen = base::flat_set<SelectableSegmentKind>();
	for (auto i = 0; i != int(expected.size()); ++i) {
		const auto &segment = segments[i];
		const auto &row = expected[i];
		const auto what = u"segment %1 "_q.arg(i);
		expect.number(what + u"kind"_q, int(segment.kind), int(row.kind));
		expect.copy(
			what + u"copy"_q,
			TextForSegment(segment),
			row.text,
			row.entities,
			row.tags);
		seen.emplace(segment.kind);
	}
	expect.number(u"distinct kinds"_q, int(seen.size()), 8);
	expect.copy(
		u"whole selection"_q,
		TextForSelectedSegments(segments, { { 0, 0 }, { 8, 1 } }, nullptr),
		u"Hello world\na\tb\nx^2\nCell\nLabel\nPhoto\nMap pin\nOne Two"_q,
		{ EntityInText(EntityType::Pre, 12, 3, u"cpp"_q) },
		{ { .offset = 16, .length = 3, .id = u"iv-math"_q } });
	return expect.result();
}

[[nodiscard]] QString CopyMediaFallbacks() {
	using Kind = PreparedBlockKind;
	auto article = Article();
	auto &blocks = article.blocks;
	blocks.resize(4);
	blocks[0].kind = Kind::Placeholder;
	blocks[0].labelText = u"Label"_q;
	blocks[0].copyText = u"Copy"_q;
	SetLeaf(blocks[0].leaf, u"Cap"_q);
	blocks[1].kind = Kind::Photo;
	SetLeaf(blocks[1].leaf, u"Cap"_q);
	blocks[2].kind = Kind::Video;
	blocks[2].copyText = u"Clip"_q;
	blocks[2].labelText = u"Lbl"_q;
	blocks[3].kind = Kind::Document;
	CollectSelectableSegments(&article.blocks, &article.segments);
	const auto expected = std::array{
		u"Label\nCap"_q,
		u"Photo\nCap"_q,
		u"Clip"_q,
		QString(),
	};
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(article.segments.size()), 4)) {
		return expect.result();
	}
	for (auto i = 0; i != int(expected.size()); ++i) {
		expect.copy(
			u"block %1"_q.arg(i),
			TextForSegment(article.segments[i]),
			expected[i],
			{},
			{});
	}
	const auto detached = SelectableSegment{
		.kind = SelectableSegmentKind::Table,
	};
	expect.copy(
		u"table without block"_q,
		TextForSegment(detached),
		QString(),
		{},
		{});
	return expect.result();
}

[[nodiscard]] QString TextLeafPartial() {
	auto blocks = std::vector<LaidOutBlock>(1);
	SetLeaf(blocks[0].leaf, u"Hello world"_q);
	auto segments = std::vector<SelectableSegment>();
	CollectSelectableSegments(&blocks, &segments);
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(segments.size()), 1)) {
		return expect.result();
	}
	expect.copy(
		u"[6,11)"_q,
		TextForSegment(segments[0], TextSelection(6, 11)),
		u"world"_q,
		{},
		{});
	expect.copy(
		u"[6,65535)"_q,
		TextForSegment(segments[0], TextSelection(6, 65535)),
		u"world"_q,
		{},
		{});
	return expect.result();
}

[[nodiscard]] Article RefreshArticle() {
	using Kind = PreparedBlockKind;
	auto result = Article();
	auto &blocks = result.blocks;
	blocks.resize(9);
	auto &details = blocks[0];
	details.kind = Kind::Details;
	SetLeaf(details.leaf, u"Summary"_q);
	details.textRect = QRect(0, 0, 50, 20);
	details.headerRect = QRect(0, 0, 100, 30);
	auto &code = blocks[1];
	code.kind = Kind::CodeBlock;
	code.codeText = tr::marked(u"x"_q);
	SetLeaf(code.leaf, u"x"_q);
	code.outer = QRect(0, 40, 100, 30);
	code.textRect = QRect(0, 45, 100, 20);
	blocks[2].kind = Kind::DisplayMath;
	blocks[2].visibleFormulaRect = QRect(0, 80, 40, 20);
	auto &table = blocks[3];
	table.kind = Kind::Table;
	table.tableColumnWidths = { 50 };
	table.visibleTableRect = QRect(0, 110, 100, 40);
	table.tableBorder = 1;
	auto &cell = table.tableRows.emplace_back().cells.emplace_back();
	SetLeaf(cell.leaf, u"Cell"_q);
	cell.outer = QRect(0, 110, 50, 20);
	cell.textRect = QRect(5, 112, 40, 16);
	const auto media = std::array{
		std::pair{ Kind::Placeholder, QRect(0, 160, 100, 30) },
		std::pair{ Kind::Photo, QRect(0, 200, 100, 30) },
		std::pair{ Kind::Map, QRect(0, 240, 100, 30) },
	};
	for (auto i = 0; i != int(media.size()); ++i) {
		auto &block = blocks[4 + i];
		block.kind = media[i].first;
		block.mediaRect = media[i].second;
		block.visibleMediaRect = media[i].second;
	}
	blocks[7].kind = Kind::ButtonRow;
	blocks[7].outer = QRect(0, 280, 100, 30);
	auto &embed = blocks[8];
	embed.kind = Kind::EmbedPost;
	embed.outer = QRect(0, 320, 100, 60);
	SetLeaf(embed.labelLeaf, u"Author"_q);
	embed.labelRect = QRect(10, 325, 40, 20);
	SetLeaf(embed.subtitleLeaf, u"Date"_q);
	embed.subtitleRect = QRect(60, 325, 30, 20);
	SetLeaf(embed.leaf, u"Body"_q);
	embed.textRect = QRect(0, 350, 100, 20);
	CollectSelectableSegments(&result.blocks, &result.segments);
	return result;
}

void MoveRefreshGeometry(std::vector<LaidOutBlock> &blocks) {
	blocks[0].textRect = QRect(-20, 0, 50, 20);
	blocks[1].contentRect = QRect(10, 40, 60, 30);
	blocks[1].textRect = QRect(-15, 45, 120, 20);
	blocks[2].visibleFormulaRect = QRect(0, 80, 25, 20);
	blocks[3].visibleTableRect = QRect(0, 110, 30, 40);
	auto &cell = blocks[3].tableRows.front().cells.front();
	cell.outer = QRect(-10, 110, 50, 20);
	cell.textRect = QRect(-5, 112, 40, 16);
	blocks[4].visibleMediaRect = QRect(0, 160, 60, 30);
	blocks[5].visibleMediaRect = QRect(0, 200, 70, 30);
	blocks[6].visibleMediaRect = QRect(0, 240, 80, 30);
	blocks[7].outer = QRect(0, 280, 90, 30);
	blocks[8].labelRect = QRect(-10, 325, 40, 20);
	blocks[8].subtitleRect = QRect(80, 325, 40, 20);
	blocks[8].textRect = QRect(0, 350, 120, 20);
}

void ExpectRects(
		Expect &expect,
		const QString &what,
		const std::vector<SelectableSegment> &segments,
		const std::vector<SegmentRects> &expected) {
	for (auto i = 0; i != int(expected.size()); ++i) {
		const auto prefix = what + u" segment %1 "_q.arg(i);
		expect.rect(
			prefix + u"outer"_q,
			segments[i].outerRect,
			expected[i].outer);
		expect.rect(
			prefix + u"text"_q,
			segments[i].textRect,
			expected[i].text);
	}
}

[[nodiscard]] QString RefreshKinds() {
	const auto before = std::vector<SegmentRects>{
		{ QRect(0, 0, 100, 30), QRect(0, 0, 50, 20) },
		{ QRect(0, 40, 100, 30), QRect(0, 45, 100, 20) },
		{ QRect(0, 80, 40, 20) },
		{ QRect(0, 110, 100, 40) },
		{ QRect(0, 110, 51, 21), QRect(5, 112, 40, 16) },
		{ QRect(0, 160, 100, 30) },
		{ QRect(0, 200, 100, 30) },
		{ QRect(0, 240, 100, 30) },
		{ QRect(0, 280, 100, 30) },
		{ QRect(10, 325, 40, 20), QRect(10, 325, 40, 20) },
		{ QRect(60, 325, 30, 20), QRect(60, 325, 30, 20) },
		{ QRect(0, 350, 100, 20), QRect(0, 350, 100, 20) },
	};
	const auto after = std::vector<SegmentRects>{
		{ QRect(0, 0, 100, 30), QRect(0, 0, 30, 20) },
		{ QRect(0, 40, 100, 30), QRect(10, 45, 60, 20) },
		{ QRect(0, 80, 25, 20) },
		{ QRect(0, 110, 30, 40) },
		{ QRect(-10, 110, 51, 21), QRect(-5, 112, 40, 16) },
		{ QRect(0, 160, 60, 30) },
		{ QRect(0, 200, 70, 30) },
		{ QRect(0, 240, 80, 30) },
		{ QRect(0, 280, 90, 30) },
		{ QRect(0, 325, 30, 20), QRect(0, 325, 30, 20) },
		{ QRect(80, 325, 20, 20), QRect(80, 325, 20, 20) },
		{ QRect(0, 350, 100, 20), QRect(0, 350, 100, 20) },
	};
	auto article = RefreshArticle();
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(article.segments.size()), 12)) {
		return expect.result();
	}
	ExpectRects(expect, u"before"_q, article.segments, before);
	MoveRefreshGeometry(article.blocks);
	RefreshScrollableSegmentRects(article.blocks, &article.segments);
	ExpectRects(expect, u"after"_q, article.segments, after);
	return expect.result();
}

void ExpectRefreshIdentity(
		Expect &expect,
		const QString &what,
		const Article &article,
		const std::vector<int> &expectedKinds) {
	const auto &segments = article.segments;
	const auto cellSegment = 4;
	const auto cell = &article.blocks[3].tableRows.front().cells.front();
	auto indices = std::vector<int>();
	auto kinds = std::vector<int>();
	auto lengths = std::vector<int>();
	for (auto i = 0; i != int(segments.size()); ++i) {
		const auto &segment = segments[i];
		indices.push_back(segment.index);
		kinds.push_back(int(segment.kind));
		lengths.push_back(segment.length);
		expect.flag(
			what + u" segment %1 cell"_q.arg(i),
			segment.cell == ((i == cellSegment) ? cell : nullptr),
			true);
	}
	expect.numbers(
		what + u" indices"_q,
		indices,
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
	expect.numbers(what + u" kinds"_q, kinds, expectedKinds);
	expect.numbers(
		what + u" lengths"_q,
		lengths,
		{ 7, 1, 1, 1, 4, 1, 1, 1, 1, 6, 4, 4 });
	expect.copy(
		what + u" copy"_q,
		TextForSelectedSegments(segments, { { 0, 0 }, { 11, 4 } }, nullptr),
		u"Summary\nx\nCell\nPhoto\nAuthor\nDate\nBody"_q,
		{ EntityInText(EntityType::Pre, 8, 1) },
		{});
}

[[nodiscard]] QString RefreshKeepsIdentity() {
	const auto kinds = std::vector<int>{ 0, 1, 2, 3, 0, 4, 5, 6, 7, 0, 0, 0 };
	auto article = RefreshArticle();
	auto expect = Expect();
	ExpectSegmentKindRows(expect, u"refresh inventory"_q, kinds);
	ExpectRefreshIdentity(expect, u"before"_q, article, kinds);
	MoveRefreshGeometry(article.blocks);
	RefreshScrollableSegmentRects(article.blocks, &article.segments);
	ExpectRefreshIdentity(expect, u"after"_q, article, kinds);
	return expect.result();
}

[[nodiscard]] QString TableCellHitRects() {
	const auto cases = std::array{
		HitRectCase{ 1, QRect(-10, 110, 50, 20), QRect(-10, 110, 51, 21) },
		HitRectCase{ 1, QRect(0, 110, 20, 20), QRect(0, 110, 21, 21) },
		HitRectCase{ 1, QRect(10, 110, 20, 20), QRect(10, 110, 20, 21) },
		HitRectCase{ 0, QRect(0, 110, 20, 20), QRect(0, 110, 20, 20) },
	};
	auto block = LaidOutBlock();
	block.kind = PreparedBlockKind::Table;
	block.visibleTableRect = QRect(0, 110, 30, 40);
	auto cell = LaidOutTableCell();
	auto expect = Expect();
	for (auto i = 0; i != int(cases.size()); ++i) {
		block.tableBorder = cases[i].border;
		cell.outer = cases[i].cell;
		expect.rect(
			u"case %1"_q.arg(i),
			TableCellHitRect(block, cell),
			cases[i].expected);
	}
	return expect.result();
}

} // namespace

std::vector<Check> SegmentChecks() {
	return {
		{ u"segment.block-kinds"_q, BlockKinds },
		{ u"segment.empty-and-skip"_q, EmptyAndSkip },
		{ u"segment.copy-kinds"_q, CopyKinds },
		{ u"segment.copy-media-fallbacks"_q, CopyMediaFallbacks },
		{ u"segment.text-leaf-partial"_q, TextLeafPartial },
		{ u"segment.refresh-kinds"_q, RefreshKinds },
		{ u"segment.refresh-keeps-identity"_q, RefreshKeepsIdentity },
		{ u"segment.table-cell-hit-rect"_q, TableCellHitRects },
	};
}

} // namespace Iv::Markdown::Tests
