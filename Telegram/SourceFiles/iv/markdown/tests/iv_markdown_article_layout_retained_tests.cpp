/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_layout_tests.h"

#include "iv/markdown/iv_markdown_article_layout_structure.h"
#include "lang/lang_text_entity.h"

namespace Iv::Markdown::Tests {
namespace {

using BlockKind = RichPage::BlockKind;
using Probe = QString(*)(const MarkdownArticleContent &content);

constexpr auto kWordJoiner = QChar(0x2060);

struct CapStep {
	int width = 0;
	bool accepted = false;
	int cap = 0;
	QStringList expected;
};

struct DeclineCase {
	QString name;
	RichPage before;
	RichPage after;
	QStringList topology;
	QStringList changedTopology;
	Probe probe = nullptr;
	QString probeBefore;
	QString probeAfter;
	QStringList fallback;
};

struct AcceptedCase {
	QString name;
	RichPage before;
	RichPage after;
	int width = 0;
	QStringList expected;
	int cap = 0;
};

[[nodiscard]] RichPage::Block ParagraphBlock(const QString &text) {
	return MakeBlock(BlockKind::Paragraph, text);
}

[[nodiscard]] RichPage InlineButtonPage() {
	auto text = MakeText(u"Tap "_q + kWordJoiner + u" now"_q);
	text.text.entities.push_back(EntityInText(
		EntityType::CustomEmoji,
		4,
		1,
		SerializeInlineTextObjectEntity({
			.kind = InlineTextObjectKind::Button,
			.data = InlineTextObjectButtonData{
				.label = tr::marked(u"Press"_q),
			},
		})));
	return MakePage({
		{ .kind = BlockKind::Paragraph, .text = std::move(text) },
	});
}

[[nodiscard]] QStringList InlineGeometry1000() {
	return {
		u"0 Paragraph outer=(18,0,964,24) content=(18,0,964,24)"
			u" text=(18,0,964,24) textWidth=964"_q,
		u"bottom=24 maxRight=97 overflow=0"_q,
	};
}

[[nodiscard]] QStringList ClosedDetails300() {
	return {
		u"0 Details outer=(0,0,300,42) text=(44,12,238,18)"
			u" header=(0,0,300,42) icon=(18,12,18,18) textWidth=238"_q,
		u"bottom=42 maxRight=300 overflow=0"_q,
	};
}

[[nodiscard]] RichPage::Block TableBlock(
		std::vector<RichPage::TableRow> rows,
		const QString &caption = QString()) {
	return {
		.kind = BlockKind::Table,
		.text = MakeText(caption),
		.bordered = true,
		.tableRows = std::move(rows),
	};
}

[[nodiscard]] RichPage::TableRow TwoCellRow(
		const QString &first,
		const QString &second,
		int colspan = 1) {
	return { .cells = { MakeCell(first), MakeCell(second, colspan) } };
}

[[nodiscard]] RichPage SpanTablePage(int colspan) {
	return MakePage({
		TableBlock({
			{
				.cells = {
					MakeCell(u"A"_q),
					MakeCell(u"B"_q),
					MakeCell(u"C"_q),
				},
			},
			TwoCellRow(u"D"_q, u"E"_q, colspan),
		}),
	});
}

[[nodiscard]] RichPage ListPage(
		RichPage::ListKind kind,
		std::vector<RichPage::Block> children) {
	return MakePage({
		{
			.kind = BlockKind::List,
			.listKind = kind,
			.listItems = { MakeItem(u"One"_q, std::move(children)) },
		},
	});
}

[[nodiscard]] RichPage DetailsPage(bool open) {
	return MakePage({
		{
			.kind = BlockKind::Details,
			.text = MakeText(u"Summary"_q),
			.open = open,
			.blocks = { ParagraphBlock(u"Inside"_q) },
		},
	});
}

[[nodiscard]] RichPage FormulaPage(const QString &formula) {
	return MakePage({ { .kind = BlockKind::Math, .formula = formula } });
}

[[nodiscard]] RichPage EmbedPostPage(const QString &author) {
	return MakePage({
		{
			.kind = BlockKind::EmbedPost,
			.author = author,
			.blocks = { ParagraphBlock(u"Post body"_q) },
		},
	});
}

[[nodiscard]] RichPage RelatedPage(const QString &description) {
	return MakePage({
		{
			.kind = BlockKind::RelatedArticles,
			.relatedArticles = {
				{
					.url = u"https://example.com/first"_q,
					.title = u"First"_q,
					.description = description,
				},
			},
		},
	});
}

[[nodiscard]] QStringList TwoCellTopology() {
	return {
		u"0 Table bordered columns=2 children=0"_q,
		u"0 row0 cells=2"_q,
		u"0 row0 cell0 column=0 span=1x1"_q,
		u"0 row0 cell1 column=1 span=1x1"_q,
	};
}

[[nodiscard]] QStringList SpanTopology(int colspan) {
	return {
		u"0 Table bordered columns=3 children=0"_q,
		u"0 row0 cells=3"_q,
		u"0 row0 cell0 column=0 span=1x1"_q,
		u"0 row0 cell1 column=1 span=1x1"_q,
		u"0 row0 cell2 column=2 span=1x1"_q,
		u"0 row1 cells=2"_q,
		u"0 row1 cell0 column=0 span=1x1"_q,
		u"0 row1 cell1 column=1 span=%1x1"_q.arg(colspan),
	};
}

[[nodiscard]] QStringList ListTopology(bool ordered) {
	const auto flag = ordered ? u" ordered"_q : QString();
	return {
		u"0 List"_q + flag + u" children=1"_q,
		u"0.0 ListItem"_q + flag + u" children=1"_q,
		u"0.0.0 Paragraph children=0"_q,
	};
}

[[nodiscard]] QStringList DetailsTopology(bool open) {
	return {
		open
			? u"0 Details open children=1"_q
			: u"0 Details collapsed children=1"_q,
		u"0.0 Paragraph children=0"_q,
	};
}

[[nodiscard]] const PreparedBlock *LastBlock(
		const MarkdownArticleContent &content) {
	const auto &blocks = content.blocks.blocks;
	return blocks.empty() ? nullptr : &blocks.back();
}

[[nodiscard]] QString LastText(const MarkdownArticleContent &content) {
	const auto block = LastBlock(content);
	return block ? block->text.text : u"<none>"_q;
}

[[nodiscard]] QString LastAuthor(const MarkdownArticleContent &content) {
	const auto block = LastBlock(content);
	return block ? block->embedPost.author : u"<none>"_q;
}

[[nodiscard]] QString LastDescription(
		const MarkdownArticleContent &content) {
	const auto block = LastBlock(content);
	return block ? block->relatedArticle.description : u"<none>"_q;
}

[[nodiscard]] QString FormulaState(const MarkdownArticleContent &content) {
	if (content.formulas.size() != 1) {
		return u"slots=%1"_q.arg(content.formulas.size());
	}
	return content.formulas.front().measured.success
		? u"measured"_q
		: u"failed"_q;
}

void ExpectInlineCap(
		Expect &expect,
		const QString &what,
		const LayoutRun &run,
		int cap) {
	if (!expect.number(what + u" blocks"_q, int(run.blocks.size()), 1)) {
		return;
	}
	const auto &block = run.blocks.front();
	expect.flag(
		what + u" carries inline button"_q,
		block.carriesInlineButton,
		true);
	expect.number(what + u" cap"_q, block.inlineButtonWidthCap, cap);
}

void ExpectCapStep(Expect &expect, const RichPage &page, const CapStep &step) {
	const auto what = u"inline 600->%1"_q.arg(step.width);
	auto run = LayoutFresh(expect, what, page, 600);
	if (!run) {
		return;
	}
	const auto accepted = LayoutRetained(*run, step.width);
	expect.flag(what + u" accepted"_q, accepted, step.accepted);
	if (accepted != step.accepted) {
		return;
	}
	const auto reference = LayoutFresh(expect, what, page, step.width);
	if (!reference) {
		return;
	}
	if (accepted) {
		ExpectSameGeometry(expect, what + u" fresh"_q, *run, *reference);
	}
	const auto &result = accepted ? *run : *reference;
	const auto label = accepted ? what : (what + u" fallback"_q);
	ExpectGeometry(expect, label, result, step.expected);
	ExpectInlineCap(expect, label, result, step.cap);
}

void ExpectRepeated(Expect &expect, const QString &name, const RichPage &page) {
	auto run = LayoutFresh(expect, name, page, 600);
	if (!run) {
		return;
	}
	for (const auto width : { 200, 600, 600, 1, 600 }) {
		const auto what = u"%1 %2->%3"_q.arg(name).arg(run->width).arg(width);
		const auto accepted = LayoutRetained(*run, width);
		expect.flag(what + u" accepted"_q, accepted, true);
		if (!accepted) {
			return;
		}
		const auto reference = LayoutFresh(expect, what, page, width);
		if (reference) {
			ExpectSameGeometry(expect, what, *run, *reference);
		}
	}
}

void ExpectDecline(Expect &expect, const DeclineCase &entry) {
	const auto &name = entry.name;
	const auto before = name + u" before"_q;
	const auto after = name + u" after"_q;
	const auto &changed = entry.changedTopology.isEmpty()
		? entry.topology
		: entry.changedTopology;
	if (!ExpectTopology(expect, before, entry.before, entry.topology)
		|| !ExpectTopology(expect, after, entry.after, changed)) {
		return;
	}
	auto run = LayoutFresh(expect, before, entry.before, 600);
	auto content = PrepareArticle(expect, after, entry.after);
	if (!run || !content) {
		return;
	}
	if (entry.probe) {
		expect.text(
			before + u" difference"_q,
			entry.probe(run->content),
			entry.probeBefore);
		expect.text(
			after + u" difference"_q,
			entry.probe(*content),
			entry.probeAfter);
	}
	expect.flag(
		name + u" recount 300"_q,
		LayoutRetained(*run, std::move(*content), 300),
		false);
	const auto what = name + u" fallback 300"_q;
	const auto fallback = LayoutFresh(expect, what, entry.after, 300);
	if (fallback) {
		ExpectGeometry(expect, what, *fallback, entry.fallback);
	}
}

void ExpectAccepted(Expect &expect, const AcceptedCase &entry) {
	auto run = LayoutFresh(expect, entry.name, entry.before, 600);
	auto content = PrepareArticle(expect, entry.name, entry.after);
	if (!run || !content) {
		return;
	}
	const auto what = u"%1 recount %2"_q.arg(entry.name).arg(entry.width);
	const auto accepted = LayoutRetained(
		*run,
		std::move(*content),
		entry.width);
	expect.flag(what + u" accepted"_q, accepted, true);
	if (!accepted) {
		return;
	}
	ExpectGeometry(expect, what, *run, entry.expected);
	if (entry.cap) {
		ExpectInlineCap(expect, what, *run, entry.cap);
	}
	const auto reference = LayoutFresh(expect, what, entry.after, entry.width);
	if (reference) {
		ExpectSameGeometry(expect, what + u" fresh"_q, *run, *reference);
	}
}

[[nodiscard]] std::vector<DeclineCase> FlowDeclines() {
	const auto two = MakePage({
		ParagraphBlock(u"First"_q),
		ParagraphBlock(u"Second"_q),
	});
	const auto twoTopology = QStringList{
		u"0 Paragraph children=0"_q,
		u"1 Paragraph children=0"_q,
	};
	return {
		{
			.name = u"added block"_q,
			.before = two,
			.after = MakePage({
				ParagraphBlock(u"First"_q),
				ParagraphBlock(u"Second"_q),
				ParagraphBlock(u"Third"_q),
			}),
			.topology = twoTopology,
			.changedTopology = twoTopology + QStringList{
				u"2 Paragraph children=0"_q,
			},
			.fallback = {
				u"0 Paragraph outer=(18,0,264,24) content=(18,0,264,24)"
					u" text=(18,0,264,24) textWidth=264"_q,
				u"1 Paragraph outer=(18,36,264,24) content=(18,36,264,24)"
					u" text=(18,36,264,24) textWidth=264"_q,
				u"2 Paragraph outer=(18,72,264,24) content=(18,72,264,24)"
					u" text=(18,72,264,24) textWidth=264"_q,
				u"bottom=96 maxRight=96 overflow=0"_q,
			},
		},
		{
			.name = u"paragraph to heading"_q,
			.before = two,
			.after = MakePage({
				ParagraphBlock(u"First"_q),
				{
					.kind = BlockKind::Heading,
					.text = MakeText(u"Second"_q),
					.headingLevel = 2,
				},
			}),
			.topology = twoTopology,
			.changedTopology = {
				u"0 Paragraph children=0"_q,
				u"1 Heading children=0"_q,
			},
			.fallback = {
				u"0 Paragraph outer=(18,0,264,24) content=(18,0,264,24)"
					u" text=(18,0,264,24) textWidth=264"_q,
				u"1 Heading outer=(18,40,264,36) content=(18,40,264,36)"
					u" text=(18,40,264,36) textWidth=264"_q,
				u"bottom=76 maxRight=144 overflow=0"_q,
			},
		},
		{
			.name = u"empty paragraph gains text"_q,
			.before = MakePage({
				ParagraphBlock(u"Intro"_q),
				ParagraphBlock(QString()),
			}),
			.after = MakePage({
				ParagraphBlock(u"Intro"_q),
				ParagraphBlock(u"Gained"_q),
			}),
			.topology = twoTopology,
			.probe = LastText,
			.probeAfter = u"Gained"_q,
			.fallback = {
				u"0 Paragraph outer=(18,0,264,24) content=(18,0,264,24)"
					u" text=(18,0,264,24) textWidth=264"_q,
				u"1 Paragraph outer=(18,36,264,24) content=(18,36,264,24)"
					u" text=(18,36,264,24) textWidth=264"_q,
				u"bottom=60 maxRight=96 overflow=0"_q,
			},
		},
	};
}

[[nodiscard]] std::vector<DeclineCase> TableDeclines() {
	const auto table = MakePage({
		TableBlock({ TwoCellRow(u"A"_q, u"B"_q) }),
	});
	return {
		{
			.name = u"table gains row"_q,
			.before = table,
			.after = MakePage({
				TableBlock({
					TwoCellRow(u"A"_q, u"B"_q),
					TwoCellRow(u"C"_q, u"D"_q),
				}),
			}),
			.topology = TwoCellTopology(),
			.changedTopology = TwoCellTopology() + QStringList{
				u"0 row1 cells=2"_q,
				u"0 row1 cell0 column=0 span=1x1"_q,
				u"0 row1 cell1 column=1 span=1x1"_q,
			},
			.fallback = {
				u"0 Table outer=(18,0,264,91) content=(18,0,264,91)"
					u" table=(18,0,264,91) visibleTable=(18,0,264,91)"
					u" columns=[130,131]"_q,
				u"0 row0 outer=(19,1,262,44)"_q,
				u"0 row0 cell0 outer=(19,1,130,44) text=(27,11,114,24)"
					u" textWidth=114"_q,
				u"0 row0 cell1 outer=(150,1,131,44) text=(158,11,115,24)"
					u" textWidth=115"_q,
				u"0 row1 outer=(19,46,262,44)"_q,
				u"0 row1 cell0 outer=(19,46,130,44) text=(27,56,114,24)"
					u" textWidth=114"_q,
				u"0 row1 cell1 outer=(150,46,131,44) text=(158,56,115,24)"
					u" textWidth=115"_q,
				u"bottom=91 maxRight=264 overflow=0"_q,
			},
		},
		{
			.name = u"table colspan"_q,
			.before = SpanTablePage(1),
			.after = SpanTablePage(2),
			.topology = SpanTopology(1),
			.changedTopology = SpanTopology(2),
			.fallback = {
				u"0 Table outer=(18,0,264,91) content=(18,0,264,91)"
					u" table=(18,0,264,91) visibleTable=(18,0,264,91)"
					u" columns=[88,86,86]"_q,
				u"0 row0 outer=(19,1,262,44)"_q,
				u"0 row0 cell0 outer=(19,1,88,44) text=(27,11,72,24)"
					u" textWidth=72"_q,
				u"0 row0 cell1 outer=(108,1,86,44) text=(116,11,70,24)"
					u" textWidth=70"_q,
				u"0 row0 cell2 outer=(195,1,86,44) text=(203,11,70,24)"
					u" textWidth=70"_q,
				u"0 row1 outer=(19,46,262,44)"_q,
				u"0 row1 cell0 outer=(19,46,88,44) text=(27,56,72,24)"
					u" textWidth=72"_q,
				u"0 row1 cell1 outer=(108,46,173,44) text=(116,56,157,24)"
					u" textWidth=157"_q,
				u"bottom=91 maxRight=264 overflow=0"_q,
			},
		},
		{
			.name = u"table gains caption"_q,
			.before = table,
			.after = MakePage({
				TableBlock({ TwoCellRow(u"A"_q, u"B"_q) }, u"Caption"_q),
			}),
			.topology = TwoCellTopology(),
			.probe = LastText,
			.probeAfter = u"Caption"_q,
			.fallback = {
				u"0 Table outer=(18,0,264,77) content=(18,0,264,77)"
					u" text=(18,0,264,24) table=(18,31,264,46)"
					u" visibleTable=(18,31,264,46) textWidth=264"
					u" columns=[131,130]"_q,
				u"0 row0 outer=(19,32,262,44)"_q,
				u"0 row0 cell0 outer=(19,32,131,44) text=(27,42,115,24)"
					u" textWidth=115"_q,
				u"0 row0 cell1 outer=(151,32,130,44) text=(159,42,114,24)"
					u" textWidth=114"_q,
				u"bottom=77 maxRight=264 overflow=0"_q,
			},
		},
	};
}

[[nodiscard]] std::vector<DeclineCase> ContainerDeclines() {
	using ListKind = RichPage::ListKind;
	return {
		{
			.name = u"list item gains child"_q,
			.before = ListPage(ListKind::Bullet, {}),
			.after = ListPage(ListKind::Bullet, {
				ParagraphBlock(u"Child"_q),
			}),
			.topology = ListTopology(false),
			.changedTopology = {
				u"0 List children=1"_q,
				u"0.0 ListItem children=2"_q,
				u"0.0.0 Paragraph children=0"_q,
				u"0.0.1 Paragraph children=0"_q,
			},
			.fallback = {
				u"0 List outer=(18,0,264,60) content=(18,0,264,60)"_q,
				u"0.0 ListItem outer=(18,0,264,60) content=(48,0,234,60)"
					u" markerCenter=(34,12) markerWidth=22"_q,
				u"0.0.0 Paragraph outer=(48,0,234,24) content=(48,0,234,24)"
					u" text=(48,0,234,24) textWidth=234"_q,
				u"0.0.1 Paragraph outer=(48,36,234,24) content=(48,36,234,24)"
					u" text=(48,36,234,24) textWidth=234"_q,
				u"bottom=60 maxRight=126 overflow=0"_q,
			},
		},
		{
			.name = u"bullet to ordered"_q,
			.before = ListPage(ListKind::Bullet, {}),
			.after = ListPage(ListKind::Ordered, {}),
			.topology = ListTopology(false),
			.changedTopology = ListTopology(true),
			.fallback = {
				u"0 List outer=(18,0,264,24) content=(18,0,264,24)"_q,
				u"0.0 ListItem outer=(18,0,264,24) content=(48,0,234,24)"
					u" marker=(29,0,11,24) markerWidth=22"_q,
				u"0.0.0 Paragraph outer=(48,0,234,24) content=(48,0,234,24)"
					u" text=(48,0,234,24) textWidth=234"_q,
				u"bottom=24 maxRight=126 overflow=0"_q,
			},
		},
		{
			.name = u"open details closes"_q,
			.before = DetailsPage(true),
			.after = DetailsPage(false),
			.topology = DetailsTopology(true),
			.changedTopology = DetailsTopology(false),
			.fallback = ClosedDetails300(),
		},
		{
			.name = u"closed details opens"_q,
			.before = DetailsPage(false),
			.after = DetailsPage(true),
			.topology = DetailsTopology(false),
			.changedTopology = DetailsTopology(true),
			.fallback = {
				u"0 Details outer=(0,0,300,90) content=(18,54,264,24)"
					u" text=(44,12,238,18) header=(0,0,300,42)"
					u" body=(0,42,300,48) icon=(18,12,18,18) textWidth=238"_q,
				u"0.0 Paragraph outer=(18,54,264,24) content=(18,54,264,24)"
					u" text=(18,54,264,24) textWidth=264"_q,
				u"bottom=90 maxRight=300 overflow=0"_q,
			},
		},
	};
}

[[nodiscard]] std::vector<DeclineCase> LeafDeclines() {
	return {
		{
			.name = u"formula becomes invalid"_q,
			.before = FormulaPage(u"x^2+y^2=z^2"_q),
			.after = FormulaPage(u"\\frac{"_q),
			.topology = { u"0 DisplayMath formula=0 children=0"_q },
			.probe = FormulaState,
			.probeBefore = u"measured"_q,
			.probeAfter = u"failed"_q,
			.fallback = {
				u"0 DisplayMath outer=(18,0,264,70) content=(28,12,244,46)"
					u" text=(102,24,96,22) formula=(92,12,116,46)"
					u" visibleFormula=(92,12,116,46) textWidth=96"_q,
				u"bottom=70 maxRight=264 overflow=0"_q,
			},
		},
		{
			.name = u"embed post gains author"_q,
			.before = EmbedPostPage(QString()),
			.after = EmbedPostPage(u"Author"_q),
			.topology = {
				u"0 EmbedPost children=1"_q,
				u"0.0 Paragraph children=0"_q,
			},
			.probe = LastAuthor,
			.probeAfter = u"Author"_q,
			.fallback = {
				u"0 EmbedPost outer=(18,0,264,58) content=(41,0,233,58)"
					u" header=(41,0,233,24) body=(41,34,233,24)"
					u" label=(41,0,233,24) media=(18,0,264,58)"
					u" labelWidth=233"_q,
				u"0.0 Paragraph outer=(41,34,233,24) content=(41,34,233,24)"
					u" text=(41,34,233,24) textWidth=233"_q,
				u"bottom=58 maxRight=264 overflow=0"_q,
			},
		},
		{
			.name = u"related gains description"_q,
			.before = RelatedPage(QString()),
			.after = RelatedPage(u"About the first"_q),
			.topology = { u"0 RelatedArticle children=0"_q },
			.probe = LastDescription,
			.probeAfter = u"About the first"_q,
			.fallback = {
				u"0 RelatedArticle outer=(0,0,300,66) content=(0,0,300,66)"
					u" label=(18,8,264,22) subtitle=(18,36,264,21)"
					u" media=(0,0,300,66) visibleMedia=(0,0,300,66)"
					u" labelWidth=264 subtitleWidth=264"_q,
				u"bottom=66 maxRight=300 overflow=0"_q,
			},
		},
	};
}

[[nodiscard]] QString RetainedRepeated() {
	auto expect = Expect();
	ExpectRepeated(expect, u"flow"_q, FlowPage());
	ExpectRepeated(expect, u"spans table"_q, SpansTablePage(true, false));
	return expect.result();
}

[[nodiscard]] QString RetainedInlineButtonCap() {
	const auto page = InlineButtonPage();
	const auto steps = std::array{
		CapStep{
			.width = 444,
			.accepted = true,
			.cap = 408,
			.expected = {
				u"0 Paragraph outer=(18,0,408,24) content=(18,0,408,24)"
					u" text=(18,0,408,24) textWidth=408"_q,
				u"bottom=24 maxRight=97 overflow=0"_q,
			},
		},
		CapStep{
			.width = 443,
			.accepted = false,
			.cap = 407,
			.expected = {
				u"0 Paragraph outer=(18,0,407,24) content=(18,0,407,24)"
					u" text=(18,0,407,24) textWidth=407"_q,
				u"bottom=24 maxRight=97 overflow=0"_q,
			},
		},
		CapStep{
			.width = 300,
			.accepted = false,
			.cap = 264,
			.expected = {
				u"0 Paragraph outer=(18,0,264,24) content=(18,0,264,24)"
					u" text=(18,0,264,24) textWidth=264"_q,
				u"bottom=24 maxRight=97 overflow=0"_q,
			},
		},
		CapStep{
			.width = 1000,
			.accepted = true,
			.cap = 408,
			.expected = InlineGeometry1000(),
		},
	};
	auto expect = Expect();
	const auto what = u"inline fresh 600"_q;
	const auto fresh = LayoutFresh(expect, what, page, 600);
	if (!fresh) {
		return expect.result();
	}
	ExpectGeometry(expect, what, *fresh, {
		u"0 Paragraph outer=(18,0,564,24) content=(18,0,564,24)"
			u" text=(18,0,564,24) textWidth=564"_q,
		u"bottom=24 maxRight=97 overflow=0"_q,
	});
	ExpectInlineCap(expect, what, *fresh, 408);
	for (const auto &step : steps) {
		ExpectCapStep(expect, page, step);
	}
	return expect.result();
}

[[nodiscard]] QString RetainedDeclines() {
	auto expect = Expect();
	const auto groups = std::array{
		FlowDeclines,
		TableDeclines,
		ContainerDeclines,
		LeafDeclines,
	};
	for (const auto group : groups) {
		for (const auto &entry : group()) {
			ExpectDecline(expect, entry);
		}
	}
	return expect.result();
}

[[nodiscard]] QString RetainedAccepted() {
	const auto closed = DetailsPage(false);
	const auto button = InlineButtonPage();
	const auto cases = std::vector<AcceptedCase>{
		{
			.name = u"closed details"_q,
			.before = closed,
			.after = closed,
			.width = 300,
			.expected = ClosedDetails300(),
		},
		{
			.name = u"inline cap"_q,
			.before = button,
			.after = button,
			.width = 1000,
			.expected = InlineGeometry1000(),
			.cap = 408,
		},
		{
			.name = u"compact table"_q,
			.before = SpansTablePage(false, false),
			.after = SpansTablePage(false, true),
			.width = 600,
			.expected = BorderlessCompactGeometry(),
		},
	};
	auto expect = Expect();
	for (const auto &entry : cases) {
		ExpectAccepted(expect, entry);
	}
	return expect.result();
}

} // namespace

std::vector<Check> RetainedLayoutChecks() {
	return {
		{ u"layout.retained-repeated"_q, RetainedRepeated },
		{ u"layout.retained-inline-button-cap"_q, RetainedInlineButtonCap },
		{ u"layout.retained-declines"_q, RetainedDeclines },
		{ u"layout.retained-accepted"_q, RetainedAccepted },
	};
}

} // namespace Iv::Markdown::Tests
