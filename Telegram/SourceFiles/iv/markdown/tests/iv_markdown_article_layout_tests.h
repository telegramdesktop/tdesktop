/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/tests/iv_markdown_article_tests.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "iv/iv_rich_page.h"

namespace Iv::Markdown::Tests {

struct LayoutRun {
	MarkdownArticleContent content;
	std::vector<LaidOutBlock> blocks;
	int width = 0;
	int bottom = 0;
};

[[nodiscard]] RichPage::RichText MakeText(const QString &text);
[[nodiscard]] RichPage::Block MakeBlock(
	RichPage::BlockKind kind,
	const QString &text = QString(),
	std::vector<RichPage::Block> blocks = {});
[[nodiscard]] RichPage::ListItem MakeItem(
	const QString &text,
	std::vector<RichPage::Block> blocks = {});
[[nodiscard]] RichPage::TableCell MakeCell(
	const QString &text,
	int colspan = 1,
	int rowspan = 1);
[[nodiscard]] RichPage MakePage(
	std::vector<RichPage::Block> blocks,
	bool rtl = false);
[[nodiscard]] RichPage FlowPage();
[[nodiscard]] RichPage SpansTablePage(bool bordered, bool compact);
[[nodiscard]] QStringList BorderlessCompactGeometry();

[[nodiscard]] std::optional<MarkdownArticleContent> PrepareArticle(
	Expect &expect,
	const QString &what,
	const RichPage &page);
[[nodiscard]] std::optional<LayoutRun> LayoutFresh(
	Expect &expect,
	const QString &what,
	const RichPage &page,
	int width);
[[nodiscard]] bool LayoutRetained(LayoutRun &run, int width);
[[nodiscard]] bool LayoutRetained(
	LayoutRun &run,
	MarkdownArticleContent content,
	int width);

[[nodiscard]] bool ExpectTopology(
	Expect &expect,
	const QString &what,
	const RichPage &page,
	const QStringList &expected);
void ExpectGeometry(
	Expect &expect,
	const QString &what,
	const LayoutRun &run,
	const QStringList &expected);
void ExpectSameGeometry(
	Expect &expect,
	const QString &what,
	const LayoutRun &run,
	const LayoutRun &reference);
void ExpectFreshAndRetained(
	Expect &expect,
	const QString &what,
	const RichPage &page,
	int width,
	const QStringList &expected);

} // namespace Iv::Markdown::Tests
