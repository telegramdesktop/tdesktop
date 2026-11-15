/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_tests.h"

#include "iv/markdown/iv_markdown_article_selection.h"
#include "lang/lang_text_entity.h"

#include <array>

namespace Iv::Markdown::Tests {
namespace {

constexpr auto kWordJoiner = QChar(0x2060);
const auto kCodeSource = u"a\tb\n\tc"_q;
const auto kGuardSource = u"a\t"_q;
const auto kLanguage = u"cpp"_q;

struct CodeCopy {
	TextSelection range;
	QString text;
	EntitiesInText entities;
};

[[nodiscard]] EntityInText Pre(
		int offset,
		int length,
		const QString &language) {
	return EntityInText(EntityType::Pre, offset, length, language);
}

void SetCodeBlock(
		LaidOutBlock &block,
		const QString &source,
		const QString &language) {
	block.kind = PreparedBlockKind::CodeBlock;
	block.codeText = tr::marked(source);
	block.codeLanguage = language;
	SetLeaf(block.leaf, CodeBlockDisplayText(source));
}

[[nodiscard]] Article CodeArticle(bool guard) {
	auto result = Article();
	result.blocks.resize(guard ? 2 : 1);
	SetCodeBlock(result.blocks[0], kCodeSource, kLanguage);
	if (guard) {
		SetCodeBlock(result.blocks[1], kGuardSource, QString());
	}
	CollectSelectableSegments(&result.blocks, &result.segments);
	return result;
}

void ExpectGuardFree(
		Expect &expect,
		const QString &what,
		const TextForMimeData &copy) {
	expect.flag(
		what + u" contains U+2060"_q,
		copy.expanded.contains(kWordJoiner)
			|| copy.rich.text.contains(kWordJoiner),
		false);
}

void ExpectCodeCopies(
		Expect &expect,
		const SelectableSegment &segment,
		const std::vector<CodeCopy> &copies) {
	for (const auto &entry : copies) {
		const auto what = u"[%1,%2)"_q
			.arg(entry.range.from)
			.arg(entry.range.to);
		const auto copy = TextForSegment(segment, entry.range);
		expect.copy(what, copy, entry.text, entry.entities, {});
		ExpectGuardFree(expect, what, copy);
	}
}

[[nodiscard]] QString Offsets() {
	auto expect = Expect();
	expect.numbers(
		u"offsets"_q,
		CodeBlockDisplayOffsets(kCodeSource),
		{ 0, 1, 4, 5, 6, 10, 11 });
	return expect.result();
}

[[nodiscard]] QString Display() {
	auto expect = Expect();
	expect.text(
		u"display"_q,
		CodeBlockDisplayText(kCodeSource),
		u"a   b\n    c"_q);
	return expect.result();
}

[[nodiscard]] QString ColumnModulo() {
	const auto partial = u"ab\tc"_q;
	const auto doubled = u"\t\t"_q;
	auto expect = Expect();
	expect.numbers(
		u"partial offsets"_q,
		CodeBlockDisplayOffsets(partial),
		{ 0, 1, 2, 4, 5 });
	expect.text(
		u"partial display"_q,
		CodeBlockDisplayText(partial),
		u"ab  c"_q);
	expect.numbers(
		u"doubled offsets"_q,
		CodeBlockDisplayOffsets(doubled),
		{ 0, 4, 8 });
	expect.text(
		u"doubled display"_q,
		CodeBlockDisplayText(doubled),
		QString(8, QChar::Space) + kWordJoiner);
	return expect.result();
}

[[nodiscard]] QString Agreement() {
	const auto article = CodeArticle(false);
	const auto offsets = CodeBlockDisplayOffsets(kCodeSource);
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(article.segments.size()), 1)
		|| !expect.number(u"offset count"_q, int(offsets.size()), 7)) {
		return expect.result();
	}
	const auto &segment = article.segments.front();
	expect.number(u"segment length"_q, segment.length, 11);
	for (auto i = 0; i != int(kCodeSource.size()); ++i) {
		const auto range = TextSelection(
			uint16(offsets[i]),
			uint16(offsets[i + 1]));
		expect.text(
			u"display [%1,%2) source %3"_q
				.arg(range.from)
				.arg(range.to)
				.arg(i),
			TextForSegment(segment, range).expanded,
			kCodeSource.mid(i, 1));
	}
	return expect.result();
}

[[nodiscard]] QString CopyInteriorTab() {
	const auto article = CodeArticle(false);
	const auto &segments = article.segments;
	const auto pre = EntitiesInText{ Pre(0, 1, kLanguage) };
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(segments.size()), 1)) {
		return expect.result();
	}
	expect.copy(
		u"segment [2,3)"_q,
		TextForSegment(segments.front(), TextSelection(2, 3)),
		u"\t"_q,
		pre,
		{});
	expect.copy(
		u"selection {0,2}-{0,3}"_q,
		TextForSelectedSegments(segments, { { 0, 2 }, { 0, 3 } }, nullptr),
		u"\t"_q,
		pre,
		{});
	expect.copy(
		u"selection {0,3}-{0,2}"_q,
		TextForSelectedSegments(segments, { { 0, 3 }, { 0, 2 } }, nullptr),
		u"\t"_q,
		pre,
		{});
	return expect.result();
}

[[nodiscard]] QString CopyRanges() {
	const auto article = CodeArticle(false);
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(article.segments.size()), 1)) {
		return expect.result();
	}
	const auto copies = std::vector<CodeCopy>{
		{ TextSelection(1, 4), u"\t"_q, { Pre(0, 1, kLanguage) } },
		{ TextSelection(3, 4), u"\t"_q, { Pre(0, 1, kLanguage) } },
		{ TextSelection(5, 7), u"\n\t"_q, { Pre(0, 2, kLanguage) } },
		{ TextSelection(11, 12), QString() },
		{ AllTextSelection, kCodeSource, { Pre(0, 6, kLanguage) } },
	};
	ExpectCodeCopies(expect, article.segments.front(), copies);
	return expect.result();
}

[[nodiscard]] QString GuardDisplay() {
	auto expect = Expect();
	expect.text(
		u"empty"_q,
		CodeBlockDisplayText(QString()),
		QString(kWordJoiner));
	expect.text(
		u"trailing tab"_q,
		CodeBlockDisplayText(kGuardSource),
		u"a   "_q + kWordJoiner);
	expect.numbers(
		u"trailing tab offsets"_q,
		CodeBlockDisplayOffsets(kGuardSource),
		{ 0, 1, 4 });
	expect.text(
		u"trailing newline"_q,
		CodeBlockDisplayText(u"x\n"_q),
		u"x\n"_q + kWordJoiner);
	expect.text(
		u"plain end"_q,
		CodeBlockDisplayText(u"ab"_q),
		u"ab"_q);
	return expect.result();
}

[[nodiscard]] QString GuardNotCopied() {
	const auto article = CodeArticle(true);
	const auto &segments = article.segments;
	const auto guardPre = EntitiesInText{ Pre(0, 2, QString()) };
	auto expect = Expect();
	if (!expect.number(u"segments"_q, int(segments.size()), 2)) {
		return expect.result();
	}
	expect.number(u"guard length"_q, segments[1].length, 5);
	const auto copies = std::vector<CodeCopy>{
		{ TextSelection(4, 5), QString() },
		{ TextSelection(0, 5), kGuardSource, guardPre },
		{ AllTextSelection, kGuardSource, guardPre },
	};
	ExpectCodeCopies(expect, segments[1], copies);
	const auto both = TextForSelectedSegments(
		segments,
		{ { 0, 6 }, { 1, 5 } },
		nullptr);
	expect.copy(
		u"{0,6}-{1,5}"_q,
		both,
		u"\tc\na\t"_q,
		{ Pre(0, 2, kLanguage), Pre(3, 2, QString()) },
		{});
	ExpectGuardFree(expect, u"{0,6}-{1,5}"_q, both);
	return expect.result();
}

[[nodiscard]] QString EntityRemap() {
	auto source = tr::marked(kCodeSource);
	source.entities = {
		EntityInText(EntityType::Bold, 0, 3),
		Pre(0, 6, kLanguage),
		EntityInText(EntityType::CustomUrl, 4, 2, u"https://e.x"_q),
		EntityInText(EntityType::Italic, -1, 2),
		EntityInText(EntityType::Italic, 6, 1),
		EntityInText(EntityType::Italic, 2, 0),
		EntityInText(EntityType::Underline, 5, 10),
	};
	const auto display = CodeBlockDisplayText(source);
	auto expect = Expect();
	expect.text(u"text"_q, display.text, u"a   b\n    c"_q);
	expect.entities(
		u"entities"_q,
		display.entities,
		{
			EntityInText(EntityType::Bold, 0, 5),
			EntityInText(EntityType::CustomUrl, 6, 5, u"https://e.x"_q),
			EntityInText(EntityType::Underline, 10, 1),
		});
	return expect.result();
}

[[nodiscard]] QString Utf16() {
	const auto units = std::array<char16_t, 4>{ 0xD835, 0xDC00, u'\t', u'z' };
	const auto displayUnits = std::array<char16_t, 5>{
		0xD835,
		0xDC00,
		u' ',
		u' ',
		u'z',
	};
	const auto source = QString::fromUtf16(units.data(), units.size());
	const auto pair = QString::fromUtf16(units.data(), 2);
	auto marked = tr::marked(source);
	marked.entities = {
		EntityInText(EntityType::Bold, 2, 2),
		EntityInText(EntityType::Italic, 0, 2),
	};
	auto article = Article();
	article.blocks.resize(1);
	SetCodeBlock(article.blocks[0], source, kLanguage);
	CollectSelectableSegments(&article.blocks, &article.segments);
	const auto display = CodeBlockDisplayText(marked);
	auto expect = Expect();
	expect.number(u"source units"_q, int(source.size()), 4);
	expect.numbers(
		u"offsets"_q,
		CodeBlockDisplayOffsets(source),
		{ 0, 1, 2, 4, 5 });
	expect.text(
		u"display"_q,
		display.text,
		QString::fromUtf16(displayUnits.data(), displayUnits.size()));
	expect.number(u"display units"_q, int(display.text.size()), 5);
	expect.entities(
		u"entities"_q,
		display.entities,
		{
			EntityInText(EntityType::Bold, 2, 3),
			EntityInText(EntityType::Italic, 0, 2),
		});
	if (!expect.number(u"segments"_q, int(article.segments.size()), 1)) {
		return expect.result();
	}
	const auto copies = std::vector<CodeCopy>{
		{ TextSelection(2, 4), u"\t"_q, { Pre(0, 1, kLanguage) } },
		{ TextSelection(0, 2), pair, { Pre(0, 2, kLanguage) } },
	};
	ExpectCodeCopies(expect, article.segments.front(), copies);
	return expect.result();
}

} // namespace

std::vector<Check> CodeChecks() {
	return {
		{ u"code.offsets"_q, Offsets },
		{ u"code.display"_q, Display },
		{ u"code.column-modulo"_q, ColumnModulo },
		{ u"code.agreement"_q, Agreement },
		{ u"code.copy-interior-tab"_q, CopyInteriorTab },
		{ u"code.copy-ranges"_q, CopyRanges },
		{ u"code.guard-display"_q, GuardDisplay },
		{ u"code.guard-not-copied"_q, GuardNotCopied },
		{ u"code.entity-remap"_q, EntityRemap },
		{ u"code.utf16"_q, Utf16 },
	};
}

} // namespace Iv::Markdown::Tests
