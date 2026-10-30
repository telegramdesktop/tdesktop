/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/tests/iv_markdown_article_tests.h"

#include "iv/markdown/iv_markdown_article_selection.h"

#include "styles/style_basic.h"

namespace Iv::Markdown::Tests {
namespace {

constexpr auto kWordJoiner = 0x2060;

[[nodiscard]] QString Visible(const QString &text) {
	auto result = u"\""_q;
	for (const auto ch : text) {
		if (ch == QChar::Tabulation) {
			result.append(u"\\t"_q);
		} else if (ch == QChar::LineFeed) {
			result.append(u"\\n"_q);
		} else if (ch.unicode() == kWordJoiner) {
			result.append(u"<WJ>"_q);
		} else if (ch.isSurrogate()) {
			const auto code = int(ch.unicode());
			result.append(u"\\u%1"_q.arg(code, 4, 16, QChar(u'0')));
		} else {
			result.append(ch);
		}
	}
	return result.append(u"\""_q);
}

[[nodiscard]] QString NumbersText(const std::vector<int> &numbers) {
	auto parts = QStringList();
	for (const auto number : numbers) {
		parts.push_back(QString::number(number));
	}
	return u"["_q + parts.join(u","_q) + u"]"_q;
}

[[nodiscard]] QString SelectionText(std::optional<TextSelection> selection) {
	return selection
		? u"[%1,%2)"_q.arg(selection->from).arg(selection->to)
		: u"none"_q;
}

[[nodiscard]] QString PositionText(MarkdownArticleSelection selection) {
	return u"{%1,%2}-{%3,%4}"_q
		.arg(selection.from.segment)
		.arg(selection.from.offset)
		.arg(selection.to.segment)
		.arg(selection.to.offset);
}

[[nodiscard]] QString RectText(QRect rect) {
	return u"(%1,%2,%3,%4)"_q
		.arg(rect.x())
		.arg(rect.y())
		.arg(rect.width())
		.arg(rect.height());
}

[[nodiscard]] QString EntityName(EntityType type) {
	switch (type) {
	case EntityType::Bold: return u"Bold"_q;
	case EntityType::Italic: return u"Italic"_q;
	case EntityType::Underline: return u"Underline"_q;
	case EntityType::CustomUrl: return u"CustomUrl"_q;
	case EntityType::Pre: return u"Pre"_q;
	}
	return u"Type%1"_q.arg(int(type));
}

[[nodiscard]] QString EntitiesText(const EntitiesInText &entities) {
	auto parts = QStringList();
	for (const auto &entity : entities) {
		parts.push_back(u"%1(%2,%3,%4)"_q
			.arg(EntityName(entity.type()))
			.arg(entity.offset())
			.arg(entity.length())
			.arg(Visible(entity.data())));
	}
	return u"["_q + parts.join(u", "_q) + u"]"_q;
}

[[nodiscard]] QString TagsText(const TextForMimeDataTags &tags) {
	auto parts = QStringList();
	for (const auto &tag : tags) {
		parts.push_back(u"{%1,%2,%3}"_q
			.arg(tag.offset)
			.arg(tag.length)
			.arg(Visible(tag.id)));
	}
	return u"["_q + parts.join(u", "_q) + u"]"_q;
}

} // namespace

void Expect::flag(const QString &what, bool got, bool expected) {
	if (got != expected) {
		fail(
			what,
			got ? u"true"_q : u"false"_q,
			expected ? u"true"_q : u"false"_q);
	}
}

bool Expect::number(const QString &what, int got, int expected) {
	if (got != expected) {
		fail(what, QString::number(got), QString::number(expected));
	}
	return (got == expected);
}

void Expect::numbers(
		const QString &what,
		const std::vector<int> &got,
		const std::vector<int> &expected) {
	if (got != expected) {
		fail(what, NumbersText(got), NumbersText(expected));
	}
}

void Expect::text(
		const QString &what,
		const QString &got,
		const QString &expected) {
	if (got != expected) {
		fail(what, Visible(got), Visible(expected));
	}
}

void Expect::selection(
		const QString &what,
		std::optional<TextSelection> got,
		std::optional<TextSelection> expected) {
	if (got != expected) {
		fail(what, SelectionText(got), SelectionText(expected));
	}
}

void Expect::position(
		const QString &what,
		MarkdownArticleSelection got,
		MarkdownArticleSelection expected) {
	if (got != expected) {
		fail(what, PositionText(got), PositionText(expected));
	}
}

void Expect::rect(const QString &what, QRect got, QRect expected) {
	if (got != expected) {
		fail(what, RectText(got), RectText(expected));
	}
}

void Expect::entities(
		const QString &what,
		const EntitiesInText &got,
		const EntitiesInText &expected) {
	if (got != expected) {
		fail(what, EntitiesText(got), EntitiesText(expected));
	}
}

void Expect::copy(
		const QString &what,
		const TextForMimeData &got,
		const QString &expanded,
		const EntitiesInText &expectedEntities,
		const TextForMimeDataTags &expectedTags) {
	text(what + u" expanded"_q, got.expanded, expanded);
	text(what + u" rich"_q, got.rich.text, expanded);
	entities(what + u" entities"_q, got.rich.entities, expectedEntities);
	if (got.tags != expectedTags) {
		fail(what + u" tags"_q, TagsText(got.tags), TagsText(expectedTags));
	}
}

QString Expect::result() const {
	return _failures.join(u"; "_q);
}

void Expect::fail(
		const QString &what,
		const QString &got,
		const QString &expected) {
	_failures.push_back(
		what + u": got "_q + got + u", expected "_q + expected);
}

void SetLeaf(Ui::Text::String &leaf, const QString &text) {
	leaf.setText(st::defaultTextStyle, text, kPlainTextOptions);
}

PreparedEditBlockPath RootPath(int index) {
	return { .index = index };
}

PreparedEditBlockPath ChildPath(int parent, int index) {
	return {
		.container = { .steps = { {
			.kind = PreparedEditBlockContainerKind::BlockChildren,
			.blockIndex = parent,
		} } },
		.index = index,
	};
}

PreparedEditSelection BlocksSelection(int from, int till) {
	return {
		.kind = PreparedEditSelectionKind::Blocks,
		.blocks = { .from = from, .till = till },
	};
}

PreparedEditSelection TableRowsSelection(int block, int from, int till) {
	return {
		.kind = PreparedEditSelectionKind::TableRows,
		.tableRows = {
			.block = RootPath(block),
			.from = from,
			.till = till,
		},
	};
}

PreparedEditSelection TableCellsSelection(
		int block,
		int rowFrom,
		int rowTill,
		int columnFrom,
		int columnTill) {
	return {
		.kind = PreparedEditSelectionKind::TableCells,
		.tableCells = {
			.block = RootPath(block),
			.rowFrom = rowFrom,
			.rowTill = rowTill,
			.columnFrom = columnFrom,
			.columnTill = columnTill,
		},
	};
}

} // namespace Iv::Markdown::Tests
