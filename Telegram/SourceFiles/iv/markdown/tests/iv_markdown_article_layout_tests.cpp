/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_layout_tests.h"

#include "iv/markdown/iv_markdown_article_layout_structure.h"
#include "lang/lang_text_entity.h"

#include <array>

#include "styles/style_iv.h"

namespace Iv::Markdown::Tests {
namespace {

using BlockKind = RichPage::BlockKind;

struct RectField {
	const char *name = nullptr;
	QRect LaidOutBlock::*rect = nullptr;
};

struct NumberField {
	const char *name = nullptr;
	int LaidOutBlock::*number = nullptr;
};

struct KindCase {
	QString fixture;
	RichPage page;
	bool retained = true;
};

struct MaxRightCase {
	QString name;
	RichPage page;
	int maxRight = 0;
};

constexpr auto kRectFields = std::array{
	RectField{ "content", &LaidOutBlock::contentRect },
	RectField{ "text", &LaidOutBlock::textRect },
	RectField{ "header", &LaidOutBlock::headerRect },
	RectField{ "body", &LaidOutBlock::bodyRect },
	RectField{ "icon", &LaidOutBlock::iconRect },
	RectField{ "label", &LaidOutBlock::labelRect },
	RectField{ "subtitle", &LaidOutBlock::subtitleRect },
	RectField{ "action", &LaidOutBlock::actionRect },
	RectField{ "marker", &LaidOutBlock::markerRect },
	RectField{ "collapseControl", &LaidOutBlock::collapseControlRect },
	RectField{ "formula", &LaidOutBlock::formulaRect },
	RectField{ "visibleFormula", &LaidOutBlock::visibleFormulaRect },
	RectField{ "table", &LaidOutBlock::tableRect },
	RectField{ "visibleTable", &LaidOutBlock::visibleTableRect },
	RectField{ "media", &LaidOutBlock::mediaRect },
	RectField{ "visibleMedia", &LaidOutBlock::visibleMediaRect },
	RectField{ "thumbnail", &LaidOutBlock::thumbnailRect },
	RectField{ "scrollViewport", &LaidOutBlock::scrollViewportRect },
	RectField{ "scrollLogical", &LaidOutBlock::scrollLogicalContentRect },
	RectField{ "scrollTrack", &LaidOutBlock::scrollScrollbarTrackRect },
	RectField{ "tableTrack", &LaidOutBlock::tableScrollbarTrackRect },
};

constexpr auto kNumberFields = std::array{
	NumberField{ "textWidth", &LaidOutBlock::textWidth },
	NumberField{ "markerWidth", &LaidOutBlock::markerWidth },
	NumberField{ "labelWidth", &LaidOutBlock::labelWidth },
	NumberField{ "subtitleWidth", &LaidOutBlock::subtitleWidth },
	NumberField{ "actionWidth", &LaidOutBlock::actionWidth },
	NumberField{ "centeredShift", &LaidOutBlock::centeredShift },
	NumberField{ "scrollMax", &LaidOutBlock::horizontalScrollMax },
};

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wswitch"
[[nodiscard]] QString KindName(PreparedBlockKind kind) {
	using Kind = PreparedBlockKind;
	switch (kind) {
	case Kind::Paragraph: return u"Paragraph"_q;
	case Kind::Thinking: return u"Thinking"_q;
	case Kind::Heading: return u"Heading"_q;
	case Kind::CodeBlock: return u"CodeBlock"_q;
	case Kind::Rule: return u"Rule"_q;
	case Kind::ButtonRow: return u"ButtonRow"_q;
	case Kind::List: return u"List"_q;
	case Kind::ListItem: return u"ListItem"_q;
	case Kind::Quote: return u"Quote"_q;
	case Kind::DisplayMath: return u"DisplayMath"_q;
	case Kind::Table: return u"Table"_q;
	case Kind::Details: return u"Details"_q;
	case Kind::Photo: return u"Photo"_q;
	case Kind::Video: return u"Video"_q;
	case Kind::Document: return u"Document"_q;
	case Kind::Map: return u"Map"_q;
	case Kind::Channel: return u"Channel"_q;
	case Kind::GroupedMedia: return u"GroupedMedia"_q;
	case Kind::RelatedArticle: return u"RelatedArticle"_q;
	case Kind::EmbedPost: return u"EmbedPost"_q;
	case Kind::Placeholder: return u"Placeholder"_q;
	}
	return u"Kind%1"_q.arg(int(kind));
}
#pragma clang diagnostic pop

[[nodiscard]] bool ContentRtl(const MarkdownArticleContent &content) {
	return content.richPage && content.richPage->rtl;
}

[[nodiscard]] LayoutContext ArticleContext(
		const MarkdownArticleContent &content,
		int width) {
	return {
		.articleWidth = width,
		.useArticleBands = true,
		.rtl = ContentRtl(content),
	};
}

void AppendRect(QStringList &parts, const char *name, QRect rect) {
	if (rect != QRect()) {
		parts.push_back(QString::fromLatin1(name) + u'=' + RectText(rect));
	}
}

[[nodiscard]] QString BlockLine(
		const LaidOutBlock &block,
		const QString &path) {
	auto parts = QStringList{
		path,
		KindName(block.kind),
		u"outer="_q + RectText(block.outer),
	};
	for (const auto &field : kRectFields) {
		AppendRect(parts, field.name, block.*(field.rect));
	}
	if (!block.markerCenter.isNull()) {
		parts.push_back(u"markerCenter=(%1,%2)"_q
			.arg(block.markerCenter.x())
			.arg(block.markerCenter.y()));
	}
	for (const auto &field : kNumberFields) {
		if (const auto number = block.*(field.number)) {
			parts.push_back(QString::fromLatin1(field.name)
				+ u'='
				+ QString::number(number));
		}
	}
	if (block.overflowed) {
		parts.push_back(u"overflowed"_q);
	}
	if (!block.tableColumnWidths.empty()) {
		parts.push_back(u"columns="_q + NumbersText(block.tableColumnWidths));
	}
	return parts.join(u' ');
}

void AppendBlockLines(
		QStringList &lines,
		const LaidOutBlock &block,
		const QString &path) {
	lines.push_back(BlockLine(block, path));
	for (auto r = 0; r != int(block.tableRows.size()); ++r) {
		const auto &row = block.tableRows[r];
		const auto rowPath = u"%1 row%2"_q.arg(path).arg(r);
		lines.push_back(rowPath + u" outer="_q + RectText(row.outer));
		for (auto c = 0; c != int(row.cells.size()); ++c) {
			const auto &cell = row.cells[c];
			lines.push_back(u"%1 cell%2 outer=%3 text=%4 textWidth=%5"_q
				.arg(rowPath)
				.arg(c)
				.arg(RectText(cell.outer))
				.arg(RectText(cell.textRect))
				.arg(cell.textWidth));
		}
	}
	for (auto i = 0; i != int(block.buttons.size()); ++i) {
		const auto &button = block.buttons[i];
		auto parts = QStringList{
			u"%1 button%2"_q.arg(path).arg(i),
			u"rect="_q + RectText(button.rect),
		};
		AppendRect(parts, "label", button.labelRect);
		AppendRect(parts, "icon", button.iconRect);
		lines.push_back(parts.join(u' '));
	}
	for (auto i = 0; i != int(block.children.size()); ++i) {
		AppendBlockLines(lines, block.children[i], u"%1.%2"_q.arg(path).arg(i));
	}
}

[[nodiscard]] QStringList GeometryLines(const LayoutRun &run) {
	auto result = QStringList();
	for (auto i = 0; i != int(run.blocks.size()); ++i) {
		AppendBlockLines(result, run.blocks[i], QString::number(i));
	}
	const auto &st = st::defaultMarkdown;
	const auto rtl = ContentRtl(run.content);
	result.push_back(u"bottom=%1 maxRight=%2 overflow=%3"_q
		.arg(run.bottom)
		.arg(ArticleContentMaxRight(run.blocks, st, rtl))
		.arg(ArticleHorizontalOverflow(run.blocks)));
	return result;
}

[[nodiscard]] QString PreparedCellLine(
		const PreparedTableCell &cell,
		const QString &path) {
	auto result = u"%1 column=%2 span=%3x%4"_q
		.arg(path)
		.arg(cell.column)
		.arg(cell.colspan)
		.arg(cell.rowspan);
	if (cell.header) {
		result += u" header"_q;
	}
	using Vertical = PreparedTableCellVerticalAlignment;
	if (cell.verticalAlignment == Vertical::Middle) {
		result += u" middle"_q;
	} else if (cell.verticalAlignment == Vertical::Bottom) {
		result += u" bottom"_q;
	}
	return result;
}

[[nodiscard]] QString PreparedLine(
		const PreparedBlock &block,
		const QString &path) {
	const auto table = (block.kind == PreparedBlockKind::Table);
	const auto flags = std::array{
		std::pair{ IsAnchorOnlyBlock(block), "anchor" },
		std::pair{ block.footer, "footer" },
		std::pair{ block.listKind == ListKind::Ordered, "ordered" },
		std::pair{ block.taskState == TaskState::Checked, "checked" },
		std::pair{ block.taskState == TaskState::Unchecked, "unchecked" },
		std::pair{ block.quoteAuthor, "author" },
		std::pair{ block.pullquote, "pullquote" },
		std::pair{ block.detailsOpen, "open" },
		std::pair{ block.collapsed, "collapsed" },
		std::pair{ table && block.tableBordered, "bordered" },
		std::pair{ table && block.tableCompact, "compact" },
	};
	auto parts = QStringList{ path, KindName(block.kind) };
	for (const auto &[set, name] : flags) {
		if (set) {
			parts.push_back(QString::fromLatin1(name));
		}
	}
	if (block.formulaIndex >= 0) {
		parts.push_back(u"formula=%1"_q.arg(block.formulaIndex));
	}
	if (block.tableColumnCount > 0) {
		parts.push_back(u"columns=%1"_q.arg(block.tableColumnCount));
	}
	parts.push_back(u"children=%1"_q.arg(block.children.size()));
	return parts.join(u' ');
}

void AppendPreparedLines(
		QStringList &lines,
		const PreparedBlock &block,
		const QString &path) {
	lines.push_back(PreparedLine(block, path));
	for (auto r = 0; r != int(block.tableRows.size()); ++r) {
		const auto &row = block.tableRows[r];
		const auto rowPath = u"%1 row%2"_q.arg(path).arg(r);
		lines.push_back(rowPath
			+ (row.header ? u" header"_q : QString())
			+ u" cells=%1"_q.arg(row.cells.size()));
		for (auto c = 0; c != int(row.cells.size()); ++c) {
			lines.push_back(PreparedCellLine(
				row.cells[c],
				u"%1 cell%2"_q.arg(rowPath).arg(c)));
		}
	}
	for (auto i = 0; i != int(block.children.size()); ++i) {
		AppendPreparedLines(
			lines,
			block.children[i],
			u"%1.%2"_q.arg(path).arg(i));
	}
}

bool ExpectLines(
		Expect &expect,
		const QString &what,
		const QStringList &lines,
		const QStringList &expected) {
	auto result = expect.number(
		what + u" lines"_q,
		int(lines.size()),
		int(expected.size()));
	const auto count = int(std::min(lines.size(), expected.size()));
	for (auto i = 0; i != count; ++i) {
		if (lines[i] != expected[i]) {
			expect.text(what + u" line %1"_q.arg(i), lines[i], expected[i]);
			result = false;
		}
	}
	return result;
}

void ExpectRecount(
		Expect &expect,
		const QString &what,
		LayoutRun &run,
		int width,
		int bottom,
		const QStringList &expected) {
	const auto accepted = LayoutRetained(run, width);
	expect.flag(what + u" accepted"_q, accepted, true);
	if (accepted && expect.number(what + u" bottom"_q, run.bottom, bottom)) {
		ExpectGeometry(expect, what, run, expected);
	}
}

[[nodiscard]] bool HasKind(
		const std::vector<PreparedBlock> &blocks,
		PreparedBlockKind kind) {
	for (const auto &block : blocks) {
		if (block.kind == kind || HasKind(block.children, kind)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool HasKind(
		const std::vector<LaidOutBlock> &blocks,
		PreparedBlockKind kind) {
	for (const auto &block : blocks) {
		if (block.kind == kind || HasKind(block.children, kind)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] RichPage::Block HeadingBlock(const QString &text) {
	return {
		.kind = BlockKind::Heading,
		.text = MakeText(text),
		.headingLevel = 2,
	};
}

[[nodiscard]] RichPage::Block ParagraphBlock(const QString &text) {
	return MakeBlock(BlockKind::Paragraph, text);
}

[[nodiscard]] RichPage::Block ListBlock(
		std::vector<RichPage::ListItem> items,
		RichPage::ListKind kind = RichPage::ListKind::Bullet) {
	return {
		.kind = BlockKind::List,
		.listKind = kind,
		.listItems = std::move(items),
	};
}

[[nodiscard]] RichPage::ListItem TaskItem(
		const QString &text,
		RichPage::TaskState state) {
	return {
		.taskState = state,
		.text = MakeText(text),
	};
}

[[nodiscard]] RichPage::Block NestedListBlock() {
	return ListBlock({
		MakeItem(u"One"_q),
		MakeItem(u"Two"_q, {
			ListBlock(
				{ MakeItem(u"Inner one"_q), MakeItem(u"Inner two"_q) },
				RichPage::ListKind::Ordered),
		}),
	});
}

[[nodiscard]] RichPage::Block CaptionedQuote(
		const QString &text,
		const QString &caption,
		bool pullquote) {
	return {
		.kind = BlockKind::Quote,
		.text = MakeText(text),
		.caption = MakeText(caption),
		.pullquote = pullquote,
	};
}

[[nodiscard]] RichPage::Block NestedQuoteBlock() {
	return MakeBlock(BlockKind::Quote, QString(), {
		ParagraphBlock(u"Outer"_q),
		MakeBlock(BlockKind::Quote, QString(), {
			ParagraphBlock(u"Inner"_q),
		}),
	});
}

[[nodiscard]] RichPage::Block DetailsBlock(
		const QString &summary,
		bool open,
		const QString &body) {
	return {
		.kind = BlockKind::Details,
		.text = MakeText(summary),
		.open = open,
		.blocks = { ParagraphBlock(body) },
	};
}

[[nodiscard]] RichPage::Block EmbedPostBlock() {
	return {
		.kind = BlockKind::EmbedPost,
		.author = u"Author"_q,
		.blocks = { ParagraphBlock(u"Post body"_q) },
	};
}

[[nodiscard]] RichPage::Block EmbedBlock() {
	return {
		.kind = BlockKind::Embed,
		.caption = MakeText(u"Embed caption"_q),
		.url = u"https://example.com/embed"_q,
	};
}

[[nodiscard]] RichPage::Block RelatedBlock(const QString &title) {
	return {
		.kind = BlockKind::RelatedArticles,
		.text = MakeText(title),
		.relatedArticles = {
			{
				.url = u"https://example.com/first"_q,
				.title = u"First"_q,
				.description = u"About the first"_q,
			},
			{
				.url = u"https://example.com/second"_q,
				.title = u"Second"_q,
			},
		},
	};
}

[[nodiscard]] RichPage::Block ButtonsBlock() {
	auto result = RichPage::Block{ .kind = BlockKind::ButtonRow };
	for (const auto &label : { u"Yes"_q, u"No"_q, u"Maybe"_q }) {
		result.buttons.push_back({ .text = MakeText(label) });
	}
	return result;
}

[[nodiscard]] RichPage::GroupedMediaItem GroupedPhoto(uint64 photoId) {
	return {
		.kind = BlockKind::Photo,
		.photoId = photoId,
		.width = 10,
		.height = 10,
	};
}

[[nodiscard]] std::vector<RichPage::Block> MediaBlocks() {
	return {
		{
			.kind = BlockKind::Photo,
			.caption = MakeText(u"Photo caption"_q),
			.width = 100,
			.height = 50,
			.photoId = 1,
		},
		{
			.kind = BlockKind::Video,
			.width = 160,
			.height = 90,
			.documentId = 2,
		},
		{ .kind = BlockKind::Audio, .documentId = 3 },
		{
			.kind = BlockKind::Map,
			.zoom = 15,
			.latitude = 10.,
			.longitude = 20.,
			.accessHash = 1,
		},
		{
			.kind = BlockKind::Channel,
			.channelTitle = u"Channel"_q,
			.channelId = 5,
		},
		{
			.kind = BlockKind::GroupedMedia,
			.mediaItems = { GroupedPhoto(7), GroupedPhoto(8) },
		},
	};
}

[[nodiscard]] RichPage ContainersPage(bool rtl) {
	return MakePage({
		NestedListBlock(),
		ListBlock({
			TaskItem(u"Checked task"_q, RichPage::TaskState::Checked),
			TaskItem(u"Open task"_q, RichPage::TaskState::Unchecked),
		}),
		CaptionedQuote(u"Quoted text"_q, u"Author"_q, false),
		NestedQuoteBlock(),
		MakeBlock(BlockKind::Quote, QString(), {
			ListBlock({ MakeItem(u"In quote"_q) }),
		}),
		CaptionedQuote(u"Pulled"_q, u"Cite"_q, true),
		DetailsBlock(u"Summary"_q, true, u"Inside"_q),
		DetailsBlock(u"Closed"_q, false, u"Hidden"_q),
		EmbedPostBlock(),
	}, rtl);
}

[[nodiscard]] RichPage CardsPage() {
	return MakePage({
		EmbedBlock(),
		RelatedBlock(u"Related"_q),
		ButtonsBlock(),
	});
}

[[nodiscard]] RichPage MathPage() {
	return MakePage({
		{ .kind = BlockKind::Math, .formula = u"x^2+y^2=z^2"_q },
		{ .kind = BlockKind::Math, .formula = u"\\frac{"_q },
	});
}

[[nodiscard]] KindCase MediaKindCase(int index) {
	return {
		.fixture = u"MediaPage block %1"_q.arg(index),
		.page = MakePage({ MediaBlocks()[index] }),
		.retained = false,
	};
}

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wswitch"
[[nodiscard]] std::optional<KindCase> KindFixture(PreparedBlockKind kind) {
	using Kind = PreparedBlockKind;
	switch (kind) {
	case Kind::Paragraph:
	case Kind::Thinking:
	case Kind::Heading:
	case Kind::CodeBlock:
	case Kind::Rule:
		return KindCase{ u"FlowPage"_q, FlowPage() };
	case Kind::List:
	case Kind::ListItem:
	case Kind::Quote:
	case Kind::Details:
	case Kind::EmbedPost:
		return KindCase{ u"ContainersPage"_q, ContainersPage(false) };
	case Kind::ButtonRow:
	case Kind::RelatedArticle:
	case Kind::Placeholder:
		return KindCase{ u"CardsPage"_q, CardsPage() };
	case Kind::DisplayMath:
		return KindCase{ u"MathPage"_q, MathPage() };
	case Kind::Table:
		return KindCase{ u"SpansTablePage"_q, SpansTablePage(true, false) };
	case Kind::Photo: return MediaKindCase(0);
	case Kind::Video: return MediaKindCase(1);
	case Kind::Document: return MediaKindCase(2);
	case Kind::Map: return MediaKindCase(3);
	case Kind::Channel: return MediaKindCase(4);
	case Kind::GroupedMedia: return MediaKindCase(5);
	}
	return std::nullopt;
}
#pragma clang diagnostic pop

[[nodiscard]] QString ParagraphGeometry() {
	const auto expected = QStringList{
		u"0 Paragraph outer=(18,0,564,24) content=(18,0,564,24)"
			u" text=(18,0,564,24) textWidth=564"_q,
		u"bottom=24 maxRight=96 overflow=0"_q,
	};
	auto expect = Expect();
	auto run = LayoutFresh(
		expect,
		u"paragraph"_q,
		MakePage({ ParagraphBlock(u"Hi"_q) }),
		600);
	if (!run) {
		return expect.result();
	}
	ExpectGeometry(expect, u"fresh 600"_q, *run, expected);
	const auto retained = LayoutRetained(*run, 600);
	expect.flag(u"retained 600 accepted"_q, retained, true);
	if (retained) {
		ExpectGeometry(expect, u"retained 600"_q, *run, expected);
	}
	return expect.result();
}

[[nodiscard]] QString FlowWide() {
	const auto expected = QStringList{
		u"0 Heading outer=(18,0,564,36) content=(18,0,564,36)"
			u" text=(18,0,564,36) textWidth=564"_q,
		u"1 Paragraph outer=(18,48,564,24) content=(18,48,564,24)"
			u" text=(18,48,564,24) textWidth=564"_q,
		u"2 Paragraph outer=(18,72,564,0) content=(18,72,564,0)"
			u" text=(18,72,564,0) textWidth=564"_q,
		u"3 Paragraph outer=(18,84,564,24) content=(18,84,564,24)"
			u" text=(18,84,564,24) textWidth=564"_q,
		u"4 Paragraph outer=(18,120,564,18) content=(18,120,564,18)"
			u" text=(18,120,564,18) textWidth=564"_q,
		u"5 Thinking outer=(18,150,564,24) content=(18,150,564,24)"
			u" text=(18,150,564,24) textWidth=564"_q,
		u"6 CodeBlock outer=(18,186,564,76) content=(28,212,550,44)"
			u" text=(28,212,550,44) header=(18,186,564,20)"
			u" body=(18,206,564,56) textWidth=550"_q,
		u"7 Rule outer=(0,274,600,1) text=(0,274,600,1)"_q,
		u"8 Paragraph outer=(18,287,564,24) content=(18,287,564,24)"
			u" text=(18,287,564,24) textWidth=564"_q,
		u"bottom=311 maxRight=451 overflow=0"_q,
	};
	auto expect = Expect();
	ExpectFreshAndRetained(expect, u"flow"_q, FlowPage(), 600, expected);
	return expect.result();
}

[[nodiscard]] QString FlowNarrow() {
	const auto expected = QStringList{
		u"0 Heading outer=(18,0,164,72) content=(18,0,164,72)"
			u" text=(18,0,164,72) textWidth=164"_q,
		u"1 Paragraph outer=(18,84,164,96) content=(18,84,164,96)"
			u" text=(18,84,164,96) textWidth=164"_q,
		u"2 Paragraph outer=(18,180,164,0) content=(18,180,164,0)"
			u" text=(18,180,164,0) textWidth=164"_q,
		u"3 Paragraph outer=(18,192,164,24) content=(18,192,164,24)"
			u" text=(18,192,164,24) textWidth=164"_q,
		u"4 Paragraph outer=(18,228,164,18) content=(18,228,164,18)"
			u" text=(18,228,164,18) textWidth=164"_q,
		u"5 Thinking outer=(18,258,164,24) content=(18,258,164,24)"
			u" text=(18,258,164,24) textWidth=164"_q,
		u"6 CodeBlock outer=(18,294,164,76) content=(28,320,150,44)"
			u" text=(28,320,150,44) header=(18,294,164,20)"
			u" body=(18,314,164,56) textWidth=150"_q,
		u"7 Rule outer=(0,382,200,1) text=(0,382,200,1)"_q,
		u"8 Paragraph outer=(18,395,164,24) content=(18,395,164,24)"
			u" text=(18,395,164,24) textWidth=164"_q,
		u"bottom=419 maxRight=164 overflow=0"_q,
	};
	auto expect = Expect();
	ExpectFreshAndRetained(expect, u"flow"_q, FlowPage(), 200, expected);
	return expect.result();
}

[[nodiscard]] QString FlowMinimum() {
	const auto expected = QStringList{
		u"0 Heading outer=(18,0,1,88) content=(18,0,1,72) text=(18,0,144,72)"
			u" scrollViewport=(18,0,1,72) scrollLogical=(18,0,144,72)"
			u" scrollTrack=(18,76,1,12) textWidth=144 scrollMax=143"
			u" overflowed"_q,
		u"1 Paragraph outer=(18,100,1,160) content=(18,100,1,144)"
			u" text=(18,100,96,144) scrollViewport=(18,100,1,144)"
			u" scrollLogical=(18,100,96,144) scrollTrack=(18,248,1,12)"
			u" textWidth=96 scrollMax=95 overflowed"_q,
		u"2 Paragraph outer=(18,260,1,0) content=(18,260,1,0)"
			u" text=(18,260,1,0) textWidth=1"_q,
		u"3 Paragraph outer=(18,272,1,64) content=(18,272,1,48)"
			u" text=(18,272,96,48) scrollViewport=(18,272,1,48)"
			u" scrollLogical=(18,272,96,48) scrollTrack=(18,324,1,12)"
			u" textWidth=96 scrollMax=95 overflowed"_q,
		u"4 Paragraph outer=(18,348,1,34) content=(18,348,1,18)"
			u" text=(18,348,72,18) scrollViewport=(18,348,1,18)"
			u" scrollLogical=(18,348,72,18) scrollTrack=(18,370,1,12)"
			u" textWidth=72 scrollMax=59 overflowed"_q,
		u"5 Thinking outer=(18,394,1,40) content=(18,394,1,24)"
			u" text=(18,394,96,24) scrollViewport=(18,394,1,24)"
			u" scrollLogical=(18,394,96,24) scrollTrack=(18,422,1,12)"
			u" textWidth=96 scrollMax=89 overflowed"_q,
		u"6 CodeBlock outer=(18,446,15,92) content=(28,472,1,44)"
			u" text=(28,472,89,44) header=(18,446,15,20) body=(18,466,15,72)"
			u" scrollViewport=(28,472,1,44) scrollLogical=(28,472,89,44)"
			u" scrollTrack=(28,520,1,12) textWidth=89 scrollMax=88"
			u" overflowed"_q,
		u"7 Rule outer=(0,550,1,1) text=(0,550,1,1)"_q,
		u"8 Paragraph outer=(18,563,1,40) content=(18,563,1,24)"
			u" text=(18,563,96,24) scrollViewport=(18,563,1,24)"
			u" scrollLogical=(18,563,96,24) scrollTrack=(18,591,1,12)"
			u" textWidth=96 scrollMax=24 overflowed"_q,
		u"bottom=603 maxRight=15 overflow=143"_q,
	};
	auto expect = Expect();
	ExpectFreshAndRetained(expect, u"flow"_q, FlowPage(), 1, expected);
	return expect.result();
}

[[nodiscard]] QStringList ContainersTopology() {
	return {
		u"0 List children=2"_q,
		u"0.0 ListItem children=1"_q,
		u"0.0.0 Paragraph children=0"_q,
		u"0.1 ListItem children=2"_q,
		u"0.1.0 Paragraph children=0"_q,
		u"0.1.1 List ordered children=2"_q,
		u"0.1.1.0 ListItem ordered children=1"_q,
		u"0.1.1.0.0 Paragraph children=0"_q,
		u"0.1.1.1 ListItem ordered children=1"_q,
		u"0.1.1.1.0 Paragraph children=0"_q,
		u"1 List children=2"_q,
		u"1.0 ListItem checked children=1"_q,
		u"1.0.0 Paragraph children=0"_q,
		u"1.1 ListItem unchecked children=1"_q,
		u"1.1.0 Paragraph children=0"_q,
		u"2 Quote children=2"_q,
		u"2.0 Paragraph children=0"_q,
		u"2.1 Paragraph author children=0"_q,
		u"3 Quote children=2"_q,
		u"3.0 Paragraph children=0"_q,
		u"3.1 Quote children=1"_q,
		u"3.1.0 Paragraph children=0"_q,
		u"4 Quote children=1"_q,
		u"4.0 List children=1"_q,
		u"4.0.0 ListItem children=1"_q,
		u"4.0.0.0 Paragraph children=0"_q,
		u"5 Quote pullquote children=2"_q,
		u"5.0 Paragraph pullquote children=0"_q,
		u"5.1 Paragraph author pullquote children=0"_q,
		u"6 Details open children=1"_q,
		u"6.0 Paragraph children=0"_q,
		u"7 Details collapsed children=1"_q,
		u"7.0 Paragraph children=0"_q,
		u"8 EmbedPost children=1"_q,
		u"8.0 Paragraph children=0"_q,
	};
}

void ExpectContainers(
		Expect &expect,
		bool rtl,
		int width,
		const QStringList &expected) {
	const auto page = ContainersPage(rtl);
	const auto what = rtl ? u"containers rtl"_q : u"containers"_q;
	if (ExpectTopology(expect, what, page, ContainersTopology())) {
		ExpectFreshAndRetained(expect, what, page, width, expected);
	}
}

[[nodiscard]] QStringList Quotes600() {
	return {
		u"2 Quote outer=(18,216,564,72) content=(28,222,534,60)"
			u" textWidth=534"_q,
		u"2.0 Paragraph outer=(28,222,534,24) content=(28,222,534,24)"
			u" text=(28,222,534,24) textWidth=534"_q,
		u"2.1 Paragraph outer=(28,258,534,24) content=(28,258,534,24)"
			u" text=(28,258,534,24) textWidth=534"_q,
		u"3 Quote outer=(18,300,564,84) content=(28,306,534,72)"
			u" textWidth=534"_q,
		u"3.0 Paragraph outer=(28,306,534,24) content=(28,306,534,24)"
			u" text=(28,306,534,24) textWidth=534"_q,
		u"3.1 Quote outer=(28,342,534,36) content=(38,348,504,24)"
			u" textWidth=504"_q,
		u"3.1.0 Paragraph outer=(38,348,504,24) content=(38,348,504,24)"
			u" text=(38,348,504,24) textWidth=504"_q,
		u"4 Quote outer=(18,396,564,36) content=(28,402,534,24)"
			u" textWidth=534"_q,
		u"4.0 List outer=(28,402,534,24) content=(28,402,534,24)"_q,
	};
}

[[nodiscard]] QString ContainersWide() {
	const auto lists = QStringList{
		u"0 List outer=(18,0,564,132) content=(18,0,564,132)"_q,
		u"0.0 ListItem outer=(18,0,564,24) content=(48,0,534,24)"
			u" markerCenter=(34,12) markerWidth=22"_q,
		u"0.0.0 Paragraph outer=(48,0,534,24) content=(48,0,534,24)"
			u" text=(48,0,534,24) textWidth=534"_q,
		u"0.1 ListItem outer=(18,36,564,96) content=(48,36,534,96)"
			u" markerCenter=(34,48) markerWidth=22"_q,
		u"0.1.0 Paragraph outer=(48,36,534,24) content=(48,36,534,24)"
			u" text=(48,36,534,24) textWidth=534"_q,
		u"0.1.1 List outer=(48,72,534,60) content=(48,72,534,60)"_q,
		u"0.1.1.0 ListItem outer=(48,72,534,24) content=(78,72,504,24)"
			u" marker=(59,72,11,24) markerWidth=22"_q,
		u"0.1.1.0.0 Paragraph outer=(78,72,504,24) content=(78,72,504,24)"
			u" text=(78,72,504,24) textWidth=504"_q,
		u"0.1.1.1 ListItem outer=(48,108,534,24) content=(78,108,504,24)"
			u" marker=(59,108,11,24) markerWidth=22"_q,
		u"0.1.1.1.0 Paragraph outer=(78,108,504,24) content=(78,108,504,24)"
			u" text=(78,108,504,24) textWidth=504"_q,
		u"1 List outer=(18,144,564,60) content=(18,144,564,60)"_q,
		u"1.0 ListItem outer=(18,144,564,24) content=(48,144,534,24)"
			u" marker=(18,145,22,22) markerWidth=22"_q,
		u"1.0.0 Paragraph outer=(48,144,534,24) content=(48,144,534,24)"
			u" text=(48,144,534,24) textWidth=534"_q,
		u"1.1 ListItem outer=(18,180,564,24) content=(48,180,534,24)"
			u" marker=(18,181,22,22) markerWidth=22"_q,
		u"1.1.0 Paragraph outer=(48,180,534,24) content=(48,180,534,24)"
			u" text=(48,180,534,24) textWidth=534"_q,
	};
	const auto rest = QStringList{
		u"4.0.0 ListItem outer=(28,402,534,24) content=(58,402,504,24)"
			u" markerCenter=(44,414) markerWidth=22"_q,
		u"4.0.0.0 Paragraph outer=(58,402,504,24) content=(58,402,504,24)"
			u" text=(58,402,504,24) textWidth=504"_q,
		u"5 Quote outer=(262,444,76,96) content=(282,462,36,60)"
			u" textWidth=420 centeredShift=244"_q,
		u"5.0 Paragraph outer=(282,462,36,24) content=(282,462,36,24)"
			u" text=(282,462,36,24) textWidth=36"_q,
		u"5.1 Paragraph outer=(282,498,36,24) content=(282,498,36,24)"
			u" text=(282,498,36,24) textWidth=36"_q,
		u"6 Details outer=(0,552,600,90) content=(18,606,564,24)"
			u" text=(44,564,538,18) header=(0,552,600,42)"
			u" body=(0,594,600,48) icon=(18,564,18,18) textWidth=538"_q,
		u"6.0 Paragraph outer=(18,606,564,24) content=(18,606,564,24)"
			u" text=(18,606,564,24) textWidth=564"_q,
		u"7 Details outer=(0,642,600,42) text=(44,654,538,18)"
			u" header=(0,642,600,42) icon=(18,654,18,18) textWidth=538"_q,
		u"8 EmbedPost outer=(18,696,564,58) content=(41,696,533,58)"
			u" header=(41,696,533,24) body=(41,730,533,24)"
			u" label=(41,696,533,24) media=(18,696,564,58) labelWidth=533"_q,
		u"8.0 Paragraph outer=(41,730,533,24) content=(41,730,533,24)"
			u" text=(41,730,533,24) textWidth=533"_q,
		u"bottom=754 maxRight=600 overflow=0"_q,
	};
	auto expect = Expect();
	ExpectContainers(expect, false, 600, lists + Quotes600() + rest);
	return expect.result();
}

[[nodiscard]] QString ContainersNarrow() {
	const auto expected = QStringList{
		u"0 List outer=(18,0,164,132) content=(18,0,164,132)"_q,
		u"0.0 ListItem outer=(18,0,164,24) content=(48,0,134,24)"
			u" markerCenter=(34,12) markerWidth=22"_q,
		u"0.0.0 Paragraph outer=(48,0,134,24) content=(48,0,134,24)"
			u" text=(48,0,134,24) textWidth=134"_q,
		u"0.1 ListItem outer=(18,36,164,96) content=(48,36,134,96)"
			u" markerCenter=(34,48) markerWidth=22"_q,
		u"0.1.0 Paragraph outer=(48,36,134,24) content=(48,36,134,24)"
			u" text=(48,36,134,24) textWidth=134"_q,
		u"0.1.1 List outer=(48,72,134,60) content=(48,72,134,60)"_q,
		u"0.1.1.0 ListItem outer=(48,72,134,24) content=(78,72,104,24)"
			u" marker=(59,72,11,24) markerWidth=22"_q,
		u"0.1.1.0.0 Paragraph outer=(78,72,104,24) content=(78,72,104,24)"
			u" text=(78,72,104,24) textWidth=104"_q,
		u"0.1.1.1 ListItem outer=(48,108,134,24) content=(78,108,104,24)"
			u" marker=(59,108,11,24) markerWidth=22"_q,
		u"0.1.1.1.0 Paragraph outer=(78,108,104,24) content=(78,108,104,24)"
			u" text=(78,108,104,24) textWidth=104"_q,
		u"1 List outer=(18,144,164,60) content=(18,144,164,60)"_q,
		u"1.0 ListItem outer=(18,144,164,24) content=(48,144,134,24)"
			u" marker=(18,145,22,22) markerWidth=22"_q,
		u"1.0.0 Paragraph outer=(48,144,134,24) content=(48,144,134,24)"
			u" text=(48,144,134,24) textWidth=134"_q,
		u"1.1 ListItem outer=(18,180,164,24) content=(48,180,134,24)"
			u" marker=(18,181,22,22) markerWidth=22"_q,
		u"1.1.0 Paragraph outer=(48,180,134,24) content=(48,180,134,24)"
			u" text=(48,180,134,24) textWidth=134"_q,
		u"2 Quote outer=(18,216,164,72) content=(28,222,134,60)"
			u" textWidth=134"_q,
		u"2.0 Paragraph outer=(28,222,134,24) content=(28,222,134,24)"
			u" text=(28,222,134,24) textWidth=134"_q,
		u"2.1 Paragraph outer=(28,258,134,24) content=(28,258,134,24)"
			u" text=(28,258,134,24) textWidth=134"_q,
		u"3 Quote outer=(18,300,164,84) content=(28,306,134,72)"
			u" textWidth=134"_q,
		u"3.0 Paragraph outer=(28,306,134,24) content=(28,306,134,24)"
			u" text=(28,306,134,24) textWidth=134"_q,
		u"3.1 Quote outer=(28,342,134,36) content=(38,348,104,24)"
			u" textWidth=104"_q,
		u"3.1.0 Paragraph outer=(38,348,104,24) content=(38,348,104,24)"
			u" text=(38,348,104,24) textWidth=104"_q,
		u"4 Quote outer=(18,396,164,36) content=(28,402,134,24)"
			u" textWidth=134"_q,
		u"4.0 List outer=(28,402,134,24) content=(28,402,134,24)"_q,
		u"4.0.0 ListItem outer=(28,402,134,24) content=(58,402,104,24)"
			u" markerCenter=(44,414) markerWidth=22"_q,
		u"4.0.0.0 Paragraph outer=(58,402,104,24) content=(58,402,104,24)"
			u" text=(58,402,104,24) textWidth=104"_q,
		u"5 Quote outer=(62,444,76,96) content=(82,462,36,60) textWidth=124"
			u" centeredShift=44"_q,
		u"5.0 Paragraph outer=(82,462,36,24) content=(82,462,36,24)"
			u" text=(82,462,36,24) textWidth=36"_q,
		u"5.1 Paragraph outer=(82,498,36,24) content=(82,498,36,24)"
			u" text=(82,498,36,24) textWidth=36"_q,
		u"6 Details outer=(0,552,200,90) content=(18,606,164,24)"
			u" text=(44,564,138,18) header=(0,552,200,42)"
			u" body=(0,594,200,48) icon=(18,564,18,18) textWidth=138"_q,
		u"6.0 Paragraph outer=(18,606,164,24) content=(18,606,164,24)"
			u" text=(18,606,164,24) textWidth=164"_q,
		u"7 Details outer=(0,642,200,42) text=(44,654,138,18)"
			u" header=(0,642,200,42) icon=(18,654,18,18) textWidth=138"_q,
		u"8 EmbedPost outer=(18,696,164,58) content=(41,696,133,58)"
			u" header=(41,696,133,24) body=(41,730,133,24)"
			u" label=(41,696,133,24) media=(18,696,164,58) labelWidth=133"_q,
		u"8.0 Paragraph outer=(41,730,133,24) content=(41,730,133,24)"
			u" text=(41,730,133,24) textWidth=133"_q,
		u"bottom=754 maxRight=200 overflow=0"_q,
	};
	auto expect = Expect();
	ExpectContainers(expect, false, 200, expected);
	return expect.result();
}

[[nodiscard]] QString ContainersRtl() {
	const auto lists = QStringList{
		u"0 List outer=(18,0,564,132) content=(18,0,564,132)"_q,
		u"0.0 ListItem outer=(18,0,564,24) content=(18,0,534,24)"
			u" markerCenter=(566,12) markerWidth=22"_q,
		u"0.0.0 Paragraph outer=(18,0,534,24) content=(18,0,534,24)"
			u" text=(18,0,534,24) textWidth=534"_q,
		u"0.1 ListItem outer=(18,36,564,96) content=(18,36,534,96)"
			u" markerCenter=(566,48) markerWidth=22"_q,
		u"0.1.0 Paragraph outer=(18,36,534,24) content=(18,36,534,24)"
			u" text=(18,36,534,24) textWidth=534"_q,
		u"0.1.1 List outer=(18,72,534,60) content=(18,72,534,60)"_q,
		u"0.1.1.0 ListItem outer=(18,72,534,24) content=(18,72,504,24)"
			u" marker=(530,72,11,24) markerWidth=22"_q,
		u"0.1.1.0.0 Paragraph outer=(18,72,504,24) content=(18,72,504,24)"
			u" text=(18,72,504,24) textWidth=504"_q,
		u"0.1.1.1 ListItem outer=(18,108,534,24) content=(18,108,504,24)"
			u" marker=(530,108,11,24) markerWidth=22"_q,
		u"0.1.1.1.0 Paragraph outer=(18,108,504,24) content=(18,108,504,24)"
			u" text=(18,108,504,24) textWidth=504"_q,
		u"1 List outer=(18,144,564,60) content=(18,144,564,60)"_q,
		u"1.0 ListItem outer=(18,144,564,24) content=(18,144,534,24)"
			u" marker=(560,145,22,22) markerWidth=22"_q,
		u"1.0.0 Paragraph outer=(18,144,534,24) content=(18,144,534,24)"
			u" text=(18,144,534,24) textWidth=534"_q,
		u"1.1 ListItem outer=(18,180,564,24) content=(18,180,534,24)"
			u" marker=(560,181,22,22) markerWidth=22"_q,
		u"1.1.0 Paragraph outer=(18,180,534,24) content=(18,180,534,24)"
			u" text=(18,180,534,24) textWidth=534"_q,
	};
	const auto rest = QStringList{
		u"4.0.0 ListItem outer=(28,402,534,24) content=(28,402,504,24)"
			u" markerCenter=(546,414) markerWidth=22"_q,
		u"4.0.0.0 Paragraph outer=(28,402,504,24) content=(28,402,504,24)"
			u" text=(28,402,504,24) textWidth=504"_q,
		u"5 Quote outer=(262,444,76,96) content=(282,462,36,60)"
			u" textWidth=420 centeredShift=244"_q,
		u"5.0 Paragraph outer=(282,462,36,24) content=(282,462,36,24)"
			u" text=(282,462,36,24) textWidth=36"_q,
		u"5.1 Paragraph outer=(282,498,36,24) content=(282,498,36,24)"
			u" text=(282,498,36,24) textWidth=36"_q,
		u"6 Details outer=(0,552,600,90) content=(18,606,564,24)"
			u" text=(18,564,538,18) header=(0,552,600,42)"
			u" body=(0,594,600,48) icon=(564,564,18,18) textWidth=538"_q,
		u"6.0 Paragraph outer=(18,606,564,24) content=(18,606,564,24)"
			u" text=(18,606,564,24) textWidth=564"_q,
		u"7 Details outer=(0,642,600,42) text=(18,654,538,18)"
			u" header=(0,642,600,42) icon=(564,654,18,18) textWidth=538"_q,
		u"8 EmbedPost outer=(18,696,564,58) content=(41,696,533,58)"
			u" header=(41,696,533,24) body=(41,730,533,24)"
			u" label=(41,696,533,24) media=(18,696,564,58) labelWidth=533"_q,
		u"8.0 Paragraph outer=(41,730,533,24) content=(41,730,533,24)"
			u" text=(41,730,533,24) textWidth=533"_q,
		u"bottom=754 maxRight=600 overflow=0"_q,
	};
	auto expect = Expect();
	ExpectContainers(expect, true, 600, lists + Quotes600() + rest);
	return expect.result();
}

[[nodiscard]] QString CardsWide() {
	const auto expected = QStringList{
		u"0 Placeholder outer=(18,0,564,110) content=(18,0,564,110)"
			u" text=(18,86,564,24) label=(34,24,532,24) media=(18,0,564,72)"
			u" visibleMedia=(18,0,564,72) textWidth=564 labelWidth=532"_q,
		u"1 Heading outer=(0,126,600,43) content=(0,126,600,43)"
			u" text=(18,134,564,27) header=(0,126,600,43) textWidth=564"_q,
		u"2 RelatedArticle outer=(0,169,600,66) content=(0,169,600,66)"
			u" label=(18,177,564,22) subtitle=(18,205,564,21)"
			u" media=(0,169,600,66) visibleMedia=(0,169,600,66)"
			u" labelWidth=564 subtitleWidth=564"_q,
		u"3 RelatedArticle outer=(0,235,600,39) content=(0,235,600,39)"
			u" label=(18,243,564,22) media=(0,235,600,39)"
			u" visibleMedia=(0,235,600,39) labelWidth=564"_q,
		u"4 ButtonRow outer=(18,286,564,26) content=(18,286,564,26)"_q,
		u"4 button0 rect=(18,286,184,26) label=(99,290,21,18)"_q,
		u"4 button1 rect=(208,286,184,26) label=(290,290,19,18)"_q,
		u"4 button2 rect=(398,286,184,26) label=(468,290,43,18)"_q,
		u"bottom=312 maxRight=600 overflow=0"_q,
	};
	auto expect = Expect();
	ExpectFreshAndRetained(expect, u"cards"_q, CardsPage(), 600, expected);
	return expect.result();
}

[[nodiscard]] QString CardsNarrow() {
	const auto expected = QStringList{
		u"0 Placeholder outer=(18,0,164,110) content=(18,0,164,110)"
			u" text=(18,86,164,24) label=(34,24,132,24) media=(18,0,164,72)"
			u" visibleMedia=(18,0,164,72) textWidth=164 labelWidth=132"_q,
		u"1 Heading outer=(0,126,200,43) content=(0,126,200,43)"
			u" text=(18,134,164,27) header=(0,126,200,43) textWidth=164"_q,
		u"2 RelatedArticle outer=(0,169,200,66) content=(0,169,200,66)"
			u" label=(18,177,164,22) subtitle=(18,205,164,21)"
			u" media=(0,169,200,66) visibleMedia=(0,169,200,66)"
			u" labelWidth=164 subtitleWidth=164"_q,
		u"3 RelatedArticle outer=(0,235,200,39) content=(0,235,200,39)"
			u" label=(18,243,164,22) media=(0,235,200,39)"
			u" visibleMedia=(0,235,200,39) labelWidth=164"_q,
		u"4 ButtonRow outer=(18,286,164,26) content=(18,286,164,26)"_q,
		u"4 button0 rect=(18,286,50,26) label=(32,290,21,18)"_q,
		u"4 button1 rect=(74,286,51,26) label=(90,290,19,18)"_q,
		u"4 button2 rect=(131,286,51,26) label=(135,290,43,18)"_q,
		u"bottom=312 maxRight=200 overflow=0"_q,
	};
	auto expect = Expect();
	ExpectFreshAndRetained(expect, u"cards"_q, CardsPage(), 200, expected);
	return expect.result();
}

void ExpectMediaFresh(
		Expect &expect,
		int width,
		const QStringList &expected) {
	const auto what = u"media fresh %1"_q.arg(width);
	const auto run = LayoutFresh(expect, what, MakePage(MediaBlocks()), width);
	if (!run) {
		return;
	}
	ExpectGeometry(expect, what, *run, expected);
	for (auto i = 0; i != int(run->blocks.size()); ++i) {
		expect.flag(
			what + u" block %1 media host"_q.arg(i),
			run->blocks[i].mediaBlock != nullptr,
			false);
	}
}

[[nodiscard]] QString MediaWithoutHost() {
	const auto wide = QStringList{
		u"0 Photo outer=(0,0,600,36) content=(200,0,200,36)"
			u" text=(18,12,564,24) media=(200,0,200,0)"
			u" visibleMedia=(200,0,200,0) textWidth=564"_q,
		u"1 Video outer=(0,48,600,0) content=(140,48,320,0)"
			u" media=(140,48,320,0) visibleMedia=(140,48,320,0)"_q,
		u"2 Document outer=(0,60,600,0) content=(0,60,600,0)"
			u" media=(0,60,600,0) visibleMedia=(0,60,600,0)"_q,
		u"3 Map outer=(0,72,600,0) content=(0,72,600,0) media=(0,72,600,0)"
			u" visibleMedia=(0,72,600,0)"_q,
		u"4 Channel outer=(0,80,600,0) content=(0,80,600,0)"
			u" media=(0,80,600,0) visibleMedia=(0,80,600,0)"_q,
		u"5 GroupedMedia outer=(0,92,600,0) content=(0,92,600,0)"
			u" media=(0,92,600,0) visibleMedia=(0,92,600,0)"_q,
		u"bottom=92 maxRight=600 overflow=0"_q,
	};
	const auto narrow = QStringList{
		u"0 Photo outer=(0,0,200,36) content=(0,0,200,36)"
			u" text=(18,12,164,24) media=(0,0,200,0)"
			u" visibleMedia=(0,0,200,0) textWidth=164"_q,
		u"1 Video outer=(0,48,200,0) content=(0,48,200,0) media=(0,48,200,0)"
			u" visibleMedia=(0,48,200,0)"_q,
		u"2 Document outer=(0,60,200,0) content=(0,60,200,0)"
			u" media=(0,60,200,0) visibleMedia=(0,60,200,0)"_q,
		u"3 Map outer=(0,72,200,0) content=(0,72,200,0) media=(0,72,200,0)"
			u" visibleMedia=(0,72,200,0)"_q,
		u"4 Channel outer=(0,80,200,0) content=(0,80,200,0)"
			u" media=(0,80,200,0) visibleMedia=(0,80,200,0)"_q,
		u"5 GroupedMedia outer=(0,92,200,0) content=(0,92,200,0)"
			u" media=(0,92,200,0) visibleMedia=(0,92,200,0)"_q,
		u"bottom=92 maxRight=200 overflow=0"_q,
	};
	const auto kinds = std::array{
		u"Photo"_q,
		u"Video"_q,
		u"Document"_q,
		u"Map"_q,
		u"Channel"_q,
		u"GroupedMedia"_q,
	};
	auto expect = Expect();
	ExpectMediaFresh(expect, 600, wide);
	ExpectMediaFresh(expect, 200, narrow);
	const auto blocks = MediaBlocks();
	if (!expect.number(u"media blocks"_q, int(blocks.size()), 6)) {
		return expect.result();
	}
	for (auto i = 0; i != int(blocks.size()); ++i) {
		const auto what = u"media %1"_q.arg(kinds[i]);
		auto run = LayoutFresh(expect, what, MakePage({ blocks[i] }), 600);
		const auto count = run ? int(run->blocks.size()) : 0;
		if (!expect.number(what + u" blocks"_q, count, 1)) {
			continue;
		}
		expect.text(what + u" kind"_q, KindName(run->blocks[0].kind), kinds[i]);
		expect.flag(what + u" recount 200"_q, LayoutRetained(*run, 200), false);
	}
	return expect.result();
}

[[nodiscard]] QString DisplayMath() {
	const auto wide = QStringList{
		u"0 DisplayMath outer=(18,0,564,46) content=(28,12,544,22)"
			u" formula=(247,12,105,22) visibleFormula=(247,12,105,22)"_q,
		u"1 DisplayMath outer=(18,58,564,70) content=(28,70,544,46)"
			u" text=(252,82,96,22) formula=(242,70,116,46)"
			u" visibleFormula=(242,70,116,46) textWidth=96"_q,
		u"bottom=128 maxRight=564 overflow=0"_q,
	};
	const auto narrow = QStringList{
		u"0 DisplayMath outer=(18,0,164,46) content=(28,12,144,22)"
			u" formula=(47,12,105,22) visibleFormula=(47,12,105,22)"_q,
		u"1 DisplayMath outer=(18,58,164,70) content=(28,70,144,46)"
			u" text=(52,82,96,22) formula=(42,70,116,46)"
			u" visibleFormula=(42,70,116,46) textWidth=96"_q,
		u"bottom=128 maxRight=164 overflow=0"_q,
	};
	auto expect = Expect();
	const auto page = MathPage();
	const auto content = PrepareArticle(expect, u"math"_q, page);
	if (!content
		|| !expect.number(
			u"formula slots"_q,
			int(content->formulas.size()),
			2)) {
		return expect.result();
	}
	const auto &valid = content->formulas[0].measured;
	expect.flag(u"valid measured"_q, valid.success, true);
	expect.number(u"valid width"_q, valid.logicalSize.width(), 105);
	expect.number(u"valid height"_q, valid.logicalSize.height(), 22);
	const auto &invalid = content->formulas[1].measured;
	expect.flag(u"invalid measured"_q, invalid.success, false);
	ExpectFreshAndRetained(expect, u"math"_q, page, 600, wide);
	ExpectFreshAndRetained(expect, u"math"_q, page, 200, narrow);
	return expect.result();
}

[[nodiscard]] QString MaxRight() {
	const auto media = MediaBlocks();
	const auto list = ListBlock({ MakeItem(u"One"_q) });
	const auto cases = std::vector<MaxRightCase>{
		{ u"paragraph"_q, MakePage({ ParagraphBlock(u"Hi"_q) }), 96 },
		{ u"heading"_q, MakePage({ HeadingBlock(u"Title"_q) }), 144 },
		{
			u"footer"_q,
			MakePage({ MakeBlock(BlockKind::Footer, u"Note"_q) }),
			72,
		},
		{
			u"code"_q,
			MakePage({ MakeBlock(BlockKind::Code, u"x = 1"_q) }),
			102,
		},
		{ u"divider"_q, MakePage({ MakeBlock(BlockKind::Divider) }), 0 },
		{ u"buttons"_q, MakePage({ ButtonsBlock() }), 564 },
		{ u"placeholder"_q, MakePage({ EmbedBlock() }), 564 },
		{ u"related"_q, MakePage({ RelatedBlock(QString()) }), 600 },
		{ u"embed post"_q, MakePage({ EmbedPostBlock() }), 564 },
		{
			u"pullquote"_q,
			MakePage({ CaptionedQuote(u"Pulled"_q, u"Cite"_q, true) }),
			76,
		},
		{ u"nested quote"_q, MakePage({ NestedQuoteBlock() }), 156 },
		{ u"list ltr"_q, MakePage({ list }), 126 },
		{ u"list rtl"_q, MakePage({ list }, true), 564 },
		{ u"photo"_q, MakePage({ media[0] }), 600 },
	};
	auto expect = Expect();
	for (const auto &entry : cases) {
		const auto run = LayoutFresh(expect, entry.name, entry.page, 600);
		if (!run) {
			continue;
		}
		const auto maxRight = ArticleContentMaxRight(
			run->blocks,
			st::defaultMarkdown,
			ContentRtl(run->content));
		expect.number(entry.name + u" max right"_q, maxRight, entry.maxRight);
	}
	return expect.result();
}

void ExpectKindCase(
		Expect &expect,
		PreparedBlockKind kind,
		const KindCase &fixture) {
	const auto what = KindName(kind) + u" via "_q + fixture.fixture;
	auto run = LayoutFresh(expect, what, fixture.page, 600);
	const auto reference = LayoutFresh(expect, what, fixture.page, 300);
	if (!run || !reference) {
		return;
	}
	expect.flag(
		what + u" prepared"_q,
		HasKind(run->content.blocks.blocks, kind),
		true);
	expect.flag(what + u" laid out"_q, HasKind(run->blocks, kind), true);
	const auto accepted = LayoutRetained(*run, 300);
	expect.flag(what + u" recount 300"_q, accepted, fixture.retained);
	if (accepted) {
		ExpectSameGeometry(expect, what + u" recount 300"_q, *run, *reference);
	}
}

[[nodiscard]] QString KindInventory() {
	static_assert(int(PreparedBlockKind::Placeholder) == 20);
	auto expect = Expect();
	auto count = 0;
	while (const auto fixture = KindFixture(PreparedBlockKind(count))) {
		ExpectKindCase(expect, PreparedBlockKind(count), *fixture);
		++count;
	}
	expect.number(u"kinds"_q, count, 21);
	return expect.result();
}

} // namespace

RichPage::RichText MakeText(const QString &text) {
	return { .text = tr::marked(text) };
}

RichPage::Block MakeBlock(
		RichPage::BlockKind kind,
		const QString &text,
		std::vector<RichPage::Block> blocks) {
	return {
		.kind = kind,
		.text = MakeText(text),
		.blocks = std::move(blocks),
	};
}

RichPage::ListItem MakeItem(
		const QString &text,
		std::vector<RichPage::Block> blocks) {
	return {
		.text = MakeText(text),
		.blocks = std::move(blocks),
	};
}

RichPage::TableCell MakeCell(const QString &text, int colspan, int rowspan) {
	return {
		.text = MakeText(text),
		.colspan = colspan,
		.rowspan = rowspan,
	};
}

RichPage MakePage(std::vector<RichPage::Block> blocks, bool rtl) {
	return {
		.rtl = rtl,
		.blocks = std::move(blocks),
	};
}

RichPage FlowPage() {
	return MakePage({
		HeadingBlock(u"Article layout"_q),
		ParagraphBlock(u"Fresh and retained layout must agree"
			u" on every rectangle of this paragraph."_q),
		{ .kind = BlockKind::Anchor, .anchorId = u"middle"_q },
		ParagraphBlock(u"After the anchor"_q),
		MakeBlock(BlockKind::Footer, u"Footer note"_q),
		MakeBlock(BlockKind::Thinking, u"Thinking aloud"_q),
		MakeBlock(BlockKind::Code, u"first line\n\tsecond line"_q),
		MakeBlock(BlockKind::Divider),
		ParagraphBlock(u"Last"_q),
	});
}

std::optional<MarkdownArticleContent> PrepareArticle(
		Expect &expect,
		const QString &what,
		const RichPage &page) {
	auto result = TryPrepareNativeInstantView({
		.richPage = std::make_shared<const RichPage>(page),
	});
	if (!result.supported()) {
		expect.text(
			what + u" prepare"_q,
			(result.failed() ? u"Failure "_q : u"Unsupported "_q)
				+ result.debugReason,
			u"Supported"_q);
		return std::nullopt;
	}
	return std::move(result.content);
}

std::optional<LayoutRun> LayoutFresh(
		Expect &expect,
		const QString &what,
		const RichPage &page,
		int width) {
	auto content = PrepareArticle(expect, what, page);
	if (!content) {
		return std::nullopt;
	}
	auto result = LayoutRun{
		.content = std::move(*content),
		.width = width,
	};
	auto rendered = std::vector<RenderedFormula>(
		result.content.formulas.size());
	result.bottom = LayoutBlocks(
		result.content.blocks.blocks,
		&result.content.formulas,
		&rendered,
		nullptr,
		nullptr,
		result.content.mediaRuntime,
		&result.blocks,
		st::defaultMarkdown,
		0,
		0,
		width,
		ArticleContext(result.content, width));
	return result;
}

bool LayoutRetained(LayoutRun &run, int width) {
	const auto bottom = RecountLaidOutBlocks(
		run.content.blocks.blocks,
		run.content.formulas,
		&run.blocks,
		st::defaultMarkdown,
		0,
		0,
		width,
		ArticleContext(run.content, width));
	if (!bottom) {
		return false;
	}
	run.width = width;
	run.bottom = *bottom;
	return true;
}

bool LayoutRetained(
		LayoutRun &run,
		MarkdownArticleContent content,
		int width) {
	run.content = std::move(content);
	return LayoutRetained(run, width);
}

bool ExpectTopology(
		Expect &expect,
		const QString &what,
		const RichPage &page,
		const QStringList &expected) {
	const auto content = PrepareArticle(expect, what, page);
	if (!content) {
		return false;
	}
	auto lines = QStringList();
	const auto &blocks = content->blocks.blocks;
	for (auto i = 0; i != int(blocks.size()); ++i) {
		AppendPreparedLines(lines, blocks[i], QString::number(i));
	}
	return ExpectLines(expect, what + u" prepared"_q, lines, expected);
}

void ExpectGeometry(
		Expect &expect,
		const QString &what,
		const LayoutRun &run,
		const QStringList &expected) {
	ExpectLines(expect, what, GeometryLines(run), expected);
}

void ExpectSameGeometry(
		Expect &expect,
		const QString &what,
		const LayoutRun &run,
		const LayoutRun &reference) {
	if (expect.number(what + u" bottom"_q, run.bottom, reference.bottom)) {
		ExpectGeometry(expect, what, run, GeometryLines(reference));
	}
}

void ExpectFreshAndRetained(
		Expect &expect,
		const QString &what,
		const RichPage &page,
		int width,
		const QStringList &expected) {
	const auto alternate = (width < 600) ? 600 : 200;
	auto fresh = LayoutFresh(expect, what, page, width);
	auto other = LayoutFresh(expect, what, page, alternate);
	if (!fresh || !other) {
		return;
	}
	const auto prefix = what + u' ';
	const auto bottom = fresh->bottom;
	ExpectGeometry(expect, prefix + u"fresh %1"_q.arg(width), *fresh, expected);
	ExpectRecount(
		expect,
		prefix + u"recount %1->%1"_q.arg(width),
		*fresh,
		width,
		bottom,
		expected);
	ExpectRecount(
		expect,
		prefix + u"recount %1->%2"_q.arg(alternate).arg(width),
		*other,
		width,
		bottom,
		expected);
}

std::vector<Check> LayoutChecks() {
	return {
		{ u"layout.paragraph"_q, ParagraphGeometry },
		{ u"layout.flow-wide"_q, FlowWide },
		{ u"layout.flow-narrow"_q, FlowNarrow },
		{ u"layout.flow-minimum"_q, FlowMinimum },
		{ u"layout.containers-wide"_q, ContainersWide },
		{ u"layout.containers-narrow"_q, ContainersNarrow },
		{ u"layout.containers-rtl"_q, ContainersRtl },
		{ u"layout.cards-wide"_q, CardsWide },
		{ u"layout.cards-narrow"_q, CardsNarrow },
		{ u"layout.media-without-host"_q, MediaWithoutHost },
		{ u"layout.display-math"_q, DisplayMath },
		{ u"layout.max-right"_q, MaxRight },
		{ u"layout.kind-inventory"_q, KindInventory },
	};
}

} // namespace Iv::Markdown::Tests
