/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_tests.h"

#include "iv/markdown/iv_markdown_article_selection.h"
#include "lang/lang_text_entity.h"

#include <algorithm>
#include <array>

namespace Iv::Markdown::Tests {
namespace {

struct ExpectedBlock {
	RichPage::BlockKind kind = RichPage::BlockKind::Paragraph;
	QString text;
	EntitiesInText entities;
	std::vector<QString> items;
	QString nested;
};

using SearchMatches = std::vector<MarkdownArticleSearchMatch>;

[[nodiscard]] RichPage::Block PageParagraph(TextWithEntities text) {
	auto result = RichPage::Block();
	result.kind = RichPage::BlockKind::Paragraph;
	result.text.text = std::move(text);
	return result;
}

[[nodiscard]] RichPage::Block HelloParagraph() {
	return PageParagraph({
		.text = u"Hello world"_q,
		.entities = {
			EntityInText(EntityType::Bold, 0, 5),
			EntityInText(EntityType::Italic, 6, 5),
		},
	});
}

[[nodiscard]] RichPage RichCopyPage() {
	auto result = RichPage();
	result.blocks.push_back(HelloParagraph());
	auto list = RichPage::Block();
	list.kind = RichPage::BlockKind::List;
	for (const auto &item : { u"One"_q, u"Two"_q, u"Three"_q }) {
		list.listItems.push_back({ .text = { .text = tr::marked(item) } });
	}
	result.blocks.push_back(std::move(list));
	auto quote = RichPage::Block();
	quote.kind = RichPage::BlockKind::Quote;
	quote.blocks.push_back(PageParagraph(tr::marked(u"Quoted"_q)));
	result.blocks.push_back(std::move(quote));
	result.blocks.push_back(PageParagraph(tr::marked(u"Tail"_q)));
	return result;
}

void SetTextBlock(
		LaidOutBlock &block,
		const QString &text,
		const PreparedEditLeafSource &source) {
	SetLeaf(block.leaf, text);
	block.editLeaf = source;
}

[[nodiscard]] Article RichCopyArticle() {
	auto result = Article();
	auto &blocks = result.blocks;
	blocks.resize(4);
	SetTextBlock(blocks[0], u"Hello world"_q, { .block = RootPath(0) });
	blocks[1].kind = PreparedBlockKind::List;
	blocks[1].children.resize(3);
	const auto items = std::array{ u"One"_q, u"Two"_q, u"Three"_q };
	for (auto i = 0; i != int(items.size()); ++i) {
		auto &item = blocks[1].children[i];
		item.kind = PreparedBlockKind::ListItem;
		const auto source = PreparedEditLeafSource{
			.kind = PreparedEditLeafKind::ListItemText,
			.block = RootPath(1),
			.listItemIndex = i,
		};
		SetTextBlock(item.children.emplace_back(), items[i], source);
	}
	blocks[2].kind = PreparedBlockKind::Quote;
	SetTextBlock(
		blocks[2].children.emplace_back(),
		u"Quoted"_q,
		{ .block = ChildPath(2, 0) });
	auto tail = PreparedEditLeafSource{ .block = RootPath(3) };
	tail.block.container.steps.push_back({
		.kind = PreparedEditBlockContainerKind::Root,
	});
	SetTextBlock(blocks[3], u"Tail"_q, tail);
	CollectSelectableSegments(&result.blocks, &result.segments);
	return result;
}

[[nodiscard]] Article SearchArticle() {
	auto result = Article();
	auto &blocks = result.blocks;
	blocks.resize(3);
	SetLeaf(blocks[0].leaf, u"Hello world"_q);
	SetLeaf(blocks[1].leaf, u"Second"_q);
	blocks[2].kind = PreparedBlockKind::DisplayMath;
	CollectSelectableSegments(&result.blocks, &result.segments);
	return result;
}

void ExpectBlock(
		Expect &expect,
		const QString &prefix,
		const RichPage::Block &block,
		const ExpectedBlock &wanted) {
	expect.number(prefix + u"kind"_q, int(block.kind), int(wanted.kind));
	expect.text(prefix + u"text"_q, block.text.text.text, wanted.text);
	expect.entities(
		prefix + u"entities"_q,
		block.text.text.entities,
		wanted.entities);
	const auto &items = block.listItems;
	const auto count = int(wanted.items.size());
	if (expect.number(prefix + u"items"_q, int(items.size()), count)) {
		for (auto i = 0; i != count; ++i) {
			expect.text(
				prefix + u"item %1"_q.arg(i),
				items[i].text.text.text,
				wanted.items[i]);
		}
	}
	const auto &nested = block.blocks;
	const auto nestedCount = wanted.nested.isEmpty() ? 0 : 1;
	if (expect.number(prefix + u"nested"_q, int(nested.size()), nestedCount)
		&& nestedCount > 0) {
		expect.text(
			prefix + u"nested text"_q,
			nested.front().text.text.text,
			wanted.nested);
	}
}

void ExpectBlocks(
		Expect &expect,
		const QString &what,
		const std::vector<RichPage::Block> &got,
		const std::vector<ExpectedBlock> &expected) {
	const auto count = int(expected.size());
	if (!expect.number(what + u" blocks"_q, int(got.size()), count)) {
		return;
	}
	for (auto i = 0; i != count; ++i) {
		const auto prefix = what + u" block %1 "_q.arg(i);
		ExpectBlock(expect, prefix, got[i], expected[i]);
	}
}

void ExpectSlice(
		Expect &expect,
		const QString &what,
		MarkdownArticleSelection selection,
		const std::vector<ExpectedBlock> &expected) {
	const auto page = RichCopyPage();
	const auto article = RichCopyArticle();
	ExpectBlocks(
		expect,
		what,
		RichPageBlocksForSelectedSegments(
			page,
			article.segments,
			selection),
		expected);
}

[[nodiscard]] Fn<QString()> RichCheck(
		MarkdownArticleSelection selection,
		std::vector<ExpectedBlock> expected) {
	return [=] {
		auto expect = Expect();
		ExpectSlice(expect, u"slice"_q, selection, expected);
		return expect.result();
	};
}

[[nodiscard]] QString SameLeafTrim() {
	const auto trimmed = std::vector<ExpectedBlock>{ {
		.text = u"lo wo"_q,
		.entities = {
			EntityInText(EntityType::Bold, 0, 2),
			EntityInText(EntityType::Italic, 3, 2),
		},
	} };
	auto expect = Expect();
	ExpectSlice(expect, u"forward"_q, { { 0, 3 }, { 0, 8 } }, trimmed);
	ExpectSlice(expect, u"reversed"_q, { { 0, 8 }, { 0, 3 } }, trimmed);
	return expect.result();
}

[[nodiscard]] QString UnresolvableFallback() {
	auto page = RichPage();
	page.blocks.push_back(HelloParagraph());
	auto article = Article();
	article.blocks.resize(2);
	SetTextBlock(
		article.blocks[0],
		u"Hello world"_q,
		{ .block = RootPath(0) });
	SetLeaf(article.blocks[1].leaf, u"Loose"_q);
	CollectSelectableSegments(&article.blocks, &article.segments);
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(article.segments.size()), 2)) {
		return expect.result();
	}
	ExpectBlocks(
		expect,
		u"slice"_q,
		RichPageBlocksForSelectedSegments(
			page,
			article.segments,
			{ { 0, 6 }, { 1, 3 } }),
		{ {
			.text = u"world"_q,
			.entities = { EntityInText(EntityType::Italic, 0, 5) },
		} });
	return expect.result();
}

[[nodiscard]] QString Invalid() {
	const auto page = RichCopyPage();
	const auto article = RichCopyArticle();
	auto reversed = Article();
	reversed.blocks.resize(2);
	SetTextBlock(reversed.blocks[0], u"Late"_q, { .block = RootPath(3) });
	SetTextBlock(reversed.blocks[1], u"Early"_q, { .block = RootPath(1) });
	CollectSelectableSegments(&reversed.blocks, &reversed.segments);
	auto expect = Expect();
	ExpectBlocks(
		expect,
		u"collapsed"_q,
		RichPageBlocksForSelectedSegments(
			page,
			article.segments,
			{ { 1, 1 }, { 1, 1 } }),
		{});
	ExpectBlocks(
		expect,
		u"page without blocks"_q,
		RichPageBlocksForSelectedSegments(
			RichPage(),
			article.segments,
			{ { 0, 0 }, { 0, 5 } }),
		{});
	const auto reversedCount = int(reversed.segments.size());
	if (expect.number(u"reversed segments"_q, reversedCount, 2)) {
		ExpectBlocks(
			expect,
			u"reversed tops"_q,
			RichPageBlocksForSelectedSegments(
				page,
				reversed.segments,
				{ { 0, 0 }, { 1, 1 } }),
			{});
	}
	return expect.result();
}

[[nodiscard]] SearchMatches SortedMatches(
		Expect &expect,
		SearchMatches matches) {
	const auto bySegment = [](
			const MarkdownArticleSearchMatch &a,
			const MarkdownArticleSearchMatch &b) {
		return (a.segment < b.segment);
	};
	expect.flag(
		u"matches sorted by segment"_q,
		std::is_sorted(matches.begin(), matches.end(), bySegment),
		true);
	return matches;
}

[[nodiscard]] SearchMatches SearchFixtureMatches(Expect &expect) {
	auto matches = SearchMatches{
		{ .segment = 0, .from = 0, .to = 5 },
		{ .segment = 0, .from = 6, .to = 11 },
		{ .segment = 0, .from = 9, .to = 20 },
		{ .segment = 0, .from = 12, .to = 15 },
		{ .segment = 0, .from = 4, .to = 4 },
		{ .segment = 1, .from = 0, .to = 3 },
		{ .segment = 2, .from = 0, .to = 1 },
	};
	return SortedMatches(expect, std::move(matches));
}

void ExpectSearchRanges(
		Expect &expect,
		const QString &what,
		const PaintSearchSegmentRanges &got,
		std::optional<TextSelection> current,
		const std::vector<TextSelection> &other) {
	expect.selection(what + u" current"_q, got.current, current);
	const auto count = int(other.size());
	if (expect.number(what + u" other"_q, int(got.other.size()), count)) {
		for (auto i = 0; i != count; ++i) {
			expect.selection(
				what + u" other %1"_q.arg(i),
				got.other[i],
				other[i]);
		}
	}
}

[[nodiscard]] Fn<QString()> SearchCheck(
		int index,
		std::optional<TextSelection> current,
		std::vector<TextSelection> other) {
	return [=] {
		const auto article = SearchArticle();
		auto expect = Expect();
		const auto matches = SearchFixtureMatches(expect);
		ExpectSearchRanges(
			expect,
			u"segment %1"_q.arg(index),
			PaintSearchRangesForSegmentIndex(
				{ .segments = &article.segments },
				{ .matches = &matches, .current = 1 },
				index),
			current,
			other);
		return expect.result();
	};
}

[[nodiscard]] QString SearchNonTextAndInvalid() {
	const auto article = SearchArticle();
	const auto selection = PaintSelectionState{
		.segments = &article.segments,
	};
	const auto none = SearchMatches();
	auto expect = Expect();
	const auto matches = SearchFixtureMatches(expect);
	if (!expect.number(u"segments"_q, int(article.segments.size()), 3)) {
		return expect.result();
	}
	expect.number(
		u"segment 2 kind"_q,
		int(article.segments[2].kind),
		int(SelectableSegmentKind::DisplayMath));
	for (const auto index : { 2, 3, -1 }) {
		ExpectSearchRanges(
			expect,
			u"segment %1"_q.arg(index),
			PaintSearchRangesForSegmentIndex(
				selection,
				{ .matches = &matches, .current = 1 },
				index),
			std::nullopt,
			{});
	}
	ExpectSearchRanges(
		expect,
		u"empty matches"_q,
		PaintSearchRangesForSegmentIndex(
			selection,
			{ .matches = &none, .current = 1 },
			0),
		std::nullopt,
		{});
	ExpectSearchRanges(
		expect,
		u"null matches"_q,
		PaintSearchRangesForSegmentIndex(selection, { .current = 1 }, 0),
		std::nullopt,
		{});
	return expect.result();
}

[[nodiscard]] QString SearchUint16Cap() {
	const auto article = SearchArticle();
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(article.segments.size()), 3)) {
		return expect.result();
	}
	auto segments = article.segments;
	segments[0].length = 70000;
	auto capped = SearchMatches{
		{ .segment = 0, .from = 65530, .to = 69999 },
		{ .segment = 0, .from = 65535, .to = 65540 },
	};
	const auto matches = SortedMatches(expect, std::move(capped));
	ExpectSearchRanges(
		expect,
		u"segment 0"_q,
		PaintSearchRangesForSegmentIndex(
			{ .segments = &segments },
			{ .matches = &matches, .current = 0 },
			0),
		TextSelection(65530, 65535),
		{});
	return expect.result();
}

} // namespace

std::vector<Check> RichCopyChecks() {
	using Kind = RichPage::BlockKind;
	const auto list = ExpectedBlock{
		.kind = Kind::List,
		.items = { u"One"_q, u"Two"_q, u"Three"_q },
	};
	const auto quote = ExpectedBlock{
		.kind = Kind::Quote,
		.nested = u"Quoted"_q,
	};
	const auto trimmedHead = ExpectedBlock{
		.text = u"llo world"_q,
		.entities = {
			EntityInText(EntityType::Bold, 0, 3),
			EntityInText(EntityType::Italic, 4, 5),
		},
	};
	return {
		{ u"rich.same-leaf-trim"_q, SameLeafTrim },
		{
			u"rich.end-trim"_q,
			RichCheck(
				{ { 0, 0 }, { 0, 5 } },
				{ {
					.text = u"Hello"_q,
					.entities = { EntityInText(EntityType::Bold, 0, 5) },
				} }),
		},
		{
			u"rich.list-item-span"_q,
			RichCheck(
				{ { 1, 1 }, { 2, 2 } },
				{ { .kind = Kind::List, .items = { u"ne"_q, u"Tw"_q } } }),
		},
		{
			u"rich.list-to-tail"_q,
			RichCheck(
				{ { 1, 0 }, { 5, 4 } },
				{ list, quote, { .text = u"Tail"_q } }),
		},
		{
			u"rich.nested-source"_q,
			RichCheck({ { 4, 1 }, { 4, 3 } }, { quote }),
		},
		{
			u"rich.root-step"_q,
			RichCheck({ { 5, 1 }, { 5, 3 } }, { { .text = u"ai"_q } }),
		},
		{
			u"rich.empty-edge-start"_q,
			RichCheck(
				{ { 0, 11 }, { 5, 2 } },
				{ list, quote, { .text = u"Ta"_q } }),
		},
		{
			u"rich.empty-edge-end"_q,
			RichCheck({ { 0, 2 }, { 3, 0 } }, { trimmedHead, list }),
		},
		{ u"rich.unresolvable-fallback"_q, UnresolvableFallback },
		{ u"rich.invalid"_q, Invalid },
		{
			u"search.current-and-other"_q,
			SearchCheck(
				0,
				TextSelection(6, 11),
				{ TextSelection(0, 5), TextSelection(9, 11) }),
		},
		{
			u"search.other-segment"_q,
			SearchCheck(1, std::nullopt, { TextSelection(0, 3) }),
		},
		{ u"search.non-text-and-invalid"_q, SearchNonTextAndInvalid },
		{ u"search.uint16-cap"_q, SearchUint16Cap },
	};
}

} // namespace Iv::Markdown::Tests
