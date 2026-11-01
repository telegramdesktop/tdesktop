/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

#include <QtCore/QRect>
#include <QtCore/QStringList>

#include <optional>
#include <vector>

struct TextSelection;

namespace Ui::Text {
class String;
} // namespace Ui::Text

namespace Iv::Markdown {
struct LaidOutBlock;
struct MarkdownArticleSelection;
struct PreparedEditBlockPath;
struct PreparedEditSelection;
struct SelectableSegment;
} // namespace Iv::Markdown

namespace Iv::Markdown::Tests {

struct Check {
	QString name;
	Fn<QString()> run;
};

class Expect final {
public:
	void flag(const QString &what, bool got, bool expected);
	bool number(const QString &what, int got, int expected);
	void numbers(
		const QString &what,
		const std::vector<int> &got,
		const std::vector<int> &expected);
	void text(
		const QString &what,
		const QString &got,
		const QString &expected);
	void selection(
		const QString &what,
		std::optional<TextSelection> got,
		std::optional<TextSelection> expected);
	void position(
		const QString &what,
		MarkdownArticleSelection got,
		MarkdownArticleSelection expected);
	void rect(const QString &what, QRect got, QRect expected);
	void entities(
		const QString &what,
		const EntitiesInText &got,
		const EntitiesInText &expected);
	void copy(
		const QString &what,
		const TextForMimeData &got,
		const QString &expanded,
		const EntitiesInText &expectedEntities,
		const TextForMimeDataTags &expectedTags);

	[[nodiscard]] QString result() const;

private:
	void fail(
		const QString &what,
		const QString &got,
		const QString &expected);

	QStringList _failures;

};

struct Article {
	std::vector<LaidOutBlock> blocks;
	std::vector<SelectableSegment> segments;
};

void SetLeaf(Ui::Text::String &leaf, const QString &text);
[[nodiscard]] PreparedEditBlockPath RootPath(int index);
[[nodiscard]] PreparedEditBlockPath ChildPath(int parent, int index);
[[nodiscard]] PreparedEditSelection BlocksSelection(int from, int till);
[[nodiscard]] PreparedEditSelection TableRowsSelection(
	int block,
	int from,
	int till);
[[nodiscard]] PreparedEditSelection TableCellsSelection(
	int block,
	int rowFrom,
	int rowTill,
	int columnFrom,
	int columnTill);

[[nodiscard]] std::vector<Check> SetupChecks();
[[nodiscard]] std::vector<Check> RangeChecks();
[[nodiscard]] std::vector<Check> SegmentChecks();
[[nodiscard]] std::vector<Check> TableChecks();
[[nodiscard]] std::vector<Check> CodeChecks();
[[nodiscard]] std::vector<Check> RichCopyChecks();

} // namespace Iv::Markdown::Tests
